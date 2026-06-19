/*
 * libmx_stub.c — Minimal libmx/libmex stub for testing Mexicah marshalers
 * without a MATLAB installation.
 *
 * When preloaded with RTLD_GLOBAL on Linux, bare ccall(:mxFoo, ...) resolves
 * to these implementations, enabling the :matlab-tagged test items to run
 * against the stub (no MATLAB license required).
 *
 * Only Linux bare names are needed: @mxccall730 on Linux still expands to bare
 * names (the _730 suffix is Windows-only via _ccall_with_lib win_suffix="").
 *
 * The stub is intentionally simple: it allocates heap memory, stores metadata
 * in a flat struct, and returns raw pointers that the marshalers read/write.
 * It is NOT thread-safe and does NOT implement the full MATLAB C API — only
 * the ~50 functions exercised by marshaling.jl.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ── mxClassID constants (match MATLAB's enum) ──────────────────────────── */
#define MX_UNKNOWN_CLASS   0
#define MX_CELL_CLASS      1
#define MX_STRUCT_CLASS    2
#define MX_LOGICAL_CLASS   3
#define MX_CHAR_CLASS      4
#define MX_DOUBLE_CLASS    6
#define MX_SINGLE_CLASS    7
#define MX_INT8_CLASS      8
#define MX_UINT8_CLASS     9
#define MX_INT16_CLASS     10
#define MX_UINT16_CLASS    11
#define MX_INT32_CLASS     12
#define MX_UINT32_CLASS    13
#define MX_INT64_CLASS     14
#define MX_UINT64_CLASS    15
/* Private marker for MATLAB's opaque `string` array (no real mxClassID exists).
 * The stub fakes string<->cellstr conversion in mexCallMATLAB over this class. */
#define MX_STRING_CLASS    19

/* ── Complexity flags ────────────────────────────────────────────────────── */
#define MX_REAL    0
#define MX_COMPLEX 1

/* ── Internal mxArray representation ────────────────────────────────────── */
typedef struct _mx_stub {
    int classid;
    int is_complex;
    int is_sparse;
    /* dimensions */
    size_t m, n;       /* 2-D extents (also used for 1-D: m=nelems, n=1) */
    size_t ndim;
    size_t *dims;      /* copy of the full dims array */
    size_t nelems;     /* product of dims */
    /* numeric / logical / char data */
    void *pr;          /* real data (or char bytes) */
    void *pi;          /* imaginary data (NULL if real) */
    /* sparse indices */
    size_t nzmax;
    size_t *ir;        /* row indices, 0-based */
    size_t *jc;        /* column pointers, 0-based */
    /* struct fields */
    int nfields;
    char **fieldnames; /* nfields null-terminated strings */
    struct _mx_stub **fields; /* [nfields × nelems] row-major: fields[f*nelems+e] */
    /* cell elements */
    struct _mx_stub **cells;  /* [nelems] */
} mx_stub_t;

typedef mx_stub_t *mxArray;

/* ── Helpers ─────────────────────────────────────────────────────────────── */
static size_t element_size(int classid) {
    switch (classid) {
        case MX_LOGICAL_CLASS: return 1;
        case MX_CHAR_CLASS:    return 2;  /* mxChar = uint16_t */
        case MX_INT8_CLASS:    return 1;
        case MX_UINT8_CLASS:   return 1;
        case MX_INT16_CLASS:   return 2;
        case MX_UINT16_CLASS:  return 2;
        case MX_INT32_CLASS:   return 4;
        case MX_UINT32_CLASS:  return 4;
        case MX_SINGLE_CLASS:  return 4;
        case MX_INT64_CLASS:   return 8;
        case MX_UINT64_CLASS:  return 8;
        case MX_DOUBLE_CLASS:  return 8;
        default:               return 8;
    }
}

