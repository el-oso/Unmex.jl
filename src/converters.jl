# Output converters: each turns a returned `mxArray` of one MATLAB class-family into
# a Julia value, implementing the `AbstractMxConverter` contract (`from_mx`). They
# delegate to LibMx's marshalers (built from the runtime class-id) for leaf types,
# and recurse through `mx_to_julia` for cells and structs.
#
# Input marshaling (Julia → mxArray) needs no converter here — `julia_to_mx`
# delegates to LibMx's `store_result`/`marshaler_for`, which already dispatch on the
# Julia type and cover the whole supported set.
#
# Coverage = every self-contained MATLAB class (no live MATLAB): the full numeric
# tower (int8/16/32/64, uint8/16/32/64, single, double), real & complex, dense &
# sparse, logical, char/string, struct (incl. arrays), cell — and nested
# combinations. Function handles / classdef objects / opaque types are interpreter-
# backed and unsupported (they have no data layout in the C Matrix API).
#
# Known limitation: a nested `Matrix{String}` (string array) inside a cell/struct does
# not round-trip — `StringArrayMarshaler.store!` is a placeholder (its output goes via a
# `store_result` override). Scalar `String`s nest fine (cells and structs special-case
# them), as do top-level strings and top-level string arrays.

const _INT_CLASS_TYPE = Dict{Cint, DataType}(
    mxINT8_CLASS => Int8, mxUINT8_CLASS => UInt8,
    mxINT16_CLASS => Int16, mxUINT16_CLASS => UInt16,
    mxINT32_CLASS => Int32, mxUINT32_CLASS => UInt32,
    mxINT64_CLASS => Int64, mxUINT64_CLASS => UInt64,
)

# LibMx's `load` is zero-copy (it wraps the mxArray's buffer), but Unmex frees the
# mxArray after converting — so every result must own its memory. `copy` captures the
# data while the mxArray is still alive.
#
# MATLAB has no 0-/1-D scalars: a 1×1 array collapses to a Julia scalar (a value copy);
# everything else keeps its N×M shape (a column comes back as N×1), copied out.
_maybe_scalar(A::AbstractArray) = length(A) == 1 ? @inbounds(A[begin]) : copy(A)

# ── Real numeric (double / single / int* / uint*) ─────────────────────────────
struct NumericConverter end
function from_mx(::NumericConverter, pa::MxArray)
    nd = Int(mx_get_number_of_dimensions(pa))
    cid = mx_get_class_id(pa)
    T = cid == mxDOUBLE_CLASS ? Float64 : cid == mxSINGLE_CLASS ? Float32 : _INT_CLASS_TYPE[cid]
    return _maybe_scalar(load(DenseArrayMarshaler{T, nd}(), pa))
end

# ── Complex (double / single) ─────────────────────────────────────────────────
struct ComplexConverter end
function from_mx(::ComplexConverter, pa::MxArray)
    nd = Int(mx_get_number_of_dimensions(pa))
    m = mx_get_class_id(pa) == mxSINGLE_CLASS ? ComplexF32ArrayMarshaler{nd}() :
        ComplexArrayMarshaler{nd}()
    return _maybe_scalar(load(m, pa))
end

# ── Logical ───────────────────────────────────────────────────────────────────
struct LogicalConverter end
from_mx(::LogicalConverter, pa::MxArray) =
    _maybe_scalar(load(LogicalArrayMarshaler{Int(mx_get_number_of_dimensions(pa))}(), pa))

# ── Char: a 1×N row is a String; an M×N (M>1) block is a Matrix{Char} ──────────
# (String is immutable so already owned; copy the char matrix out of the buffer.)
struct CharConverter end
from_mx(::CharConverter, pa::MxArray) =
    Int(mx_get_m(pa)) <= 1 ? load(StringMarshaler(), pa) : copy(load(CharMatrixMarshaler(), pa))

# ── Sparse (double / complex double / logical) ────────────────────────────────
struct SparseConverter end
function from_mx(::SparseConverter, pa::MxArray)
    mx_get_class_id(pa) == mxLOGICAL_CLASS && return copy(load(SparseLogicalMarshaler(), pa))
    mx_is_complex(pa) && return copy(load(SparseComplexF64Marshaler(), pa))
    return copy(load(SparseFloat64Marshaler(), pa))
end

# ── R2016b string array (host class 19) → Matrix{String} ──────────────────────
struct StringArrayConverter end
from_mx(::StringArrayConverter, pa::MxArray) = copy(load(StringArrayMarshaler(), pa))

# ── Cell → Array{Any} (recursive) ─────────────────────────────────────────────
struct CellConverter end
function from_mx(::CellConverter, pa::MxArray)
    m = Int(mx_get_m(pa))
    n = Int(mx_get_n(pa))
    out = Array{Any}(undef, m, n)
    for i in 1:(m * n)
        out[i] = mx_to_julia(mx_get_cell(pa, Csize_t(i - 1)))
    end
    return out
end

# ── Struct → NamedTuple (scalar) or Array{Any} of NamedTuples (struct array) ──
struct StructConverter end
function from_mx(::StructConverter, pa::MxArray)
    nf = Int(mx_get_number_of_fields(pa))
    names = ntuple(f -> Symbol(mx_get_field_name_by_number(pa, Cint(f - 1))), nf)
    _elt(idx) = NamedTuple{names}(
        ntuple(f -> mx_to_julia(mx_get_field(pa, Csize_t(idx), String(names[f]))), nf),
    )
    nel = Int(mx_get_number_of_elements(pa))
    nel == 1 && return _elt(0)
    out = Array{Any}(undef, Int(mx_get_m(pa)), Int(mx_get_n(pa)))
    for i in 1:nel
        out[i] = _elt(i - 1)
    end
    return out
end

# ── Dispatch: runtime mxClassID (+ complex/sparse flags) → output converter ───
function _output_converter(pa::MxArray)
    mx_is_sparse(pa) && return SparseConverter()
    cid = mx_get_class_id(pa)
    if cid == mxDOUBLE_CLASS || cid == mxSINGLE_CLASS
        return mx_is_complex(pa) ? ComplexConverter() : NumericConverter()
    elseif haskey(_INT_CLASS_TYPE, cid)
        return NumericConverter()
    elseif cid == mxLOGICAL_CLASS
        return LogicalConverter()
    elseif cid == mxCHAR_CLASS
        return CharConverter()
    elseif cid == mxCELL_CLASS
        return CellConverter()
    elseif cid == mxSTRUCT_CLASS
        return StructConverter()
    elseif cid == mxSTRING_CLASS
        return StringArrayConverter()
    end
    return error(
        "Unmex: unsupported output mxClassID $cid. Function handles (16), opaque (17), " *
        "and classdef objects (18) are interpreter-backed and need live MATLAB.",
    )
end
