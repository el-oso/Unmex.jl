/*
 * libmxhost.c - Host libmx/libmex for Unmex: provides the mx- and mex- C symbols
 * a MATLAB MEX file resolves at load time (dlopen with RTLD_NOW), so MEX files can
 * be called from Julia without a MATLAB installation.
 *
 * Phase 1 (host-completeness): includes all common mx functions that real-world
 * MEX use, plus _730 large-array-API aliases that MATLAB-compiled MEX link against.
 *
 * C resource-management convention:
 *   Heap-acquiring temporaries use __attribute__((cleanup(free_fn))) compiler scope
 *   guards where practical. On paths that can raise (via mexErrMsgIdAndTxt/longjmp),
 *   we free explicitly before raising -- cleanup handlers do NOT run through longjmp.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <float.h>

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

/* ── Memory management ───────────────────────────────────────────────────── */

/* Thin wrappers over the system allocator — MEX code that calls mxMalloc/mxFree
 * expects to hand the pointer back through mxFree, not free(), so keep symmetric. */
void *mxMalloc(size_t n)               { return malloc(n); }
void *mxCalloc(size_t n, size_t esz)   { return calloc(n, esz); }
void *mxRealloc(void *ptr, size_t n)   { return realloc(ptr, n); }
void  mxFree(void *ptr)                { free(ptr); }

/* ── Field removal ───────────────────────────────────────────────────────── */

void mxRemoveField(mxArray pa, int fieldnum) {
    if (!pa || fieldnum < 0 || fieldnum >= pa->nfields) return;
    /* Free all values in this column */
    for (size_t e = 0; e < pa->nelems; e++)
        mxDestroyArray(pa->fields[(size_t)fieldnum * pa->nelems + e]);
    /* Free the field name */
    free(pa->fieldnames[fieldnum]);
    /* Shift remaining field names and data columns down */
    int nf = pa->nfields;
    for (int f = fieldnum; f < nf - 1; f++) {
        pa->fieldnames[f] = pa->fieldnames[f + 1];
        for (size_t e = 0; e < pa->nelems; e++)
            pa->fields[(size_t)f * pa->nelems + e] =
                pa->fields[(size_t)(f + 1) * pa->nelems + e];
    }
    pa->nfields--;
    /* Shrink arrays (ignore realloc failure — stub, not production code) */
    if (pa->nfields > 0) {
        pa->fieldnames = (char **)realloc(pa->fieldnames,
                                          (size_t)pa->nfields * sizeof(char *));
        pa->fields = (mx_stub_t **)realloc(
            pa->fields, (size_t)pa->nfields * pa->nelems * sizeof(mx_stub_t *));
    } else {
        free(pa->fieldnames); pa->fieldnames = NULL;
        free(pa->fields);     pa->fields     = NULL;
    }
}

/* ── Introspection ───────────────────────────────────────────────────────── */

size_t mxGetElementSize(const mxArray pa) {
    return pa ? element_size(pa->classid) : 0;
}

const char *mxGetClassName(const mxArray pa) {
    if (!pa) return "unknown";
    switch (pa->classid) {
        case MX_DOUBLE_CLASS:  return "double";
        case MX_SINGLE_CLASS:  return "single";
        case MX_INT8_CLASS:    return "int8";
        case MX_UINT8_CLASS:   return "uint8";
        case MX_INT16_CLASS:   return "int16";
        case MX_UINT16_CLASS:  return "uint16";
        case MX_INT32_CLASS:   return "int32";
        case MX_UINT32_CLASS:  return "uint32";
        case MX_INT64_CLASS:   return "int64";
        case MX_UINT64_CLASS:  return "uint64";
        case MX_LOGICAL_CLASS: return "logical";
        case MX_CHAR_CLASS:    return "char";
        case MX_CELL_CLASS:    return "cell";
        case MX_STRUCT_CLASS:  return "struct";
        case MX_STRING_CLASS:  return "string";
        default:               return "unknown";
    }
}

int mxIsClass(const mxArray pa, const char *classname) {
    return strcmp(mxGetClassName(pa), classname) == 0;
}

int mxIsEmpty(const mxArray pa)        { return !pa || pa->nelems == 0; }
int mxIsScalar(const mxArray pa)       { return pa && pa->nelems == 1; }

/* These always return 0 in the host — no interpreter, no function handle or objects. */
int mxIsFunctionHandle(const mxArray pa) { (void)pa; return 0; }
int mxIsObject(const mxArray pa)         { (void)pa; return 0; }
int mxIsOpaque(const mxArray pa)         { (void)pa; return 0; }

