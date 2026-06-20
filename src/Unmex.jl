"""
    Unmex

Call a MATLAB **MEX** file from Julia — the inverse of Mexicah (which compiles
Julia → MEX). Unmex `dlopen`s a `.mex*`, builds `mxArray*` inputs from Julia
values, invokes its `mexFunction`, and converts the `mxArray*` outputs back.

No MATLAB required: Unmex ships its own host `libmx`/`libmex` (`runtime/libmxhost`)
that provides the `mx*`/`mex*` symbols a MEX resolves at load time.

Scope: MEX that use the C Matrix API (the full self-contained MATLAB type set —
numeric, complex, logical, char/string, sparse, struct, cell — round-trips). MEX
that call back into MATLAB (`mexCallMATLAB` for a real builtin, `mexEvalString`, …)
need a live interpreter; the host can't fabricate those, but it fails **gracefully**
— they raise a catchable Julia error instead of crashing.

```julia
mex = Unmex.open_mex("double_it.mexa64")
Unmex.call(mex, [1.0 2.0; 3.0 4.0])   # → [2.0 4.0; 6.0 8.0]
```
"""
module Unmex

using Libdl

# Share the mxArray FFI + marshaling core with LibMx (the same code Mexicah uses).
# Inputs reuse LibMx's `store_result`/`marshaler_for`; outputs reuse its `load` +
# marshaler types, dispatched on the runtime class-id (see converters.jl).
using LibMx: MxArray, store_result, load,
    DenseArrayMarshaler, ComplexArrayMarshaler, ComplexF32ArrayMarshaler,
    LogicalArrayMarshaler, SparseFloat64Marshaler, SparseComplexF64Marshaler,
    SparseLogicalMarshaler, CharMatrixMarshaler, StringMarshaler, StringArrayMarshaler,
    mxDOUBLE_CLASS, mxSINGLE_CLASS, mxLOGICAL_CLASS, mxCHAR_CLASS, mxCELL_CLASS,
    mxSTRUCT_CLASS, mxINT8_CLASS, mxUINT8_CLASS, mxINT16_CLASS, mxUINT16_CLASS,
    mxINT32_CLASS, mxUINT32_CLASS, mxINT64_CLASS, mxUINT64_CLASS,
    mx_get_class_id, mx_is_complex, mx_is_sparse, mx_get_m, mx_get_n,
    mx_get_number_of_elements, mx_get_number_of_dimensions, mx_get_cell,
    mx_get_field, mx_get_field_name_by_number, mx_get_number_of_fields, mx_destroy_array

export open_mex, call, callmex, probe

# The host models R2016b `string` arrays as this class (libmxhost.c); not in LibMx's
# legacy enum (which stops at mxOBJECT_CLASS = 18).
const mxSTRING_CLASS = Cint(19)

# Output converters + the TypeContracts interface they satisfy. converters.jl defines
# `from_mx` and the per-class converter types; contracts.jl registers the contract
# that references them (so it is included after).
include("converters.jl")
include("contracts.jl")

const _HOST = Ref{Ptr{Cvoid}}(C_NULL)

_host_path() = joinpath(dirname(@__DIR__), "runtime", "libmxhost.$(Libdl.dlext)")

function __init__()
    path = _host_path()
    if isfile(path)
        # RTLD_GLOBAL so a subsequently-dlopen'd MEX binds its mx*/mex* symbols here.
        _HOST[] = dlopen(path, RTLD_GLOBAL | RTLD_NOW)
    else
        # Don't hard-fail at load (keeps `using Unmex` usable for docs/inspection);
        # MEX calls error clearly via `_ensure_host()` until the host is built.
        @warn "Unmex: host libmx not built at $path; run `julia $(joinpath(dirname(@__DIR__), "deps", "build.jl"))`. MEX calls will fail until then."
    end
    return
end

function _ensure_host()
    _HOST[] == C_NULL && error(
        "Unmex: host libmx is not loaded. Build it with " *
            "`julia $(joinpath(dirname(@__DIR__), "deps", "build.jl"))` and reload Unmex.",
    )
    return
end

# ── A loaded MEX ──────────────────────────────────────────────────────────────

