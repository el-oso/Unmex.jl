# Unmex.jl

**Call a MATLAB MEX file from Julia — the inverse of [Mexicah](../Mexicah.jl).**
Mexicah compiles Julia → MEX (Julia is the callee); Unmex `dlopen`s an existing
`.mex*` and *calls* it (Julia is the caller), converting `mxArray`s ↔ Julia values.

**No MATLAB required.** Unmex ships its own host `libmx`/`libmex` (`runtime/libmxhost.c`,
grown from Mexicah's libmx test stub) that provides the `mx*`/`mex*` symbols a MEX
resolves at load time.

```julia
using Unmex
mex = open_mex("double_it.mexa64")
call(mex, [1.0 2.0; 3.0 4.0])     # → [2.0 4.0; 6.0 8.0]
call(mex, 3.0)                    # → 6.0
callmex("double_it.mexa64", 5.0)  # one-shot → 10.0
```

## Status — working MVP

A real round-trip works end to end with **no MATLAB**: build inputs, invoke
`mexFunction`, read outputs back, and turn a MEX-raised `mexErrMsgIdAndTxt` into a
catchable Julia `ErrorException` (not a process `abort`). See `test/runtests.jl`.

Currently supported: real `Float64` scalars / vectors / matrices, in and out.

## How it works

1. **Provide the symbols** — `dlopen(libmxhost; RTLD_GLOBAL)` so a subsequently
   `dlopen`ed MEX binds its undefined `mx*`/`mex*` references to the host.
2. **Marshal inputs** — Julia value → `mxArray*` (`mxCreateDoubleMatrix` + copy);
   collect into a `prhs` vector.
3. **Invoke** — through a `setjmp`/`longjmp` C shim (`unmex_call`) so a MEX error
   unwinds to the host and is reported to Julia instead of crashing.
4. **Convert outputs** — `mxArray*` → Julia, dispatched on the runtime
   `mxGetClassID` (no compile-time types needed, unlike Mexicah).
5. **Manage lifetimes** — Unmex `mxDestroyArray`s every `mxArray` it owns.

## Build & test

```bash
julia deps/build.jl                       # build runtime/libmxhost.so (needs cc/gcc)
julia --project=. test/runtests.jl        # builds the C test MEX + runs the suite
```

## Limitations (MVP)

- No `mexCallMATLAB` / engine / `mexGetVariable` — those need a live MATLAB.
- Only real `double` is marshaled so far (complex, logical, char/string, cell,
  struct, sparse are next).
- Fidelity for arbitrary real-world MATLAB MEX grows with the host `libmx`'s
  completeness; the MVP targets well-behaved, Matrix-API-only MEX.

## Roadmap

- Extract a shared `LibMx.jl` core (the `mx*` FFI + marshalers) so Mexicah and
  Unmex share one implementation, and `libmxhost` is one artifact for both.
- Grow the host `libmx` (all `mxCreate*`, `_730` aliases, `mxMalloc` tracking).
- Full type coverage in both directions (strings, cells, structs, sparse, …).