/* IEEE special values — same semantics as MATLAB's mxGetInf()/mxGetNaN()/mxGetEps(). */
double mxGetInf(void) { return INFINITY;    }
double mxGetNaN(void) { return NAN;         }
double mxGetEps(void) { return DBL_EPSILON; }

/* ── String conversion ───────────────────────────────────────────────────── */

/* Return a newly malloc'd null-terminated char* from a char (uint16) array.
 * Caller is responsible for freeing via mxFree (== free). */
char *mxArrayToString(const mxArray pa) {
    if (!pa || pa->classid != MX_CHAR_CLASS) return NULL;
    size_t len = pa->nelems;
    char *buf = (char *)malloc(len + 1);
    if (!buf) return NULL;
    const uint16_t *s = (const uint16_t *)pa->pr;
    for (size_t i = 0; i < len; i++) buf[i] = s ? (char)s[i] : '\0';
    buf[len] = '\0';
    return buf;
}

/* mxArrayToUTF8String — identical for ASCII content; alias behaviour for the host. */
char *mxArrayToUTF8String(const mxArray pa) { return mxArrayToString(pa); }

/* ── Field accessors by number ───────────────────────────────────────────── */

int mxGetFieldNumber(const mxArray pa, const char *name) {
    if (!pa || !name) return -1;
    for (int f = 0; f < pa->nfields; f++)
        if (strcmp(pa->fieldnames[f], name) == 0) return f;
    return -1;
}

mxArray mxGetFieldByNumber(const mxArray pa, size_t index, int fieldnum) {
    if (!pa || fieldnum < 0 || fieldnum >= pa->nfields || index >= pa->nelems)
        return NULL;
    return pa->fields[(size_t)fieldnum * pa->nelems + index];
}

void mxSetFieldByNumber(mxArray pa, size_t index, int fieldnum, mxArray value) {
    if (!pa || fieldnum < 0 || fieldnum >= pa->nfields || index >= pa->nelems)
        return;
    mxDestroyArray(pa->fields[(size_t)fieldnum * pa->nelems + index]);
    pa->fields[(size_t)fieldnum * pa->nelems + index] = value;
}

/* ── Mutators ────────────────────────────────────────────────────────────── */

void mxSetM(mxArray pa, size_t m) {
    if (!pa) return;
    pa->m = m;
    if (pa->ndim >= 1 && pa->dims) pa->dims[0] = m;
    pa->nelems = prod(pa->ndim, pa->dims);
}

void mxSetN(mxArray pa, size_t n) {
    if (!pa) return;
    pa->n = n;
    if (pa->ndim >= 2 && pa->dims) pa->dims[1] = n;
    pa->nelems = prod(pa->ndim, pa->dims);
}

int mxSetDimensions(mxArray pa, const size_t *dims, size_t ndim) {
    if (!pa || !dims) return 1;
    free(pa->dims);
    pa->dims = (size_t *)malloc(ndim * sizeof(size_t));
    if (!pa->dims) return 1;
    memcpy(pa->dims, dims, ndim * sizeof(size_t));
    pa->ndim  = ndim;
    pa->m     = ndim >= 1 ? dims[0] : 1;
    pa->n     = ndim >= 2 ? dims[1] : 1;
    pa->nelems = prod(ndim, dims);
    return 0;
}

/* mxSetData / mxSetPr — replace the real-data pointer (caller manages memory). */
void mxSetData(mxArray pa, void *ptr)      { if (pa) pa->pr = ptr; }
void mxSetPr(mxArray pa, double *ptr)      { if (pa) pa->pr = ptr; }
void mxSetPi(mxArray pa, double *ptr)      { if (pa) pa->pi = ptr; }
void mxSetImagData(mxArray pa, void *ptr)  { if (pa) pa->pi = ptr; }
void mxSetIr(mxArray pa, size_t *ptr)      { if (pa) pa->ir = ptr; }
void mxSetJc(mxArray pa, size_t *ptr)      { if (pa) pa->jc = ptr; }
void mxSetNzmax(mxArray pa, size_t nzmax)  { if (pa) pa->nzmax = nzmax; }