/* ── Live-array accounting (temporary-cleanup discipline guard) ───────────── */
/* Counts live mx_stub_t arrays (every birth via alloc_stub/deep_copy increments;
 * every free in mxDestroyArray decrements). NOTE: this stub deliberately does NOT
 * emulate MATLAB's behaviour of auto-freeing temporary mxArrays when a MEX-function
 * returns — so it is STRICTER than real MATLAB. The test that samples
 * mx_stub_live_count() therefore guards the marshalers' explicit-temporary-cleanup
 * discipline (it flags a marshaler that orphans a temporary without destroying it);
 * it is NOT a real-MATLAB leak detector — in MATLAB those temporaries are reclaimed
 * at return. Children attached via mxSetField/mxSetCell are counted individually, so
 * a fully-destroyed parent nets back to the baseline. */
static long g_mx_live = 0;

long mx_stub_live_count(void) { return g_mx_live; }
void mx_stub_reset_count(void) { g_mx_live = 0; }

static mx_stub_t *alloc_stub(void) {
    mx_stub_t *p = (mx_stub_t *)calloc(1, sizeof(mx_stub_t));
    if (!p) { perror("libmx_stub: calloc"); abort(); }
    g_mx_live++;
    return p;
}

static size_t prod(size_t ndim, const size_t *dims) {
    size_t n = 1;
    for (size_t i = 0; i < ndim; i++) n *= dims[i];
    return n;
}

static mx_stub_t *make_numeric(size_t m, size_t n, int classid, int complex) {
    mx_stub_t *p = alloc_stub();
    p->classid    = classid;
    p->is_complex = complex;
    p->m          = m;
    p->n          = n;
    p->ndim       = 2;
    p->dims       = (size_t *)malloc(2 * sizeof(size_t));
    p->dims[0]    = m;
    p->dims[1]    = n;
    p->nelems     = m * n;
    size_t esz    = element_size(classid);
    p->pr         = calloc(p->nelems, esz);
    if (complex)
        p->pi     = calloc(p->nelems, esz);
    return p;
}

/* ── Creation ────────────────────────────────────────────────────────────── */

mxArray mxCreateDoubleMatrix(size_t m, size_t n, int complex_flag) {
    return make_numeric(m, n, MX_DOUBLE_CLASS, complex_flag);
}

mxArray mxCreateDoubleScalar(double v) {
    mx_stub_t *p = make_numeric(1, 1, MX_DOUBLE_CLASS, 0);
    *(double *)p->pr = v;
    return p;
}

mxArray mxCreateNumericMatrix(size_t m, size_t n, int classid, int complex_flag) {
    return make_numeric(m, n, classid, complex_flag);
}

mxArray mxCreateNumericArray(size_t ndim, const size_t *dims, int classid, int complex_flag) {
    mx_stub_t *p = alloc_stub();
    p->classid    = classid;
    p->is_complex = complex_flag;
    p->ndim       = ndim;
    p->dims       = (size_t *)malloc(ndim * sizeof(size_t));
    memcpy(p->dims, dims, ndim * sizeof(size_t));
    p->m          = ndim >= 1 ? dims[0] : 1;
    p->n          = ndim >= 2 ? dims[1] : 1;
    p->nelems     = prod(ndim, dims);
    size_t esz    = element_size(classid);
    p->pr         = calloc(p->nelems, esz);
    if (complex_flag)
        p->pi     = calloc(p->nelems, esz);
    return p;
}

mxArray mxCreateLogicalMatrix(size_t m, size_t n) {
    return make_numeric(m, n, MX_LOGICAL_CLASS, 0);
}

/* 2-D logical alias used by LogicalArrayMarshaler for rank-1/2 */
mxArray mxCreateLogicalArray(size_t ndim, const size_t *dims) {
    return mxCreateNumericArray(ndim, dims, MX_LOGICAL_CLASS, 0);
}

