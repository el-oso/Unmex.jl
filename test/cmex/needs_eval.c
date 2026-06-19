/* Calls mexEvalString — interpreter-only. With the host stub it links + loads,
 * then raises a catchable error when called. */
#include <stddef.h>
typedef struct mxArray_tag *mxArray;
extern int mexEvalString(const char *cmd);
void mexFunction(int nlhs, mxArray *plhs, int nrhs, mxArray *prhs) {
    (void)nlhs; (void)plhs; (void)nrhs; (void)prhs;
    mexEvalString("x = 1;");
}
