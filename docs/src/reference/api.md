# API Reference

## Opening and calling a MEX

```@docs
Unmex.open_mex
Unmex.call
Unmex.callmex
Unmex.MexFunction
```

## Discovering a MEX's signature

A `.mex*` exports only `mexFunction` and carries no signature metadata, so the call
signature is opaque. `probe` recovers what it can — an arity sweep, a type probe, and a
scan of the binary for the author's `mexErrMsgIdAndTxt` contract strings.

```@docs
Unmex.probe
```

## Marshaling

`julia_to_mx` / `mx_to_julia` delegate to per-class converter types that implement
the `Unmex.AbstractMxConverter` contract (`to_mx`, `from_mx`, `mx_class_id`).

```@docs
Unmex.julia_to_mx
Unmex.mx_to_julia
```