/* mxSetClassName — changes the classid to match the string if recognised. */
int mxSetClassName(mxArray pa, const char *classname) {
    if (!pa || !classname) return 1;
    if      (strcmp(classname, "double")  == 0) pa->classid = MX_DOUBLE_CLASS;
    else if (strcmp(classname, "single")  == 0) pa->classid = MX_SINGLE_CLASS;
    else if (strcmp(classname, "int8")    == 0) pa->classid = MX_INT8_CLASS;
    else if (strcmp(classname, "uint8")   == 0) pa->classid = MX_UINT8_CLASS;
    else if (strcmp(classname, "int16")   == 0) pa->classid = MX_INT16_CLASS;
    else if (strcmp(classname, "uint16")  == 0) pa->classid = MX_UINT16_CLASS;
    else if (strcmp(classname, "int32")   == 0) pa->classid = MX_INT32_CLASS;
    else if (strcmp(classname, "uint32")  == 0) pa->classid = MX_UINT32_CLASS;
    else if (strcmp(classname, "int64")   == 0) pa->classid = MX_INT64_CLASS;
    else if (strcmp(classname, "uint64")  == 0) pa->classid = MX_UINT64_CLASS;
    else if (strcmp(classname, "logical") == 0) pa->classid = MX_LOGICAL_CLASS;
    else if (strcmp(classname, "char")    == 0) pa->classid = MX_CHAR_CLASS;
    else return 1;   /* unknown class name */
    return 0;
}

/* ── Creation gaps ───────────────────────────────────────────────────────── */

/* N-D cell array (mirrors mxCreateStructArray layout). */
mxArray mxCreateCellArray(size_t ndim, const size_t *dims) {
    mx_stub_t *p = alloc_stub();
    p->classid = MX_CELL_CLASS;
    p->ndim    = ndim;
    p->dims    = (size_t *)malloc(ndim * sizeof(size_t));
    memcpy(p->dims, dims, ndim * sizeof(size_t));
    p->m       = ndim >= 1 ? dims[0] : 1;
    p->n       = ndim >= 2 ? dims[1] : 1;
    p->nelems  = prod(ndim, dims);
    p->cells   = (mx_stub_t **)calloc(p->nelems ? p->nelems : 1, sizeof(mx_stub_t *));
    return p;
}

mxArray mxCreateLogicalScalar(int val) {
    mx_stub_t *p = make_numeric(1, 1, MX_LOGICAL_CLASS, 0);
    *(uint8_t *)p->pr = val ? 1 : 0;
    return p;
}

/* Build a 1×N char array from an array of C strings (like MATLAB's char(strs...)). */
mxArray mxCreateCharMatrixFromStrings(size_t m, const char **strs) {
    if (!strs || m == 0) {
        size_t dims[2] = {0, 0};
        return mxCreateCharArray(2, dims);
    }
    /* Find the maximum string length to determine N. */
    size_t maxlen = 0;
    for (size_t i = 0; i < m; i++) {
        size_t l = strs[i] ? strlen(strs[i]) : 0;
        if (l > maxlen) maxlen = l;
    }
    mx_stub_t *p = alloc_stub();
    p->classid = MX_CHAR_CLASS;
    p->ndim    = 2;
    p->dims    = (size_t *)malloc(2 * sizeof(size_t));
    p->dims[0] = m; p->dims[1] = maxlen;
    p->m       = m;
    p->n       = maxlen;
    p->nelems  = m * maxlen;
    size_t nelems_cm = m * maxlen;
    p->pr      = calloc(nelems_cm > 0 ? nelems_cm : 1, sizeof(uint16_t));
    uint16_t *d = (uint16_t *)p->pr;
    /* MATLAB stores char matrices column-major: d[col*m + row] */
    for (size_t r = 0; r < m; r++) {
        const char *s = strs[r] ? strs[r] : "";
        size_t slen   = strlen(s);
        for (size_t c = 0; c < maxlen; c++)
            d[c * m + r] = (c < slen) ? (uint16_t)(unsigned char)s[c] : (uint16_t)' ';
    }
    return p;
}

/* Uninitialised variants — like the numeric creators but skip calloc (use malloc). */
mxArray mxCreateUninitNumericMatrix(size_t m, size_t n, int classid, int complex_flag) {
    mx_stub_t *p = alloc_stub();
    p->classid    = classid;
    p->is_complex = complex_flag;
    p->m          = m;
    p->n          = n;
    p->ndim       = 2;
    p->dims       = (size_t *)malloc(2 * sizeof(size_t));
    p->dims[0]    = m; p->dims[1] = n;
    p->nelems     = m * n;
    size_t esz    = element_size(classid);
    p->pr         = malloc(p->nelems * esz + 1);   /* +1 to avoid 0-byte malloc */
    if (complex_flag)
        p->pi     = malloc(p->nelems * esz + 1);
    return p;
}

mxArray mxCreateUninitNumericArray(size_t ndim, const size_t *dims,
                                    int classid, int complex_flag) {
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
    p->pr         = malloc(p->nelems * esz + 1);
    if (complex_flag)
        p->pi     = malloc(p->nelems * esz + 1);
    return p;
}

/* ── mxCalcSingleSubscript ───────────────────────────────────────────────── */

