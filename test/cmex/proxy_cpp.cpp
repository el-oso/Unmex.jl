/* proxy_cpp.cpp
 * MEX test: uses MATLAB's C++ mxArray API (matrix::detail::noninlined::mx_array_api)
 * exactly as a modern C++-compiled MATLAB MEX does — the mangled C++ symbols our host
 * provides by forwarding to its C functions. Takes a 1x1 double scalar N and returns a
 * 1xN double row vector [1.0, 2.0, ..., N].
 *
 * If the host exports the C++ mx-API symbols, this MEX loads (RTLD_NOW resolves the
 * mangled names) and runs; otherwise dlopen raises "undefined symbol".
 */
#include <cstddef>

struct mxArray_tag;
enum mxComplexity { mxREAL = 0, mxCOMPLEX = 1 };

/* The C++ mxArray API — same namespace/signatures the host exports (mangled). */
namespace matrix { namespace detail { namespace noninlined { namespace mx_array_api {
    mxArray_tag *mxCreateDoubleMatrix(std::size_t m, std::size_t n, mxComplexity c);
    double       mxGetScalar(const mxArray_tag *pa);
}}}}

/* Data pointer + error reporting via the C API (also exported by the host). */
extern "C" double *mxGetPr(const void *pa);
extern "C" void    mexErrMsgIdAndTxt(const char *id, const char *msg, ...);

using namespace matrix::detail::noninlined::mx_array_api;

extern "C" void mexFunction(int nlhs, mxArray_tag **plhs, int nrhs, mxArray_tag **prhs) {
    (void)nlhs;
    if (nrhs != 1)
        mexErrMsgIdAndTxt("Unmex:proxy_cpp:nargin", "Expected 1 input");

    std::size_t N = (std::size_t)mxGetScalar(prhs[0]);
    if (N == 0)
        mexErrMsgIdAndTxt("Unmex:proxy_cpp:badarg", "N must be >= 1");

    plhs[0] = mxCreateDoubleMatrix(1, N, mxREAL);
    double *y = mxGetPr((const void *)plhs[0]);
    for (std::size_t i = 0; i < N; i++) y[i] = (double)(i + 1);
}
