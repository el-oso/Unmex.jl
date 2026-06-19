/* proxy730.c
 * MEX test: uses _730-suffixed symbol names exclusively, simulating a MATLAB-compiled
 * binary that links against the large-array API.  Takes a 1x1 double scalar input N
 * and returns a 1xN double row vector [1.0, 2.0, ..., N].
 *
 * If the host exports the _730 aliases correctly, this MEX loads (RTLD_NOW resolves
 * every symbol) and runs; otherwise dlopen raises "undefined symbol".
 */
#include <stddef.h>

typedef struct mxArray_tag *mxArray;

/* Only _730-suffixed names are used here -- exactly as MATLAB-compiled MEX link. */
extern mxArray mxCreateDoubleMatrix_730(size_t m, size_t n, int complex_flag);
extern double *mxGetPr(const mxArray pa);
extern double  mxGetScalar(const mxArray pa);
extern size_t  mxGetM(const mxArray pa);
extern size_t  mxGetN(const mxArray pa);
extern void    mexErrMsgIdAndTxt(const char *id, const char *msg, ...);

void mexFunction(int nlhs, mxArray *plhs, int nrhs, mxArray *prhs) {
    (void)nlhs;
    if (nrhs != 1)
        mexErrMsgIdAndTxt("Unmex:proxy730:nargin", "Expected 1 input");

    size_t N = (size_t)mxGetScalar(prhs[0]);
    if (N == 0)
        mexErrMsgIdAndTxt("Unmex:proxy730:badarg", "N must be >= 1");

    /* Use the _730 creator */
    plhs[0] = mxCreateDoubleMatrix_730(1, N, 0);
    double *y = mxGetPr(plhs[0]);
    for (size_t i = 0; i < N; i++) y[i] = (double)(i + 1);
}
