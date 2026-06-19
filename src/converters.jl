# Per-class converters: each handles one MATLAB class in both directions
# (`to_mx`: Julia → mxArray for inputs; `from_mx`: mxArray → Julia for outputs).
# They implement the `AbstractMxConverter` contract (see contracts.jl), verified
# structurally in test/contracts_test.jl. Add a converter type + register it in the
# two dispatch tables below to support a new MATLAB class.

"""Real `double` scalars / vectors / matrices ↔ an `mxDOUBLE_CLASS` mxArray."""
struct DoubleConverter end

mx_class_id(::DoubleConverter)::Cint = mxDOUBLE_CLASS

# Julia → mxArray, via LibMx's FFI wrappers. The value arg is untyped (`::Any`) so
# `hasmethod(to_mx, (Self, Any))` holds for the contract; we branch on the value.
function to_mx(::DoubleConverter, x)::MxArray
    if x isa Real
        return mx_create_double_scalar(Cdouble(x))
    elseif x isa AbstractVecOrMat && eltype(x) <: Real
        A = x isa Array{Float64} ? x : convert(Array{Float64}, x)
        m = Csize_t(size(A, 1))
        n = Csize_t(ndims(A) == 1 ? 1 : size(A, 2))
        pa = mx_create_double_matrix(m, n, Cint(0))
        pr = mx_get_pr(pa)
        GC.@preserve A unsafe_copyto!(pr, pointer(A), length(A))
        return pa
    end
    return error("Unmex/DoubleConverter: cannot marshal a $(typeof(x)) (expected a real scalar/vector/matrix)")
end

# mxArray → Julia. A 1×1 collapses to a scalar; everything else stays an N×M
# Matrix (MATLAB has no 1-D arrays, so a column comes back as N×1).
function from_mx(::DoubleConverter, pa::MxArray)
    mx_is_complex(pa) && return error("Unmex: complex double output not supported yet")
    m = Int(mx_get_m(pa))
    n = Int(mx_get_n(pa))
    pr = mx_get_pr(pa)
    if m == 1 && n == 1
        return unsafe_load(pr)
    end
    B = Matrix{Float64}(undef, m, n)
    unsafe_copyto!(pointer(B), pr, m * n)
    return B
end

# ── Dispatch: input by Julia value type, output by runtime mxClassID ───────────

converter_for(::Real) = DoubleConverter()
converter_for(::AbstractVecOrMat{<:Real}) = DoubleConverter()
converter_for(x) = error("Unmex: no input converter for $(typeof(x)) (MVP supports real scalars/vectors/matrices)")

const _CLASS_CONVERTERS = Dict{Cint, Any}(mxDOUBLE_CLASS => DoubleConverter())

function converter_for_class(cid::Cint)
    haskey(_CLASS_CONVERTERS, cid) ||
        error("Unmex: no output converter for mxClassID $cid (MVP supports real double)")
    return _CLASS_CONVERTERS[cid]
end
