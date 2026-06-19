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

@testset "AbstractMxConverter contract (TypeContracts)" begin
    I = Unmex.AbstractMxConverter
    @test TypeContracts.check_contract(Unmex.DoubleConverter, I).passed
    @test TypeContracts.interface_trait(I, Unmex.DoubleConverter) isa TypeContracts.Implemented{I}
    # A type missing the methods must NOT satisfy the contract.
    struct NotAConverter end
    @test TypeContracts.interface_trait(I, NotAConverter) isa TypeContracts.NotImplemented{I}
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
    @testset "integer input is promoted to double" begin
        @test call(mex, [1 2; 3 4]) == [2.0 4.0; 6.0 8.0]
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
