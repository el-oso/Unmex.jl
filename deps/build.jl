# Build the libmx/libmex host that provides the mx*/mex* symbols a MEX resolves at
# load time. The host C source is owned by LibMx (the single source of truth shared
# with Mexicah's tests); compile it into runtime/ where Unmex.__init__ dlopens it.
using Libdl
using LibMx
using LinearAlgebra  # ensures libblastrampoline (LBT) is present for the BLAS bridge

const RT = joinpath(dirname(@__DIR__), "runtime")

if Sys.islinux()
    # Real MATLAB-compiled MEX import versioned symbols (mx*_800 etc., now aliased in the
    # host) and carry DT_NEEDED entries for libmx.so / libmex.so. Build the host under both
    # sonames so __init__ can satisfy those DT_NEEDED from the already-loaded host.
    LibMx.build_libmxhost(joinpath(RT, "libmx.so"); soname = "libmx.so")
    LibMx.build_libmxhost(joinpath(RT, "libmex.so"); soname = "libmex.so")
    @info "Unmex: built host as libmx.so + libmex.so from LibMx's canonical source" RT

    # BLAS bridge: a libmwblas.so that forwards MATLAB's plain ILP64 BLAS names to Julia's
    # libblastrampoline `_64_` entry points (→ OpenBLAS, or MKL after `using MKL`), so MEX
    # that link MATLAB's BLAS load and run with a real (MATLAB-grade) BLAS.
    cc = something(Sys.which("cc"), Sys.which("gcc"), Sys.which("clang"))
    cc === nothing && error("Unmex/build: no C compiler for the BLAS shim")
    lbt = Libdl.dlpath("libblastrampoline")
    run(`$cc -O2 -shared -fPIC -Wl,-soname,libmwblas.so -o $(joinpath(RT, "libmwblas.so")) $(joinpath(RT, "libmwblas_shim.c")) $lbt`)
    @info "Unmex: built BLAS bridge libmwblas.so → libblastrampoline" lbt
else
    LibMx.build_libmxhost(joinpath(RT, "libmxhost.$(Libdl.dlext)"))
    @info "Unmex: built host libmxhost from LibMx's canonical source" RT
end
