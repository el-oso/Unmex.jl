# How it works

A MEX file is a shared library exporting a single C entry point:

```c
void mexFunction(int nlhs, mxArray *plhs[], int nrhs, mxArray *prhs[]);
```

Its `mx*`/`mex*` references (`mxCreateDoubleMatrix`, `mxGetPr`, `mexErrMsgIdAndTxt`, …)
are **undefined** in the file — they resolve at load time from `libmx`/`libmex`.
Unmex calls it in six steps.

## 1. Provide the symbols

`using Unmex` `dlopen`s the bundled host `libmxhost` with `RTLD_GLOBAL`. When you
then `open_mex` a `.mex*`, the dynamic linker binds the MEX's undefined `mx*`/`mex*`
symbols to the host — no MATLAB needed.

## 2. Marshal inputs (Julia → `prhs`)

Each argument is converted to a freshly allocated `mxArray` by a per-class
converter (see below) and collected into a `prhs` vector.

## 3. Invoke through an error shim

Unmex never calls `mexFunction` directly. It calls a tiny C shim, `unmex_call`,
that arms a `setjmp` target and then runs the MEX. A MEX's `mexErrMsgIdAndTxt`
`longjmp`s back to that target instead of returning, so the error unwinds entirely
within C and is reported to Julia as a return code + id/message — which
[`call`](@ref) raises as an `ErrorException`. Without this, a MEX error would
`abort()` the whole process.

## 4. Convert outputs (`plhs` → Julia)

Mexicah knows its output types at compile time; Unmex does not — a returned
`mxArray` is whatever the MEX produced. So [`mx_to_julia`](@ref Unmex.mx_to_julia)
dispatches on the **runtime** `mxGetClassID` (plus the complex/sparse/cell/struct/
char flags) to pick the right Julia value.

## 5. Manage lifetimes

Unmex owns every `mxArray` it creates (the inputs) and the ones the MEX returns; it
`mxDestroyArray`s them after conversion, and `GC.@preserve`s the Julia buffers
across the `ccall`.

## The converter interface

Conversions live in per-class converter types that implement the
`AbstractMxConverter` [TypeContracts](https://github.com/el-oso/TypeContracts.jl)
contract — `to_mx` (Julia → mxArray), `from_mx` (mxArray → Julia), and
`mx_class_id`. The contract is verified structurally in the test suite, the same
discipline Mexicah uses for its marshalers. Supporting a new MATLAB class is a
matter of adding a converter type and registering it in the input/output dispatch
tables.

## The host library

`runtime/libmxhost.c` is a from-scratch, MATLAB-free implementation of the libmx
Matrix API (grown from Mexicah's libmx test stub). Because an `mxArray` is opaque to
the MEX — it only ever calls accessor functions — the host controls the struct
layout internally and a faithful set of `mx*` accessors is enough to serve a
well-behaved MEX.

## Interpreter-only MEX

Some MEX call back into the MATLAB interpreter (`mexCallMATLAB` for a real builtin,
`mexEvalString`, `mexGetVariable`, …). The host can't fabricate those without a live
MATLAB, but it fails **gracefully** rather than crashing:

- The interpreter symbols are present in the host (so `dlopen` with `RTLD_NOW`
  resolves them and the MEX loads), but each **raises a catchable Julia
  `ErrorException`** when actually called — e.g. `mexEvalString needs a live MATLAB
  interpreter`.
- The host's `mexCallMATLAB` **raises** for any builtin other than the emulated
  `"string"`/`"cellstr"` (instead of returning an error code), so a MEX that ignores
  the return value and dereferences the output can't segfault the process.
- A MEX that *returns* a function handle / classdef object / opaque type fails with a
  clear "unsupported output class" error.

The one remaining hard-crash path is a MEX that calls `mexCallMATLAB` and then derefs
its (untouched) output **without** the call unwinding first — which the host's raise
now prevents in the common case. A MEX that references an `mx*` accessor the host
doesn't implement yet fails cleanly at `open_mex` with an undefined-symbol error.