mxArray mxCreateSparse(size_t m, size_t n, size_t nzmax, int complex_flag) {
    mx_stub_t *p = alloc_stub();
    p->classid    = MX_DOUBLE_CLASS;
    p->is_sparse  = 1;
    p->is_complex = complex_flag;
    p->m          = m;
    p->n          = n;
    p->ndim       = 2;
    p->dims       = (size_t *)malloc(2 * sizeof(size_t));
    p->dims[0]    = m; p->dims[1] = n;
    p->nelems     = m * n;
    p->nzmax      = nzmax > 0 ? nzmax : 1;
    p->ir         = (size_t *)calloc(p->nzmax, sizeof(size_t));
    p->jc         = (size_t *)calloc(n + 1,    sizeof(size_t));
    p->pr         = calloc(p->nzmax, sizeof(double));
    if (complex_flag)
        p->pi     = calloc(p->nzmax, sizeof(double));
    return p;
}

mxArray mxCreateSparseLogicalMatrix(size_t m, size_t n, size_t nzmax) {
    mx_stub_t *p = alloc_stub();
    p->classid    = MX_LOGICAL_CLASS;
    p->is_sparse  = 1;
    p->m          = m;
    p->n          = n;
    p->ndim       = 2;
    p->dims       = (size_t *)malloc(2 * sizeof(size_t));
    p->dims[0]    = m; p->dims[1] = n;
    p->nelems     = m * n;
    p->nzmax      = nzmax > 0 ? nzmax : 1;
    p->ir         = (size_t *)calloc(p->nzmax, sizeof(size_t));
    p->jc         = (size_t *)calloc(n + 1,    sizeof(size_t));
    p->pr         = calloc(p->nzmax, 1);   /* mxLogical = uint8 */
    return p;
}

mxArray mxCreateStructMatrix(size_t m, size_t n, int nfields, const char **fieldnames) {
    mx_stub_t *p = alloc_stub();
    p->classid   = MX_STRUCT_CLASS;
    p->m         = m;
    p->n         = n;
    p->ndim      = 2;
    p->dims      = (size_t *)malloc(2 * sizeof(size_t));
    p->dims[0]   = m; p->dims[1] = n;
    p->nelems    = m * n;
    p->nfields   = nfields;
    p->fieldnames = (char **)malloc(nfields * sizeof(char *));
    for (int i = 0; i < nfields; i++)
        p->fieldnames[i] = strdup(fieldnames[i]);
    p->fields = (mx_stub_t **)calloc((size_t)nfields * p->nelems, sizeof(mx_stub_t *));
    return p;
}

/* N-D struct array; field storage stays [nfields × nelems], same as the 2-D form. */
mxArray mxCreateStructArray(size_t ndim, const size_t *dims, int nfields, const char **fieldnames) {
    mx_stub_t *p = alloc_stub();
    p->classid   = MX_STRUCT_CLASS;
    p->ndim      = ndim;
    p->dims      = (size_t *)malloc(ndim * sizeof(size_t));
    memcpy(p->dims, dims, ndim * sizeof(size_t));
    p->m         = ndim >= 1 ? dims[0] : 1;
    p->n         = ndim >= 2 ? dims[1] : 1;
    p->nelems    = prod(ndim, dims);
    p->nfields   = nfields;
    p->fieldnames = (char **)malloc(nfields * sizeof(char *));
    for (int i = 0; i < nfields; i++)
        p->fieldnames[i] = strdup(fieldnames[i]);
    p->fields = (mx_stub_t **)calloc((size_t)nfields * p->nelems, sizeof(mx_stub_t *));
    return p;
}

mxArray mxCreateCellMatrix(size_t m, size_t n) {
    mx_stub_t *p = alloc_stub();
    p->classid   = MX_CELL_CLASS;
    p->m         = m;
    p->n         = n;
    p->ndim      = 2;
    p->dims      = (size_t *)malloc(2 * sizeof(size_t));
    p->dims[0]   = m; p->dims[1] = n;
    p->nelems    = m * n;
    p->cells     = (mx_stub_t **)calloc(p->nelems, sizeof(mx_stub_t *));
    return p;
}

