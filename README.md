# Unmex.jl

[![Dev](https://img.shields.io/badge/docs-dev-blue.svg)](https://el-oso.github.io/Unmex.jl/dev)
[![Build Status](https://github.com/el-oso/Unmex.jl/actions/workflows/CI.yml/badge.svg?branch=master)](https://github.com/el-oso/Unmex.jl/actions/workflows/CI.yml?query=branch%3Amaster)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

**Call a MATLAB MEX file from Julia — the inverse of
[Mexicah](https://github.com/el-oso/Mexicah.jl).** Mexicah compiles Julia → MEX
(Julia is the callee); Unmex `dlopen`s an existing `.mex*` and *calls* it (Julia is
the caller), converting `mxArray`s ↔ Julia values.

**No MATLAB required.** Unmex ships its own host `libmx`/`libmex`
(`runtime/libmxhost.c`) that provides the `mx*`/`mex*` symbols a MEX resolves at
load time. The mxArray FFI + marshaling core is shared with Mexicah via
[LibMx](https://github.com/el-oso/LibMx.jl).

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
```

A MEX's `mexErrMsgIdAndTxt` is turned into a catchable Julia `ErrorException`
(via a `setjmp`/`longjmp` shim) rather than crashing the process.

See the [documentation](https://el-oso.github.io/Unmex.jl/dev) for how it works.

## Status

A working **MVP**: real `Float64` scalars / vectors / matrices round-trip in and
out. MEX that call back into MATLAB (`mexCallMATLAB`, engine features) need a live
interpreter and are out of scope.

## License

MIT — see [LICENSE](LICENSE).
