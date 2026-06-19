/* struct_fieldnum.c
 * MEX test: reads a 1x1 struct input via mxGetFieldNumber / mxGetFieldByNumber,
 * then returns the value of field "y" as the output.
 *
 * The calling test builds a struct with fields "x" and "y"; this MEX looks up
 * "y" by number (not by name) to exercise the by-number accessor path.
 */
#include <stddef.h>

typedef struct mxArray_tag *mxArray;

extern int     mxGetFieldNumber(const mxArray pa, const char *name);
extern mxArray mxGetFieldByNumber(const mxArray pa, size_t index, int fieldnum);
extern mxArray mxDuplicateArray(mxArray pa);
extern void    mexErrMsgIdAndTxt(const char *id, const char *msg, ...);

void mexFunction(int nlhs, mxArray *plhs, int nrhs, mxArray *prhs) {
    (void)nlhs;
    if (nrhs != 1)
        mexErrMsgIdAndTxt("Unmex:struct_fieldnum:nargin", "Expected 1 input");

    int fn = mxGetFieldNumber(prhs[0], "y");
    if (fn < 0)
        mexErrMsgIdAndTxt("Unmex:struct_fieldnum:nofield",
                          "Input struct has no field 'y'");

    mxArray val = mxGetFieldByNumber(prhs[0], 0, fn);
    if (!val)
        mexErrMsgIdAndTxt("Unmex:struct_fieldnum:nullfield",
                          "Field 'y' is NULL");

    /* Return a copy (field is owned by the struct, not the MEX) */
    plhs[0] = mxDuplicateArray(val);
}