/* M×N char array; elements are uint16_t (mxChar) stored column-major in pr. */
mxArray mxCreateCharArray(size_t ndim, const size_t *dims) {
    mx_stub_t *p = alloc_stub();
    p->classid = MX_CHAR_CLASS;
    p->ndim    = ndim;
    p->dims    = (size_t *)malloc(ndim * sizeof(size_t));
    memcpy(p->dims, dims, ndim * sizeof(size_t));
    p->m       = ndim >= 1 ? dims[0] : 1;
    p->n       = ndim >= 2 ? dims[1] : 1;
    p->nelems  = prod(ndim, dims);
    p->pr      = calloc(p->nelems, 2);  /* uint16_t per element */
    return p;
}

uint16_t *mxGetChars(mxArray pa) {
    return (uint16_t *)pa->pr;
}

/* Char data is stored as uint16_t (mxChar), consistent with element_size and
 * mxCreateCharArray/mxGetChars — so deep_copy (uses element_size) is byte-correct. */
mxArray mxCreateString(const char *str) {
    size_t len = str ? strlen(str) : 0;
    mx_stub_t *p = alloc_stub();
    p->classid = MX_CHAR_CLASS;
    p->m       = 1;
    p->n       = len;
    p->ndim    = 2;
    p->dims    = (size_t *)malloc(2 * sizeof(size_t));
    p->dims[0] = 1; p->dims[1] = len;
    p->nelems  = len;
    p->pr      = calloc(len ? len : 1, sizeof(uint16_t));
    uint16_t *d = (uint16_t *)p->pr;
    for (size_t i = 0; i < len; i++) d[i] = (uint16_t)(unsigned char)str[i];
    return p;
}

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

void mxDestroyArray(mxArray pa) {
    if (!pa) return;
    free(pa->pr);
    free(pa->pi);
    free(pa->ir);
    free(pa->jc);
    free(pa->dims);
    for (int i = 0; i < pa->nfields; i++) free(pa->fieldnames[i]);
    free(pa->fieldnames);
    if (pa->fields) {
        size_t total = (size_t)pa->nfields * pa->nelems;
        for (size_t i = 0; i < total; i++) mxDestroyArray(pa->fields[i]);
        free(pa->fields);
    }
    if (pa->cells) {
        for (size_t i = 0; i < pa->nelems; i++) mxDestroyArray(pa->cells[i]);
        free(pa->cells);
    }
    free(pa);
    g_mx_live--;
}

static mx_stub_t *deep_copy(const mx_stub_t *src);

mxArray mxDuplicateArray(mxArray pa) {
    return pa ? deep_copy(pa) : NULL;
}

static mx_stub_t *deep_copy(const mx_stub_t *src) {
    mx_stub_t *dst = (mx_stub_t *)malloc(sizeof(mx_stub_t));
    g_mx_live++;   /* a fresh live array, balanced by its own mxDestroyArray */
    *dst = *src;
    if (src->dims) {
        dst->dims = (size_t *)malloc(src->ndim * sizeof(size_t));
        memcpy(dst->dims, src->dims, src->ndim * sizeof(size_t));
    }
    size_t esz = element_size(src->classid);
    if (src->pr) {
        size_t nb = src->is_sparse ? src->nzmax * esz : src->nelems * esz;
        dst->pr = malloc(nb);
        memcpy(dst->pr, src->pr, nb);
    }
    if (src->pi) {
        size_t nb = src->is_sparse ? src->nzmax * esz : src->nelems * esz;
        dst->pi = malloc(nb);
        memcpy(dst->pi, src->pi, nb);
    }
    if (src->ir) {
        dst->ir = (size_t *)malloc(src->nzmax * sizeof(size_t));
        memcpy(dst->ir, src->ir, src->nzmax * sizeof(size_t));
    }
    if (src->jc) {
        dst->jc = (size_t *)malloc((src->n + 1) * sizeof(size_t));
        memcpy(dst->jc, src->jc, (src->n + 1) * sizeof(size_t));
    }
    if (src->fieldnames) {
        dst->fieldnames = (char **)malloc(src->nfields * sizeof(char *));
        for (int i = 0; i < src->nfields; i++)
            dst->fieldnames[i] = strdup(src->fieldnames[i]);
    }
    if (src->fields) {
        size_t total = (size_t)src->nfields * src->nelems;
        dst->fields = (mx_stub_t **)calloc(total, sizeof(mx_stub_t *));
        for (size_t i = 0; i < total; i++)
            if (src->fields[i]) dst->fields[i] = deep_copy(src->fields[i]);
    }
    if (src->cells) {
        dst->cells = (mx_stub_t **)calloc(src->nelems, sizeof(mx_stub_t *));
        for (size_t i = 0; i < src->nelems; i++)
            if (src->cells[i]) dst->cells[i] = deep_copy(src->cells[i]);
    }
    return dst;
}