"""
    MexFunction

A handle to an opened MEX file: its path, the `dlopen` library handle, and the
resolved `mexFunction` pointer. Create one with [`open_mex`](@ref) and invoke it
with [`call`](@ref).
"""
struct MexFunction
    path::String
    lib::Ptr{Cvoid}
    fn::Ptr{Cvoid}
end

Base.show(io::IO, m::MexFunction) = print(io, "MexFunction(", repr(basename(m.path)), ")")

"""
    open_mex(path) -> MexFunction

`dlopen` a MEX file and resolve its `mexFunction` entry point.
"""
function open_mex(path::AbstractString)::MexFunction
    _ensure_host()
    isfile(path) || error("Unmex: no such MEX file: $path")
    # `dlopen` treats a name with no directory separator as a soname to search for on the
    # library path, not a file in the CWD — so always hand it the absolute path.
    path = abspath(path)
    lib = dlopen(path, RTLD_NOW)
    fn = dlsym(lib, :mexFunction)
    return MexFunction(path, lib, fn)
end

# ── Julia ↔ mxArray (delegates to the per-class converters in converters.jl) ──

"""    julia_to_mx(x) -> mxArray

Marshal a Julia value into a freshly allocated `mxArray` (input side / `prhs`),
reusing LibMx's full marshaler set via `store_result` — so every Julia type LibMx
supports (numeric, complex, logical, char/string, sparse, struct, cell, …) works.
"""
function julia_to_mx(x)::MxArray
    buf = MxArray[C_NULL]
    GC.@preserve buf store_result(pointer(buf), 1, x)
    return buf[1]
end

"""    mx_to_julia(pa) -> value

Convert a returned `mxArray*` to a Julia value, dispatching on its runtime
`mxClassID` (+ complex/sparse flags) to the matching LibMx marshaler; cells and
structs are converted recursively. See `converters.jl`.
"""
mx_to_julia(pa::MxArray) =
    pa == C_NULL ? nothing : from_mx(_output_converter(pa), pa)

# ── call ──────────────────────────────────────────────────────────────────────

"""
    call(mex, args...; nargout=1)

Invoke `mex` with Julia `args` (marshaled to `mxArray`s) and convert the outputs
back. `nargout=1` returns the single output (or `nothing`); `nargout>1` returns a
tuple. A `mexErrMsgIdAndTxt` raised by the MEX becomes a Julia `ErrorException`.
"""
function call(mex::MexFunction, args...; nargout::Integer = 1)
    nrhs = length(args)
    nlhs = max(Int(nargout), 0)
    prhs = MxArray[julia_to_mx(a) for a in args]
    plhs = fill(MxArray(C_NULL), max(nlhs, 1))
    errid = Vector{UInt8}(undef, 256)
    errmsg = Vector{UInt8}(undef, 1024)

    rc = GC.@preserve prhs plhs errid errmsg ccall(
        :unmex_call, Cint,
        (Ptr{Cvoid}, Cint, Ptr{MxArray}, Cint, Ptr{MxArray}, Ptr{UInt8}, Csize_t, Ptr{UInt8}, Csize_t),
        mex.fn, nlhs, plhs, nrhs, prhs, errid, length(errid), errmsg, length(errmsg),
    )

    if rc != 0
        id = unsafe_string(pointer(errid))
        msg = unsafe_string(pointer(errmsg))
        _destroy_all(prhs)
        error("MEX raised [$id]: $msg")
    end

    outs = Any[mx_to_julia(plhs[i]) for i in 1:nlhs]
    _destroy_all(prhs)
    _destroy_all(@view plhs[1:nlhs])

    return nargout <= 1 ? (isempty(outs) ? nothing : outs[1]) : Tuple(outs)
end

"""
    callmex(path, args...; nargout=1)

One-shot: `open_mex(path)` then `call(...)`.
"""
callmex(path::AbstractString, args...; nargout::Integer = 1) =
    call(open_mex(path), args...; nargout = nargout)

function _destroy_all(ptrs)
    for p in ptrs
        p != C_NULL && mx_destroy_array(p)
    end
    return
end

include("probe.jl")

end # module Unmex
