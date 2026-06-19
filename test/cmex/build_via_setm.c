/* build_via_setm.c
 * MEX test: uses mxMalloc + mxCreateDoubleMatrix/mxSetData/mxSetM/mxSetN to build
 * and return a modified output.  Input: a 1x1 double scalar N.
 * Output: a 1xN double row vector [1, 2, ..., N].
 *
 * Declares only the bare mx- and mex- prototypes it uses -- no MATLAB headers.
 */
#include <stddef.h>

typedef struct mxArray_tag *mxArray;

extern mxArray mxCreateDoubleMatrix(size_t m, size_t n, int complex_flag);
extern double *mxGetPr(const mxArray pa);
extern double  mxGetScalar(const mxArray pa);
extern void   *mxMalloc(size_t n);
extern void    mxFree(void *ptr);
extern void    mxSetM(mxArray pa, size_t m);
extern void    mxSetN(mxArray pa, size_t n);
extern void    mxSetData(mxArray pa, void *data);
extern void    mexErrMsgIdAndTxt(const char *id, const char *msg, ...);

void mexFunction(int nlhs, mxArray *plhs, int nrhs, mxArray *prhs) {
    (void)nlhs;
    if (nrhs != 1)
        mexErrMsgIdAndTxt("Unmex:build_via_setm:nargin", "Expected 1 input");

    size_t N = (size_t)mxGetScalar(prhs[0]);
    if (N == 0)
        mexErrMsgIdAndTxt("Unmex:build_via_setm:badarg", "N must be >= 1");

    /* Allocate data via mxMalloc; hand it over via mxSetData */
    double *data = (double *)mxMalloc(N * sizeof(double));
    for (size_t i = 0; i < N; i++) data[i] = (double)(i + 1);

    /* Start with a 1x1 matrix, then resize via mxSetM/mxSetN/mxSetData */
    plhs[0] = mxCreateDoubleMatrix(1, 1, 0);
    mxFree(mxGetPr(plhs[0]));  /* release the original 1-element buffer */
    mxSetData(plhs[0], data);   /* install our mxMalloc'd buffer */
    mxSetM(plhs[0], 1);
    mxSetN(plhs[0], N);
}