/* ── Dimension accessors ─────────────────────────────────────────────────── */

size_t mxGetM(const mxArray pa) { return pa->m; }
size_t mxGetN(const mxArray pa) { return pa->n; }
size_t mxGetNumberOfElements(const mxArray pa) { return pa->nelems; }
size_t mxGetNumberOfDimensions(const mxArray pa) { return pa->ndim; }
const size_t *mxGetDimensions(const mxArray pa) { return pa->dims; }

/* ── Type queries ────────────────────────────────────────────────────────── */

int mxGetClassID(const mxArray pa)  { return pa->classid; }
int mxIsDouble(const mxArray pa)    { return pa->classid == MX_DOUBLE_CLASS  && !pa->is_sparse; }
int mxIsSingle(const mxArray pa)    { return pa->classid == MX_SINGLE_CLASS; }
int mxIsInt8(const mxArray pa)      { return pa->classid == MX_INT8_CLASS;   }
int mxIsInt16(const mxArray pa)     { return pa->classid == MX_INT16_CLASS;  }
int mxIsInt32(const mxArray pa)     { return pa->classid == MX_INT32_CLASS;  }
int mxIsInt64(const mxArray pa)     { return pa->classid == MX_INT64_CLASS;  }
int mxIsUint8(const mxArray pa)     { return pa->classid == MX_UINT8_CLASS;  }
int mxIsUint16(const mxArray pa)    { return pa->classid == MX_UINT16_CLASS; }
int mxIsUint32(const mxArray pa)    { return pa->classid == MX_UINT32_CLASS; }
int mxIsUint64(const mxArray pa)    { return pa->classid == MX_UINT64_CLASS; }
int mxIsLogical(const mxArray pa)   { return pa->classid == MX_LOGICAL_CLASS; }
int mxIsComplex(const mxArray pa)   { return pa->is_complex; }
int mxIsSparse(const mxArray pa)    { return pa->is_sparse; }
int mxIsNumeric(const mxArray pa)   { return pa->classid >= MX_DOUBLE_CLASS; }
int mxIsStruct(const mxArray pa)    { return pa->classid == MX_STRUCT_CLASS; }
int mxIsChar(const mxArray pa)      { return pa->classid == MX_CHAR_CLASS;   }
int mxIsCell(const mxArray pa)      { return pa->classid == MX_CELL_CLASS;   }

/* ── Data accessors ──────────────────────────────────────────────────────── */

double *mxGetPr(const mxArray pa)       { return (double *)pa->pr; }
double *mxGetPi(const mxArray pa)       { return (double *)pa->pi; }
/* Faithful to MATLAB: read the first element per its class and return as double
 * (NOT a raw double reinterpret — int/logical/char data is not double-encoded). */
double mxGetScalar(const mxArray pa) {
    if (!pa || !pa->pr) return 0.0;
    switch (pa->classid) {
        case MX_DOUBLE_CLASS:  return *(double *)pa->pr;
        case MX_SINGLE_CLASS:  return (double)*(float *)pa->pr;
        case MX_INT8_CLASS:    return (double)*(int8_t *)pa->pr;
        case MX_UINT8_CLASS:   return (double)*(uint8_t *)pa->pr;
        case MX_INT16_CLASS:   return (double)*(int16_t *)pa->pr;
        case MX_UINT16_CLASS:  return (double)*(uint16_t *)pa->pr;
        case MX_INT32_CLASS:   return (double)*(int32_t *)pa->pr;
        case MX_UINT32_CLASS:  return (double)*(uint32_t *)pa->pr;
        case MX_INT64_CLASS:   return (double)*(int64_t *)pa->pr;
        case MX_UINT64_CLASS:  return (double)*(uint64_t *)pa->pr;
        case MX_LOGICAL_CLASS: return (double)*(uint8_t *)pa->pr;
        case MX_CHAR_CLASS:    return (double)*(uint16_t *)pa->pr;
        default:               return *(double *)pa->pr;
    }
}
void   *mxGetData(const mxArray pa)     { return pa->pr; }
void   *mxGetImagData(const mxArray pa) { return pa->pi; }
unsigned char *mxGetLogicals(const mxArray pa) { return (unsigned char *)pa->pr; }

