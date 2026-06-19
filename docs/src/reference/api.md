# API Reference

## Opening and calling a MEX

```@docs
Unmex.open_mex
Unmex.call
Unmex.callmex
Unmex.MexFunction
```

## Marshaling

`julia_to_mx` / `mx_to_julia` delegate to per-class converter types that implement
the `Unmex.AbstractMxConverter` contract (`to_mx`, `from_mx`, `mx_class_id`).

```@docs
Unmex.julia_to_mx
Unmex.mx_to_julia
```
