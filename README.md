# Unmex.jl

[![Dev](https://img.shields.io/badge/docs-dev-blue.svg)](https://el-oso.github.io/Unmex.jl/dev)
[![Build Status](https://github.com/el-oso/Unmex.jl/actions/workflows/CI.yml/badge.svg?branch=master)](https://github.com/el-oso/Unmex.jl/actions/workflows/CI.yml?query=branch%3Amaster)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

**Call a MATLAB MEX file from Julia — the inverse of
[Mexicah](https://github.com/el-oso/Mexicah.jl).** Mexicah compiles Julia → MEX
(Julia is the callee); Unmex `dlopen`s an existing `.mex*` and *calls* it (Julia is
the caller), converting `mxArray`s ↔ Julia values.

**No MATLAB required.** Unmex builds a from-scratch host `libmx`/`libmex` that provides
the `mx*`/`mex*` symbols a MEX resolves at load time — including the `_730` large-array
aliases that MATLAB-compiled binaries link against. The host C source
(`cruntime/libmxhost.c`) and the mxArray FFI + marshaling core both live in
[LibMx](https://github.com/el-oso/LibMx.jl) (shared with Mexicah); `deps/build.jl`
compiles the host into `runtime/`.

## Installation

Unmex is not registered; add it directly from GitHub:

```julia
pkg> add https://github.com/el-oso/Unmex.jl
```

Then build the host once (needs a C compiler — `cc`/`gcc`/`clang`):

```julia
julia> import Unmex; include(joinpath(pkgdir(Unmex), "deps", "build.jl"))
```

## Usage

```julia
using Unmex

mex = open_mex("double_it.mexa64")
call(mex, [1.0 2.0; 3.0 4.0])     # → [2.0 4.0; 6.0 8.0]
call(mex, 3.0)                    # → 6.0
callmex("double_it.mexa64", 5.0)  # one-shot → 10.0

probe(mex)                        # a MEX carries no signature metadata; probe sweeps
                                  # arities/types and scans the binary for its contract
```

A MEX's `mexErrMsgIdAndTxt` is turned into a catchable Julia `ErrorException`
(via a `setjmp`/`longjmp` shim) rather than crashing the process.

See the [documentation](https://el-oso.github.io/Unmex.jl/dev) for how it works.

## Status

The full self-contained MATLAB type set round-trips in and out — the numeric tower
(int8/16/32/64, uint8/16/32/64, single, double), complex, logical, char/string,
string arrays, sparse, struct, and cell. The host implements the common `mx*`/`mex*`
API surface (memory, introspection, field/data accessors and mutators, strings) and
aliases every function to the `_700`/`_730`/`_800` versioned names real MATLAB releases
link against. Built under the `libmx.so`/`libmex.so` sonames, it satisfies a MEX's
`DT_NEEDED`, so **genuine MATLAB-compiled `.mexa64` that depend only on `libmx`/`libmex`
load and can be called** (verified against a real MATLAB install's MEX corpus). MEX that
also pull in interpreter libraries (`libmwfl`, `libmwm_interpreter`, …) or call back
into MATLAB
(`mexCallMATLAB` for a real builtin, `mexEvalString`, …) need a live interpreter;
the host can't fabricate those, but it fails **gracefully** — a catchable Julia
error instead of a crash. Function handles, classdef objects, and opaque types are
interpreter-backed and unsupported.

## License

MIT — see [LICENSE](LICENSE).

## Provenance & trademarks

This software is an independent, clean-room implementation of the publicly
documented MATLAB® C Matrix API — it contains no MathWorks headers, source, or
binaries. MATLAB and MEX are trademarks of The MathWorks, Inc.; this project is
not affiliated with, sponsored by, or endorsed by The MathWorks, Inc. See
[NOTICE](NOTICE) for details.