/* These are aliases required by mxGetComplexDoubles in api.jl (unused in
   split-Pr/Pi marshalers, but the symbol must exist for dlopen). */
double *mxGetComplexDoubles(const mxArray pa) { return (double *)pa->pr; }

/* ── Sparse accessors ────────────────────────────────────────────────────── */

size_t  mxGetNzmax(const mxArray pa)    { return pa->nzmax; }
size_t *mxGetIr(const mxArray pa)       { return pa->ir; }
size_t *mxGetJc(const mxArray pa)       { return pa->jc; }

/* ── String accessor ─────────────────────────────────────────────────────── */

int mxGetString(const mxArray pa, char *buf, size_t buflen) {
    if (!pa || !buf || buflen == 0) return 1;
    size_t len = pa->nelems;
    if (len >= buflen) len = buflen - 1;
    const uint16_t *s = (const uint16_t *)pa->pr;  /* mxChar (uint16) → narrow to char */
    for (size_t i = 0; i < len; i++) buf[i] = s ? (char)s[i] : '\0';
    buf[len] = '\0';
    return 0;
}

/* ── Struct accessors ────────────────────────────────────────────────────── */

int mxGetNumberOfFields(const mxArray pa) { return pa->nfields; }

const char *mxGetFieldNameByNumber(const mxArray pa, int n) {
    if (n < 0 || n >= pa->nfields) return NULL;
    return pa->fieldnames[n];
}

mxArray mxGetField(const mxArray pa, size_t index, const char *fieldname) {
    for (int f = 0; f < pa->nfields; f++) {
        if (strcmp(pa->fieldnames[f], fieldname) == 0)
            return pa->fields[(size_t)f * pa->nelems + index];
    }
    return NULL;
}

void mxSetField(mxArray pa, size_t index, const char *fieldname, mxArray value) {
    for (int f = 0; f < pa->nfields; f++) {
        if (strcmp(pa->fieldnames[f], fieldname) == 0) {
            mxDestroyArray(pa->fields[(size_t)f * pa->nelems + index]);
            pa->fields[(size_t)f * pa->nelems + index] = value;
            return;
        }
    }
}

int mxAddField(mxArray pa, const char *fieldname) {
    int idx = pa->nfields;
    pa->nfields++;
    pa->fieldnames = (char **)realloc(pa->fieldnames, (size_t)pa->nfields * sizeof(char *));
    pa->fieldnames[idx] = strdup(fieldname);
    size_t new_total = (size_t)pa->nfields * pa->nelems;
    pa->fields = (mx_stub_t **)realloc(pa->fields, new_total * sizeof(mx_stub_t *));
    for (size_t e = 0; e < pa->nelems; e++)
        pa->fields[(size_t)idx * pa->nelems + e] = NULL;
    return idx;
}

/* ── Cell accessors ──────────────────────────────────────────────────────── */

mxArray mxGetCell(const mxArray pa, size_t index) {
    if (!pa->cells || index >= pa->nelems) return NULL;
    return pa->cells[index];
}

void mxSetCell(mxArray pa, size_t index, mxArray value) {
    if (!pa->cells || index >= pa->nelems) return;
    mxDestroyArray(pa->cells[index]);
    pa->cells[index] = value;
}

/* ── MEX callback (only the string<->cellstr bridge Mexicah uses) ─────────── */

