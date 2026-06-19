/* Tiny MEX-ABI test function: plhs[0] = 2 .* prhs[0] for a real double matrix.
 * Declares only the bare mx* prototypes it uses (no MATLAB headers) — the symbols
 * resolve from the Unmex host libmx at load time, exactly like a real MEX. */
#include <stddef.h>

typedef struct mxArray_tag *mxArray; /* opaque to the MEX; only pointers are used */

extern mxArray mxCreateDoubleMatrix(size_t m, size_t n, int complex_flag);
extern double *mxGetPr(const mxArray pa);
extern size_t  mxGetM(const mxArray pa);
extern size_t  mxGetN(const mxArray pa);
extern void    mexErrMsgIdAndTxt(const char *id, const char *msg, ...);

void mexFunction(int nlhs, mxArray *plhs, int nrhs, mxArray *prhs) {
    (void)nlhs;
    if (nrhs != 1) {
        mexErrMsgIdAndTxt("Unmex:double_it:nargin", "double_it expects exactly 1 input");
        return;
    }
    size_t m = mxGetM(prhs[0]);
    size_t n = mxGetN(prhs[0]);
    const double *x = mxGetPr(prhs[0]);
    plhs[0] = mxCreateDoubleMatrix(m, n, 0);
    double *y = mxGetPr(plhs[0]);
    for (size_t i = 0; i < m * n; i++) y[i] = 2.0 * x[i];
}
