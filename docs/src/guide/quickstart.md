# Quickstart

## 1. Build the host libmx

Unmex provides the `mx*`/`mex*` symbols a MEX needs via a small host library. Build
it once (needs a C compiler — `cc`/`gcc`/`clang`):

```bash
julia deps/build.jl        # → runtime/libmxhost.<so|dylib|dll>
```

`using Unmex` `dlopen`s this host with `RTLD_GLOBAL` so any MEX you open afterwards
resolves its symbols against it.

## 2. Open and call a MEX

```julia
using Unmex

mex = open_mex("double_it.mexa64")

# A Julia matrix marshals to a MATLAB matrix and back.
call(mex, [1.0 2.0 3.0; 4.0 5.0 6.0])    # → [2.0 4.0 6.0; 8.0 10.0 12.0]

# 1×1 results come back as a scalar.
call(mex, 3.0)                            # → 6.0

# Multiple outputs → a tuple.
# (U, S, V) = call(mex_svd, A; nargout = 3)
```

A Julia `Vector` becomes a MATLAB **N×1** column (MATLAB has no 1-D arrays), so it
returns as an `N×1` `Matrix`; `vec(...)` it if you want it flat.

## 3. Errors

If the MEX raises via `mexErrMsgIdAndTxt`, Unmex turns it into a Julia exception you
can `try`/`catch` — the process keeps running and the MEX stays usable:

```julia
try
    call(mex, A, B)          # wrong number of inputs
catch e
    @info "MEX errored" e
end
```

## One-shot

```julia
callmex("double_it.mexa64", 5.0)   # open_mex + call in one go → 10.0
```

## Supported types (MVP)

Real `double` scalars, vectors, and matrices, in both directions. Complex,
logical, char/string, cell, struct, and sparse are planned.