/* Fakes MATLAB's string()/cellstr() converters so the StringArrayMarshaler round
 * trip runs without real MATLAB. A `string` array is modeled as MX_STRING_CLASS
 * wrapping the same `cells` (char arrays) as a cell-of-char; both directions copy
 * shape and deep-copy the elements (source is not mutated). Any other function
 * name returns nonzero (mimicking a MATLAB error). */
/* mexErrMsgIdAndTxt is defined below (it longjmps to the unmex_call shim); forward-
 * declare it so mexCallMATLAB and the interpreter stubs can raise through it. */
void mexErrMsgIdAndTxt(const char *id, const char *fmt, ...);
static void unmex_need_matlab(const char *fn) {
    mexErrMsgIdAndTxt("Unmex:noInterpreter",
        "%s needs a live MATLAB interpreter, which the Unmex host does not provide.", fn);
}

int mexCallMATLAB(int nlhs, mxArray plhs[], int nrhs, mxArray prhs[], const char *fn) {
    if (nlhs < 1 || nrhs < 1 || !prhs[0] || !fn) return 1;
    int to_string = strcmp(fn, "string") == 0;
    int to_cell   = strcmp(fn, "cellstr") == 0;
    if (!to_string && !to_cell) {
        /* Raise (→ catchable Julia exception) instead of returning 1, so a MEX that
         * ignores the error code and dereferences plhs can't segfault the process. */
        mexErrMsgIdAndTxt("Unmex:mexCallMATLAB:noInterpreter",
            "mexCallMATLAB(\"%s\") needs a live MATLAB interpreter; the Unmex host only "
            "emulates \"string\" and \"cellstr\".", fn);
        return 1; /* unreachable: mexErrMsgIdAndTxt longjmps */
    }
    mx_stub_t *src = prhs[0];
    mx_stub_t *p = alloc_stub();
    p->classid = to_string ? MX_STRING_CLASS : MX_CELL_CLASS;
    p->m       = src->m;
    p->n       = src->n;
    p->ndim    = src->ndim ? src->ndim : 2;
    p->dims    = (size_t *)malloc(p->ndim * sizeof(size_t));
    if (src->dims) memcpy(p->dims, src->dims, src->ndim * sizeof(size_t));
    p->nelems  = src->nelems;
    p->cells   = (mx_stub_t **)calloc(p->nelems ? p->nelems : 1, sizeof(mx_stub_t *));
    for (size_t i = 0; i < p->nelems; i++)
        p->cells[i] = (src->cells && src->cells[i]) ? deep_copy(src->cells[i]) : NULL;
    plhs[0] = p;
    return 0;
}

/* ── MEX error → setjmp/longjmp so a caller (Unmex) turns it into an exception ──
 * mexErrMsgIdAndTxt never returns in MATLAB; here it longjmps to the jmp_buf the
 * caller armed in unmex_call(). Without an armed handler it falls back to abort()
 * (a MEX must never resume after an error). Thread-local so concurrent calls on
 * different threads don't clobber each other. */
#include <setjmp.h>

static __thread jmp_buf g_unmex_jmp;
static __thread int     g_unmex_armed = 0;
static __thread char    g_unmex_errid[256];
static __thread char    g_unmex_errmsg[1024];

void mexErrMsgIdAndTxt(const char *id, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_unmex_errmsg, sizeof g_unmex_errmsg, fmt ? fmt : "", ap);
    va_end(ap);
    strncpy(g_unmex_errid, id ? id : "", sizeof g_unmex_errid - 1);
    g_unmex_errid[sizeof g_unmex_errid - 1] = '\0';
    if (g_unmex_armed) {
        g_unmex_armed = 0;
        longjmp(g_unmex_jmp, 1);
    }
    fprintf(stderr, "mexErrMsgIdAndTxt (no Unmex handler): [%s] %s\n",
            g_unmex_errid, g_unmex_errmsg);
    abort();
}

void mexErrMsgTxt(const char *msg) { mexErrMsgIdAndTxt("MATLAB:error", "%s", msg ? msg : ""); }