/* Converts an N-D subscript tuple to a linear 0-based index (column-major). */
size_t mxCalcSingleSubscript(const mxArray pa, size_t nsubs, const size_t *subs) {
    if (!pa || !subs || nsubs == 0) return 0;
    size_t idx = 0, stride = 1;
    for (size_t i = 0; i < nsubs && i < pa->ndim; i++) {
        idx    += subs[i] * stride;
        stride *= pa->dims[i];
    }
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

/* ── _730 large-array-API aliases ────────────────────────────────────────────
 * MATLAB-compiled MEX on 64-bit platforms import symbol names with a _730 suffix
 * (the "large-array API" introduced in MATLAB 7.3). Each alias is declared with the
 * same prototype as its bare counterpart; __attribute__((alias)) makes the linker
 * emit a second export pointing at the same code without duplicating it.
 *
 * GCC/Clang (the compilers used by the Unmex host build) support __attribute__((alias)).
 * The declaration order must come AFTER the definitions above. */

#define _MX_ALIAS(ret, name730, bare, params) \
    ret name730 params __attribute__((alias(#bare)))

/* Creation */
_MX_ALIAS(mxArray, mxCreateNumericArray_730,        mxCreateNumericArray,
          (size_t ndim, const size_t *dims, int classid, int complex_flag));
_MX_ALIAS(mxArray, mxCreateNumericMatrix_730,       mxCreateNumericMatrix,
          (size_t m, size_t n, int classid, int complex_flag));
_MX_ALIAS(mxArray, mxCreateDoubleMatrix_730,        mxCreateDoubleMatrix,
          (size_t m, size_t n, int complex_flag));
_MX_ALIAS(mxArray, mxCreateLogicalArray_730,        mxCreateLogicalArray,
          (size_t ndim, const size_t *dims));
_MX_ALIAS(mxArray, mxCreateCellArray_730,           mxCreateCellArray,
          (size_t ndim, const size_t *dims));
_MX_ALIAS(mxArray, mxCreateCellMatrix_730,          mxCreateCellMatrix,
          (size_t m, size_t n));
_MX_ALIAS(mxArray, mxCreateStructArray_730,         mxCreateStructArray,
          (size_t ndim, const size_t *dims, int nfields, const char **fieldnames));
_MX_ALIAS(mxArray, mxCreateStructMatrix_730,        mxCreateStructMatrix,
          (size_t m, size_t n, int nfields, const char **fieldnames));
_MX_ALIAS(mxArray, mxCreateCharArray_730,           mxCreateCharArray,
          (size_t ndim, const size_t *dims));
_MX_ALIAS(mxArray, mxCreateSparse_730,              mxCreateSparse,
          (size_t m, size_t n, size_t nzmax, int complex_flag));
_MX_ALIAS(mxArray, mxCreateSparseLogicalMatrix_730, mxCreateSparseLogicalMatrix,
          (size_t m, size_t n, size_t nzmax));

/* Dimension accessors */
_MX_ALIAS(const size_t *, mxGetDimensions_730,         mxGetDimensions,
          (const mxArray pa));
_MX_ALIAS(size_t,          mxGetNumberOfDimensions_730, mxGetNumberOfDimensions,
          (const mxArray pa));
_MX_ALIAS(int,             mxSetDimensions_730,         mxSetDimensions,
          (mxArray pa, const size_t *dims, size_t ndim));

/* Cell */
_MX_ALIAS(mxArray, mxGetCell_730, mxGetCell,
          (const mxArray pa, size_t index));
_MX_ALIAS(void,    mxSetCell_730, mxSetCell,
          (mxArray pa, size_t index, mxArray value));

/* Struct */
_MX_ALIAS(mxArray, mxGetField_730,           mxGetField,
          (const mxArray pa, size_t index, const char *fieldname));
_MX_ALIAS(void,    mxSetField_730,           mxSetField,
          (mxArray pa, size_t index, const char *fieldname, mxArray value));
_MX_ALIAS(mxArray, mxGetFieldByNumber_730,   mxGetFieldByNumber,
          (const mxArray pa, size_t index, int fieldnum));
_MX_ALIAS(void,    mxSetFieldByNumber_730,   mxSetFieldByNumber,
          (mxArray pa, size_t index, int fieldnum, mxArray value));

/* Sparse */
_MX_ALIAS(size_t,  mxGetNzmax_730,  mxGetNzmax,
          (const mxArray pa));
_MX_ALIAS(void,    mxSetNzmax_730,  mxSetNzmax,
          (mxArray pa, size_t nzmax));

/* Single-subscript */
_MX_ALIAS(size_t,  mxCalcSingleSubscript_730, mxCalcSingleSubscript,
          (const mxArray pa, size_t nsubs, const size_t *subs));
