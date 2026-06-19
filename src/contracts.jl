using TypeContracts

# The interface every output converter (converters.jl) implements. Verified
# structurally — `check_contract(NumericConverter, AbstractMxConverter)` etc. — in
# test/contracts_test.jl. Converters implement it via plain dispatch (they don't
# subtype the contract), so the two-arg structural check is used, mirroring Mexicah.
#
# Output only: input marshaling (Julia → mxArray) delegates to LibMx's `store_result`
# and needs no per-type converter, so the contract carries just `from_mx`.
@contract AbstractMxConverter "Convert a returned mxArray to a Julia value (output side)." begin
    from_mx(::Self, ::MxArray)::Any => "Read an mxArray as a Julia value"
end
