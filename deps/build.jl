# Build the libmx/libmex host that provides the mx*/mex* symbols a MEX resolves at
# load time. The host C source is owned by LibMx (the single source of truth shared
# with Mexicah's tests); compile it into runtime/ where Unmex.__init__ dlopens it.
using Libdl
using LibMx

const OUT = joinpath(dirname(@__DIR__), "runtime", "libmxhost.$(Libdl.dlext)")
LibMx.build_libmxhost(OUT)
@info "Unmex: built host libmx from LibMx's canonical source" OUT
