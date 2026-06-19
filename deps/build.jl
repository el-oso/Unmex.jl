# Build the host libmx/libmex (`libmxhost`) that provides the mx*/mex* symbols a
# MEX resolves at load time. Plain `cc -shared` — no MATLAB, no headers.
using Libdl

const ROOT = dirname(@__DIR__)
const SRC = joinpath(ROOT, "runtime", "libmxhost.c")
const OUT = joinpath(ROOT, "runtime", "libmxhost.$(Libdl.dlext)")

cc = something(Sys.which("cc"), Sys.which("gcc"), Sys.which("clang"))
cc === nothing && error("Unmex/build: no C compiler (cc/gcc/clang) found on PATH")

run(`$cc -O2 -shared -fPIC -o $OUT $SRC`)
@info "Unmex: built host libmx" OUT