/* The entry Unmex ccalls instead of the raw mexFunction pointer: arms the handler,
 * runs the MEX, and reports a raised error (id/msg) via out-params + return 1. */
typedef void (*mexfn_t)(int, void **, int, void **);

int unmex_call(mexfn_t fn, int nlhs, void **plhs, int nrhs, void **prhs,
               char *errid, size_t errid_n, char *errmsg, size_t errmsg_n) {
    g_unmex_armed = 1;
    if (setjmp(g_unmex_jmp)) {
        if (errid && errid_n)   { strncpy(errid, g_unmex_errid, errid_n - 1);   errid[errid_n - 1] = '\0'; }
        if (errmsg && errmsg_n) { strncpy(errmsg, g_unmex_errmsg, errmsg_n - 1); errmsg[errmsg_n - 1] = '\0'; }
        return 1;
    }
    fn(nlhs, plhs, nrhs, prhs);
    g_unmex_armed = 0;
    return 0;
}

/* mexPrintf — writes to stdout in the stub (the banner/logo path). Varargs like
 * the real MATLAB symbol; the marshaler always passes a "%s" format. */
int mexPrintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vprintf(fmt, ap);
    va_end(ap);
    return r;
}

/* ── Interpreter-only entries ──────────────────────────────────────────────────
 * Present so a MEX that *references* them links and loads (dlopen with RTLD_NOW
 * resolves every symbol eagerly), but each raises a clear, catchable error via the
 * setjmp shim when actually called — instead of a cryptic "undefined symbol" at
 * load or a crash. The Unmex host can never fabricate these without a live MATLAB. */
int      mexEvalString(const char *cmd) { (void)cmd; unmex_need_matlab("mexEvalString"); return 1; }
mxArray  mexEvalStringWithTrap(const char *cmd) { (void)cmd; unmex_need_matlab("mexEvalStringWithTrap"); return NULL; }
mxArray  mexGetVariable(const char *ws, const char *name) { (void)ws; (void)name; unmex_need_matlab("mexGetVariable"); return NULL; }
mxArray  mexGetVariablePtr(const char *ws, const char *name) { (void)ws; (void)name; unmex_need_matlab("mexGetVariablePtr"); return NULL; }
int      mexPutVariable(const char *ws, const char *name, const mxArray pm) { (void)ws; (void)name; (void)pm; unmex_need_matlab("mexPutVariable"); return 1; }
mxArray  mexCallMATLABWithTrap(int nlhs, mxArray plhs[], int nrhs, mxArray prhs[], const char *fn) {
    (void)nlhs; (void)plhs; (void)nrhs; (void)prhs; (void)fn; unmex_need_matlab("mexCallMATLABWithTrap"); return NULL;
}
mxArray  mexGet(double h, const char *prop) { (void)h; (void)prop; unmex_need_matlab("mexGet"); return NULL; }
int      mexSet(double h, const char *prop, mxArray v) { (void)h; (void)prop; (void)v; unmex_need_matlab("mexSet"); return 1; }

/* ── Benign entries (no interpreter needed) ───────────────────────────────────
 * Implemented so common MEX link and behave sanely. There is no cross-call
 * persistence or locking in the host, but these never crash. */
void mexWarnMsgIdAndTxt(const char *id, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "[mex warning %s] ", id ? id : "");
    vfprintf(stderr, fmt ? fmt : "", ap);
    fprintf(stderr, "\n");
    va_end(ap);
}
void mexWarnMsgTxt(const char *msg) { fprintf(stderr, "[mex warning] %s\n", msg ? msg : ""); }
void mexLock(void) {}
void mexUnlock(void) {}
int  mexIsLocked(void) { return 0; }
int  mexAtExit(void (*fn)(void)) { (void)fn; return 0; }
const char *mexFunctionName(void) { return "unmex_hosted_mex"; }
void mexMakeArrayPersistent(mxArray pm) { (void)pm; }
void mexMakeMemoryPersistent(void *ptr) { (void)ptr; }
