/* proxy_blas.c
 * MEX test: computes C = A*B by calling MATLAB's BLAS `dgemm_` (declared ILP64, as
 * MATLAB MEX do). The symbol resolves from the host's libmwblas.so bridge, which forwards
 * to libblastrampoline's dgemm_64_ (→ OpenBLAS, or MKL after `using MKL`). Validates the
 * BLAS bridge end to end: A (m×k), B (k×n) → C (m×n), column-major.
 */
#include <stddef.h>

typedef struct mxArray_tag *mxArray;
typedef long long f77int; /* ILP64: MATLAB BLAS dimensions are 64-bit */

extern size_t  mxGetM(const mxArray);
extern size_t  mxGetN(const mxArray);
extern double *mxGetPr(const mxArray);
extern mxArray mxCreateDoubleMatrix(size_t m, size_t n, int complex_flag);
extern void    mexErrMsgIdAndTxt(const char *id, const char *msg, ...);

/* Plain ILP64 BLAS name, exactly as a MATLAB MEX links it (provided by libmwblas.so). */
extern void dgemm_(const char *transa, const char *transb,
    const f77int *m, const f77int *n, const f77int *k,
    const double *alpha, const double *a, const f77int *lda,
    const double *b, const f77int *ldb,
    const double *beta, double *c, const f77int *ldc);

void mexFunction(int nlhs, mxArray *plhs, int nrhs, mxArray *prhs) {
    (void)nlhs;
    if (nrhs != 2)
        mexErrMsgIdAndTxt("Unmex:proxy_blas:nargin", "Expected 2 inputs (A, B)");

    f77int m = (f77int)mxGetM(prhs[0]);
    f77int k = (f77int)mxGetN(prhs[0]);
    f77int n = (f77int)mxGetN(prhs[1]);
    if ((f77int)mxGetM(prhs[1]) != k)
        mexErrMsgIdAndTxt("Unmex:proxy_blas:dims", "inner dimensions disagree");

    double *A = mxGetPr(prhs[0]);
    double *B = mxGetPr(prhs[1]);
    plhs[0] = mxCreateDoubleMatrix((size_t)m, (size_t)n, 0);
    double *C = mxGetPr(plhs[0]);

    double one = 1.0, zero = 0.0;
    dgemm_("N", "N", &m, &n, &k, &one, A, &m, B, &k, &zero, C, &m);
}
