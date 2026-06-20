```@raw html
---
layout: home

hero:
  name: Unmex.jl
  text: Call a MATLAB MEX from Julia
  tagline: Load a .mex* file and call it from Julia — no MATLAB. The inverse of Mexicah.
  actions:
    - theme: brand
      text: Quickstart
      link: /guide/quickstart
    - theme: alt
      text: How it works
      link: /guide/how-it-works
    - theme: alt
      text: API
      link: /reference/api

features:
  - title: No MATLAB required
    icon: 🧩
    details: "Unmex builds a from-scratch host libmx/libmex (shared via LibMx), so a MEX's mx*/mex* symbols — including the _730 large-array aliases MATLAB-compiled binaries link — resolve without a MATLAB install."
  - title: Errors, not crashes
    icon: 🛟
    details: "A MEX's mexErrMsgIdAndTxt unwinds through a setjmp/longjmp shim and surfaces as a catchable Julia exception."
  - title: Dynamic conversion
    icon: ⚡
    details: "Outputs are converted by their runtime mxClassID — no compile-time types needed, unlike Mexicah."
---
```

## What is Unmex.jl?

[Mexicah](https://github.com/el-oso/Mexicah.jl) compiles a Julia function into a
MATLAB **MEX** file (Julia is the *callee*). **Unmex does the opposite**: it
`dlopen`s an existing `.mex*` and *calls* it from Julia (Julia is the *caller*),
marshaling Julia values into MATLAB `mxArray`s and converting the outputs back.

```julia
using Unmex

mex = open_mex("double_it.mexa64")
call(mex, [1.0 2.0; 3.0 4.0])        # → [2.0 4.0; 6.0 8.0]
call(mex, 3.0)                       # → 6.0
callmex("double_it.mexa64", 5.0)     # one-shot → 10.0
```

A MEX is a shared library exporting one C symbol —
`void mexFunction(int nlhs, mxArray *plhs[], int nrhs, mxArray *prhs[])` — whose
`mx*`/`mex*` references resolve from `libmx`/`libmex` at load time. **No MATLAB is
needed**: Unmex provides those symbols itself via a host library built from LibMx's
canonical C source (`cruntime/libmxhost.c`, shared with Mexicah's tests), which
`deps/build.jl` compiles into `runtime/`.

## Status

A working **MVP**: real `Float64` scalars / vectors / matrices round-trip in and
out, and a MEX-raised error becomes a Julia exception (not a process crash). MEX
that call back into MATLAB (`mexCallMATLAB`, engine features) are out of scope —
they need a live interpreter.
