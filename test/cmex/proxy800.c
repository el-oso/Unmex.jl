/* proxy800.c
 * MEX test: uses _800-suffixed symbol names exclusively, simulating a modern
 * MATLAB-compiled binary (recent releases version the C API with the _800 suffix —
 * observed across a real MATLAB install's MEX corpus). Takes a 1x1 double scalar N and
 * returns a 1xN double row vector [1.0, 2.0, ..., N].
 *
 * If the host exports the _800 aliases correctly, this MEX loads (RTLD_NOW resolves
 * every symbol) and runs; otherwise dlopen raises "undefined symbol".
 */
#include <stddef.h>

typedef struct mxArray_tag *mxArray;

/* Only _800-suffixed names are used here -- exactly as a modern MATLAB-compiled MEX links. */
extern mxArray mxCreateDoubleMatrix_800(size_t m, size_t n, int complex_flag);
extern double *mxGetPr_800(const mxArray pa);
extern double  mxGetScalar_800(const mxArray pa);
extern void    mexErrMsgIdAndTxt_800(const char *id, const char *msg, ...);

void mexFunction(int nlhs, mxArray *plhs, int nrhs, mxArray *prhs) {
    (void)nlhs;
    if (nrhs != 1)
        mexErrMsgIdAndTxt_800("Unmex:proxy800:nargin", "Expected 1 input");

    size_t N = (size_t)mxGetScalar_800(prhs[0]);
    if (N == 0)
        mexErrMsgIdAndTxt_800("Unmex:proxy800:badarg", "N must be >= 1");

    plhs[0] = mxCreateDoubleMatrix_800(1, N, 0);
    double *y = mxGetPr_800(plhs[0]);
    for (size_t i = 0; i < N; i++) y[i] = (double)(i + 1);
}
