using Test, Libdl

const ROOT = dirname(@__DIR__)

# Build the host libmx and the C test MEX before loading Unmex (whose __init__
# dlopens the host).
include(joinpath(ROOT, "deps", "build.jl"))

const CC = something(Sys.which("cc"), Sys.which("gcc"), Sys.which("clang"))
function build_cmex(name)
    src = joinpath(ROOT, "test", "cmex", "$name.c")
    out = joinpath(ROOT, "test", "cmex", "$name.$(Libdl.dlext)")
    run(`$CC -O2 -shared -fPIC -o $out $src`)
    return out
end
const DOUBLE_IT = build_cmex("double_it")

using Unmex
using TypeContracts
using SparseArrays

@testset "AbstractMxConverter contract (TypeContracts)" begin
    I = Unmex.AbstractMxConverter
    converters = [
        Unmex.NumericConverter, Unmex.ComplexConverter, Unmex.LogicalConverter,
        Unmex.CharConverter, Unmex.SparseConverter, Unmex.StringArrayConverter,
        Unmex.CellConverter, Unmex.StructConverter,
    ]
    for C in converters
        @test TypeContracts.check_contract(C, I).passed
        @test TypeContracts.interface_trait(I, C) isa TypeContracts.Implemented{I}
    end
    # A type missing the methods must NOT satisfy the contract.
    struct NotAConverter end
    @test TypeContracts.interface_trait(I, NotAConverter) isa TypeContracts.NotImplemented{I}
end

@testset "type round-trip: julia_to_mx → mx_to_julia (no MATLAB)" begin
    # Build an mxArray from a Julia value, read it back, free it.
    function rt(x)
        pa = Unmex.julia_to_mx(x)
        y = Unmex.mx_to_julia(pa)
        Unmex.mx_destroy_array(pa)
        return y
    end

    @testset "integer tower (scalars + matrix)" begin
        for T in (Int8, Int16, Int32, Int64, UInt8, UInt16, UInt32, UInt64)
            @test rt(T(7)) === T(7)
        end
        @test rt(Int32[1 2; 3 4]) == Int32[1 2; 3 4]
    end
    @testset "floats" begin
        @test rt(2.5) === 2.5
        @test rt(2.5f0) === 2.5f0
        @test rt(Float32[1 2; 3 4]) == Float32[1 2; 3 4]
        @test rt([1.0 2.0; 3.0 4.0]) == [1.0 2.0; 3.0 4.0]
    end
    @testset "complex (double + single)" begin
        @test rt([1.0 + 2im 3.0 - 4im]) == [1.0 + 2im 3.0 - 4im]
        @test rt(ComplexF32[1 + 2im 3 - 4im]) == ComplexF32[1 + 2im 3 - 4im]
    end
    @testset "logical" begin
        @test rt(true) === true
        @test rt([true false; false true]) == [true false; false true]
    end
    @testset "char / string" begin
        @test rt("hello") == "hello"
        @test rt(['a' 'b'; 'c' 'd']) == ['a' 'b'; 'c' 'd']
    end
    @testset "string array (Matrix{String})" begin
        @test rt(["a" "b"; "c" "d"]) == ["a" "b"; "c" "d"]
    end
    @testset "sparse (double + logical)" begin
        @test rt(sparse([1.0 0.0; 0.0 2.0])) == sparse([1.0 0.0; 0.0 2.0])
        @test rt(sparse([true false; false true])) == sparse([true false; false true])
    end
    @testset "struct (NamedTuple, incl. array field)" begin
        @test rt((a = 1.0, b = 2.0)) == (a = 1.0, b = 2.0)
        y = rt((x = 3.0, v = [1.0, 2.0]))
        @test y.x == 3.0 && y.v == reshape([1.0, 2.0], 2, 1)
    end
    @testset "cell (Tuple → Array{Any}), incl. nested String + nested cell" begin
        y = rt((1.0, "x", Int32(7), [2.0 3.0]))
        @test y[1] == 1.0 && y[2] == "x" && y[3] === Int32(7) && y[4] == [2.0 3.0]
        # nested cell with a string round-trips too
        z = rt(("a", (2.0, "b")))
        @test z[1] == "a" && z[2][1] == 2.0 && z[2][2] == "b"
    end
end

