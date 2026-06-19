/* Calls mexCallMATLAB for an unsupported builtin ("sort"). The host raises instead
 * of returning an error code, so an unchecked dereference can't segfault. */
#include <stddef.h>
typedef struct mxArray_tag *mxArray;
extern mxArray mxCreateDoubleScalar(double v);
extern int mexCallMATLAB(int nlhs, mxArray plhs[], int nrhs, mxArray prhs[], const char *fn);
void mexFunction(int nlhs, mxArray *plhs, int nrhs, mxArray *prhs) {
    (void)nlhs; (void)plhs; (void)nrhs; (void)prhs;
    mxArray in[1];  in[0]  = mxCreateDoubleScalar(3.0);
    mxArray out[1]; out[0] = NULL;
    mexCallMATLAB(1, out, 1, in, "sort");
}
