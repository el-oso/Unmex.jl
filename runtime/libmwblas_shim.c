/* libmwblas_shim.c
 *
 * A drop-in `libmwblas.so` for loading real MATLAB MEX that link MATLAB's BLAS.
 * MATLAB's libmwblas exports the standard Fortran BLAS names with **ILP64** (64-bit
 * integer) dimensions and plain names (`dgemm_`, not `dgemm_64_`). Julia's
 * libblastrampoline (LBT) — already loaded in every Julia process — exposes the ILP64
 * entry points under the `_64_` suffix and routes them to a real BLAS backend (OpenBLAS
 * by default; Intel MKL, MATLAB's own backend, after `using MKL`).
 *
 * So this shim forwards MATLAB's plain ILP64 names to LBT's `_64_` ILP64 symbols. It is
 * implemented as **tail-call trampolines** (`jmp`): forwarding by jump passes every
 * argument register and the entire stack through unchanged, so we do not need to
 * replicate each BLAS signature — including the Fortran hidden character-length
 * arguments. MATLAB's libmwblas and OpenBLAS/MKL both use the gfortran calling
 * convention, so the caller's frame already matches the `_64_` callee's expectations.
 *
 * x86-64 Linux only (the .mexa64 ABI). Built with `-soname libmwblas.so` and linked
 * against libblastrampoline so a MEX's `DT_NEEDED libmwblas.so` is satisfied by this
 * already-loaded shim, and `*_64_` resolves from the in-process LBT.
 */

#if defined(__x86_64__) && defined(__linux__)

#define MW_BLAS_TRAMPOLINE(name)         \
    ".globl " #name "_\n"                \
    ".type " #name "_, @function\n"      \
    #name "_:\n"                         \
    "    jmp " #name "_64_@PLT\n"

__asm__(
    ".text\n"
    MW_BLAS_TRAMPOLINE(dgemm)
    MW_BLAS_TRAMPOLINE(dgemv)
    MW_BLAS_TRAMPOLINE(daxpy)
    MW_BLAS_TRAMPOLINE(dcopy)
    MW_BLAS_TRAMPOLINE(dscal)
    MW_BLAS_TRAMPOLINE(ddot)
    MW_BLAS_TRAMPOLINE(dasum)
);

#endif
