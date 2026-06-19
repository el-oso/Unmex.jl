using TypeContracts

# The interface every per-class converter (converters.jl) implements. Verified
# structurally — `check_contract(DoubleConverter, AbstractMxConverter)` — in
# test/contracts_test.jl. Converters implement it via plain dispatch (they don't
# subtype the contract), so the two-arg structural check is used, mirroring Mexicah.
@contract AbstractMxConverter "Bidirectional Julia ↔ mxArray conversion for one MATLAB class." begin
    to_mx(::Self, ::Any)::MxArray =>
        "Build a new mxArray from a Julia value. Value arg ::Any so hasmethod passes for any converter."
    from_mx(::Self, ::MxArray)::Any => "Read an mxArray as a Julia value"
    mx_class_id(::Self)::Cint => "mxClassID constant this converter handles"
end