@testset "Unmex MVP — call a C MEX from Julia (no MATLAB)" begin
    mex = open_mex(DOUBLE_IT)

    A = [1.0 2.0 3.0; 4.0 5.0 6.0]
    @testset "matrix round-trip" begin
        @test call(mex, A) == 2 .* A
    end
    @testset "scalar (1×1)" begin
        @test call(mex, 3.0) == 6.0
    end
    @testset "vector → N×1 (MATLAB has no 1-D arrays)" begin
        # A Julia Vector marshals to a MATLAB N×1 column, so it returns as an N×1
        # Matrix — faithful to MATLAB semantics. `vec(...)` if you want it flat.
        @test call(mex, [1.0, 2.0, 3.0]) == reshape([2.0, 4.0, 6.0], 3, 1)
    end
    @testset "MEX error → Julia exception (not a process abort)" begin
        # double_it raises mexErrMsgIdAndTxt unless nrhs==1.
        err = try
            call(mex, A, A)
            nothing
        catch e
            e
        end
        @test err isa ErrorException
        @test occursin("double_it", err.msg)
        @test occursin("Unmex:double_it:nargin", err.msg)
    end
    @testset "still usable after a caught MEX error" begin
        @test call(mex, A) == 2 .* A
    end
    @testset "callmex one-shot" begin
        @test callmex(DOUBLE_IT, 5.0) == 10.0
    end
end

@testset "Phase 1 — new mx* functions and _730 aliases" begin

    @testset "mxMalloc/mxSetData/mxSetM/mxSetN (build_via_setm)" begin
        mex = open_mex(build_cmex("build_via_setm"))
        # Scalar 5.0 → should return [1.0 2.0 3.0 4.0 5.0]
        result = call(mex, 5.0)
        @test result == reshape(1.0:5.0, 1, 5)
        # Scalar 1.0 → 1x1 matrix which converters return as scalar 1.0
        @test call(mex, 1.0) == 1.0
    end

    @testset "mxArrayToString + mxFree (arraytostring)" begin
        mex = open_mex(build_cmex("arraytostring"))
        # "hello" has 5 chars; mxArrayToString should return those 5 chars
        @test call(mex, "hello") == 5.0
        @test call(mex, "ab") == 2.0
        @test call(mex, "") == 0.0
    end

    @testset "mxGetFieldNumber + mxGetFieldByNumber (struct_fieldnum)" begin
        mex = open_mex(build_cmex("struct_fieldnum"))
        # Build a struct with fields x=1.0, y=42.0 and verify MEX returns y
        s = (x = 1.0, y = 42.0)
        @test call(mex, s) == 42.0
        # Try with a different y value
        @test call(mex, (x = -3.0, y = 7.0)) == 7.0
    end

    @testset "_730 large-array aliases (proxy730)" begin
        mex = open_mex(build_cmex("proxy730"))
        # N=4 → [1.0 2.0 3.0 4.0]
        result = call(mex, 4.0)
        @test result == reshape(1.0:4.0, 1, 4)
        @test call(mex, 1.0) == 1.0
    end

    @testset "_800 version aliases (proxy800 — modern MATLAB ABI)" begin
        # Real modern MATLAB-compiled MEX link mx*_800/mex*_800; this MEX uses only those.
        mex = open_mex(build_cmex("proxy800"))
        @test call(mex, 4.0) == reshape(1.0:4.0, 1, 4)
        @test call(mex, 1.0) == 1.0
    end

end

@testset "probe — best-effort introspection of an opaque MEX" begin
    mex = open_mex(DOUBLE_IT)
    buf = IOBuffer()
    probe(buf, mex)
    out = String(take!(buf))
    @test occursin("entry point: mexFunction", out)
    @test occursin("nargs=1 → OK", out)                       # arity discovered by sweep
    @test occursin("double_it expects exactly 1 input", out)  # contract string scanned
    @test occursin("Unmex:double_it:nargin", out)             # error id scanned
    # accepts a path too (one-shot), and the strings scan is independent of calling.
    @test !isempty(Unmex._scan_contract_strings(DOUBLE_IT))

    # scan-only mode (call=false) must not invoke the MEX (no arity sweep), but still scans.
    buf2 = IOBuffer()
    probe(buf2, mex; call = false)
    out2 = String(take!(buf2))
    @test occursin("call=false", out2)
    @test !occursin("nargs=", out2)
    @test occursin("Unmex:double_it:nargin", out2)
end

@testset "graceful failure on interpreter-only MEX (no crash)" begin
    # These reference interpreter-only symbols. Thanks to the host stubs they LOAD
    # (dlopen resolves the symbols), and raise a catchable Julia error when run —
    # instead of an undefined-symbol load failure or a process crash.
    @testset "mexEvalString → ErrorException" begin
        mex = open_mex(build_cmex("needs_eval"))
        e = try
            call(mex)
            nothing
        catch e
            e
        end
        @test e isa ErrorException
        @test occursin("mexEvalString", e.msg)
        @test occursin("live MATLAB", e.msg)
    end
    @testset "mexCallMATLAB(unsupported) → ErrorException" begin
        mex = open_mex(build_cmex("needs_callmatlab"))
        e = try
            call(mex)
            nothing
        catch e
            e
        end
        @test e isa ErrorException
        @test occursin("mexCallMATLAB", e.msg)
        @test occursin("sort", e.msg)
    end
end
