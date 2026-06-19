/* arraytostring.c
 * MEX test: takes a MATLAB char array as input, calls mxArrayToString, and returns
 * its byte-length as a 1x1 double scalar.
 *
 * Verifies that mxArrayToString correctly converts uint16 char data to a narrow
 * char* string and that the caller can mxFree the result.
 */
#include <stddef.h>
#include <string.h>

typedef struct mxArray_tag *mxArray;

extern mxArray mxCreateDoubleScalar(double v);
extern char   *mxArrayToString(const mxArray pa);
extern void    mxFree(void *ptr);
extern void    mexErrMsgIdAndTxt(const char *id, const char *msg, ...);

void mexFunction(int nlhs, mxArray *plhs, int nrhs, mxArray *prhs) {
    (void)nlhs;
    if (nrhs != 1)
        mexErrMsgIdAndTxt("Unmex:arraytostring:nargin", "Expected 1 input");

    char *str = mxArrayToString(prhs[0]);
    if (!str)
        mexErrMsgIdAndTxt("Unmex:arraytostring:failed",
                          "mxArrayToString returned NULL");

    double len = (double)strlen(str);
    mxFree(str);

    plhs[0] = mxCreateDoubleScalar(len);
}
