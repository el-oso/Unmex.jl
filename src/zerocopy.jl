# Opt-in zero-copy input fast path for `call(mex, args...; copy=false)`.
#
# Default (`copy=true`) marshals each argument by allocating a fresh `mxArray` and copying
# the Julia data in — a safety firewall: the MEX gets its own buffer, so it cannot corrupt
# the caller's array. With `copy=false`, for a dense, real, directly-mappable numeric
# `Array` we instead build the input `mxArray` pointing **straight at Julia's buffer** (no
# allocation, no copy), and on cleanup null its data pointer before destroying it so Julia's
# memory is never freed by the host.
#
# Trade-off (why it is opt-in): the MEX now shares memory with the caller's array. A MEX
# that writes to a `prhs`, `mxDestroyArray`s it, or stashes the pointer past the call can
# corrupt or crash the process. Use only for a MEX you trust and a hot path where the copy
# dominates. Only inputs are borrowed; outputs are still copied (always safe).

const _ZC_CLASS = IdDict{DataType, Cint}(
    Float64 => mxDOUBLE_CLASS, Float32 => mxSINGLE_CLASS,
    Int8 => mxINT8_CLASS, Int16 => mxINT16_CLASS, Int32 => mxINT32_CLASS, Int64 => mxINT64_CLASS,
    UInt8 => mxUINT8_CLASS, UInt16 => mxUINT16_CLASS, UInt32 => mxUINT32_CLASS, UInt64 => mxUINT64_CLASS,
    Bool => mxLOGICAL_CLASS,
)

# Eligible: a dense `Array` (contiguous, owns its memory, stable pointer) whose element
# type maps 1:1 to an mxClassID. Views/adjoints/`Complex`/`String`/struct/cell fall back to
# the copy path.
_zerocopy_eligible(@nospecialize(a))::Bool = a isa Array && haskey(_ZC_CLASS, eltype(a))

function _borrow_mx(@nospecialize(a))::MxArray
    cls = _ZC_CLASS[eltype(a)]
    N = ndims(a)
    pa = if N == 1
        LibMx.mx_create_numeric_matrix(Csize_t(size(a, 1)), Csize_t(1), cls, LibMx.mxREAL)
    elseif N == 2
        LibMx.mx_create_numeric_matrix(Csize_t(size(a, 1)), Csize_t(size(a, 2)), cls, LibMx.mxREAL)
    else
        d = Csize_t[Csize_t(size(a, i)) for i in 1:N]
        GC.@preserve d LibMx.mx_create_numeric_array(Csize_t(N), pointer(d), cls, LibMx.mxREAL)
    end
    # Drop the host-allocated buffer and point pr at Julia's data (borrowed, not owned).
    ccall(:mxFree, Cvoid, (Ptr{Cvoid},), LibMx.mx_get_data(pa))
    ccall(:mxSetData, Cvoid, (MxArray, Ptr{Cvoid}), pa, pointer(a))
    return pa
end

# Destroy the prhs array; for borrowed entries, null `pr` first so the host never frees
# Julia's buffer (only the stub + dims are freed).
function _destroy_inputs(prhs::Vector{MxArray}, borrowed::Union{Nothing, BitVector})
    for i in eachindex(prhs)
        prhs[i] == C_NULL && continue
        if borrowed !== nothing && borrowed[i]
            ccall(:mxSetData, Cvoid, (MxArray, Ptr{Cvoid}), prhs[i], C_NULL)
        end
        mx_destroy_array(prhs[i])
    end
    return
end
