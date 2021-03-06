/* npy_index.c */

#include "npy_index.h"
#include "npy_object.h"
#include "npy_arrayobject.h"
#include "npy_api.h"


NDARRAY_API void
NpyArray_IndexDealloc(NpyIndex*  indexes, int n)
{
    int i;
    NpyIndex *index = indexes;

    for (i=0; i<n; i++) {
        switch(index->type) {
        case NPY_INDEX_INTP_ARRAY:
            Npy_DECREF(index->index.intp_array);
            index->index.intp_array = NULL;
            break;
        case NPY_INDEX_BOOL_ARRAY:
            Npy_DECREF(index->index.bool_array);
            index->index.bool_array = NULL;
            break;
        default:
            break;
        }
        index++;
    }
}


/*
 * Returns the number of non-new indices.  Boolean arrays are
 * counted as if they are expanded.
 */
static int
count_non_new(NpyIndex* indexes, int n)
{
    int i;
    int result = 0;

    for (i=0; i<n; i++) {
        switch (indexes[i].type) {
        case NPY_INDEX_NEWAXIS:
            break;
        case NPY_INDEX_BOOL_ARRAY:
            result += indexes[i].index.bool_array->nd;
            break;
        default:
            result++;
            break;
        }
    }
    return result;
}


/*
 * Expands any boolean arrays in the index into intp arrays of the
 * indexes of the non-zero entries. Also converts boolean to intp.
 */
NDARRAY_API int
NpyArray_IndexExpandBool(NpyIndex *indexes, int n, NpyIndex *out_indexes)
{
    int i;
    int result = 0;

    for (i=0; i<n; i++) {
        switch (indexes[i].type) {
        case NPY_INDEX_BOOL_ARRAY:
            {
                /* Convert to intp array on non-zero indexes. */
                NpyArray *index_arrays[NPY_MAXDIMS];
                NpyArray *bool_array = indexes[i].index.bool_array;
                int j;

                if (NpyArray_NonZero(bool_array, index_arrays, NULL) < 0) {
                    NpyArray_IndexDealloc(out_indexes, result);
                    return -1;
                }
                for (j=0; j<bool_array->nd; j++) {
                    out_indexes[result].type = NPY_INDEX_INTP_ARRAY;
                    out_indexes[result].index.intp_array = index_arrays[j];
                    result++;
                }
            }
            break;
        case NPY_INDEX_INTP_ARRAY:
            out_indexes[result++] = indexes[i];
            Npy_INCREF(indexes[i].index.intp_array);
            break;
        case NPY_INDEX_BOOL:
            out_indexes[result].type = NPY_INDEX_INTP;
            out_indexes[result].index.intp = indexes[i].index.boolean;
            result++;
        default:
            /* Copy anything else. */
            out_indexes[result++] = indexes[i];
            break;
        }
    }

    return result;
}


/*
 * Converts indexes int out_indexes appropriate for an array by:
 *
 * 1. Expanding any ellipses.
 * 2. Setting slice start/stop/step appropriately for the array dims.
 * 3. Handling any negative indexes.
 * 4. Expanding any boolean arrays to intp arrays of non-zero indices.
 * 5. Convert any booleans to intp.
 *
 * Returns the number of indices in out_indexes, or -1 on error.
 */
NDARRAY_API int
NpyArray_IndexBind(NpyIndex* indexes, int n, npy_intp *dimensions, int nd,
                   NpyIndex* out_indexes)
{
    int i;
    int result = 0;
    int n_new = 0;

    for (i=0; i<n; i++) {
        switch (indexes[i].type) {

        case NPY_INDEX_STRING:
            NpyErr_SetString(NpyExc_IndexError,
                             "String index not allowed.");
            return -1;
            break;

        case NPY_INDEX_ELLIPSIS:
            {
                /* Expand the ellipsis. */
                int j, n2;
                n2 = nd + n_new -
                    count_non_new(&indexes[i+1], n-i-1) - result;
                if (n2 < 0) {
                    NpyErr_SetString(NpyExc_IndexError,
                                     "too many indices");
                    NpyArray_IndexDealloc(out_indexes, result);
                    return -1;
                }
                /* Fill with full slices. */
                for (j=0; j<n2; j++) {
                    NpyIndex *out = &out_indexes[result];
                    out->type = NPY_INDEX_SLICE;
                    out->index.slice.start = 0;
                    out->index.slice.stop = dimensions[result-n_new];
                    out->index.slice.step = 1;
                    result++;
                }
            }
            break;

        case NPY_INDEX_BOOL_ARRAY:
            {
                /* Convert to intp array on non-zero indexes. */
                NpyArray *index_arrays[NPY_MAXDIMS];
                NpyArray *bool_array = indexes[i].index.bool_array;
                int j;

                if (result + bool_array->nd >= nd + n_new) {
                    NpyErr_SetString(NpyExc_IndexError,
                                     "too many indices");
                    NpyArray_IndexDealloc(out_indexes, result);
                    return -1;
                }
                if (NpyArray_NonZero(bool_array, index_arrays, NULL) < 0) {
                    NpyArray_IndexDealloc(out_indexes, result);
                    return -1;
                }
                for (j=0; j<bool_array->nd; j++) {
                    out_indexes[result].type = NPY_INDEX_INTP_ARRAY;
                    out_indexes[result].index.intp_array = index_arrays[j];
                    result++;
                }
            }
            break;

        case NPY_INDEX_SLICE:
            {
                /* Sets the slice values based on the array. */
                npy_intp dim;
                NpyIndexSlice *slice;

                if (result >= nd + n_new) {
                    NpyErr_SetString(NpyExc_IndexError,
                                     "too many indices");
                    NpyArray_IndexDealloc(out_indexes, result);
                    return -1;
                }

                dim = dimensions[result-n_new];
                out_indexes[result].type = NPY_INDEX_SLICE;
                out_indexes[result].index.slice = indexes[i].index.slice;
                slice = &out_indexes[result].index.slice;

                if (slice->start < 0) {
                    slice->start += dim;
                }
                if (slice->start < 0) {
                    slice->start = 0;
                    if (slice->step < 0) {
                        slice->start -= 1;
                    }
                }
                if (slice->start >= dim) {
                    slice->start = dim;
                    if (slice->step < 0) {
                        slice->start -= 1;
                    }
                }

                if (slice->stop < 0) {
                    slice->stop += dim;
                }

                if (slice->stop < 0) {
                    slice->stop = -1;
                }
                if (slice->stop > dim) {
                    slice->stop = dim;
                }

                result++;
            }
            break;

        case NPY_INDEX_SLICE_NOSTOP:
            {
                /* Sets the slice values based on the array. */
                npy_intp dim;
                NpyIndexSlice *oslice;
                NpyIndexSliceNoStop *islice;

                if (result >= nd + n_new) {
                    NpyErr_SetString(NpyExc_IndexError,
                                     "too many indices");
                    NpyArray_IndexDealloc(out_indexes, result);
                    return -1;
                }

                dim = dimensions[result-n_new];
                out_indexes[result].type = NPY_INDEX_SLICE;
                oslice = &out_indexes[result].index.slice;
                islice = &indexes[i].index.slice_nostop;

                oslice->step = islice->step;

                if (islice->start < 0) {
                    oslice->start = islice->start + dim;
                }
                else {
                    oslice->start = islice->start;
                }
                if (oslice->start < 0) {
                    oslice->start = 0;
                    if (oslice->step < 0) {
                        oslice->start -= 1;
                    }
                }
                if (oslice->start >= dim) {
                    oslice->start = dim;
                    if (oslice->step < 0) {
                        oslice->start -= 1;
                    }
                }

                if (oslice->step > 0) {
                    oslice->stop = dim;
                } else {
                    oslice->stop = -1;
                }

                result++;
            }
            break;

        case NPY_INDEX_INTP:
        case NPY_INDEX_BOOL:
            {
                npy_intp val, dim;

                if (result >= nd + n_new) {
                    NpyErr_SetString(NpyExc_IndexError,
                                     "too many indices");
                    NpyArray_IndexDealloc(out_indexes, result);
                    return -1;
                }

                if (indexes[i].type == NPY_INDEX_INTP) {
                    val = indexes[i].index.intp;
                } else {
                    val = indexes[i].index.boolean;
                }
                dim = dimensions[result-n_new];

                if (val < 0) {
                    val += dim;
                }
                if (val < 0 || val >= dim) {
                    NpyErr_SetString(NpyExc_IndexError,
                                     "Invalid index.");
                    return -1;
                }

                out_indexes[result].type = NPY_INDEX_INTP;
                out_indexes[result].index.intp = val;
                result++;
            }
            break;


        case NPY_INDEX_INTP_ARRAY:
            if (result >= nd + n_new) {
                NpyErr_SetString(NpyExc_IndexError,
                                 "too many indices");
                NpyArray_IndexDealloc(out_indexes, result);
                return -1;
            }

            out_indexes[result++] = indexes[i];
            Npy_INCREF(indexes[i].index.intp_array);
            break;

        case NPY_INDEX_NEWAXIS:
            n_new++;
            out_indexes[result++] = indexes[i];
            break;

        default:
            /* Copy anything else. */
            out_indexes[result++] = indexes[i];
            break;
        }
    }

    return result;
}


/*
 * Converts a bound index into dimensions, strides, and an offset_ptr.
 */
NDARRAY_API int
NpyArray_IndexToDimsEtc(NpyArray* array, NpyIndex* indexes, int n,
                        npy_intp *dimensions, npy_intp* strides,
                        npy_intp* offset_ptr, npy_bool allow_arrays)
{
    int i;
    int iDim = 0;
    int nd_new = 0;
    npy_intp offset = 0;

    for (i=0; i<n; i++) {
        switch (indexes[i].type) {
        case NPY_INDEX_INTP:
            if (iDim >= array->nd) {
                NpyErr_SetString(NpyExc_IndexError,
                                 "too many indices");
                return -1;
            }
            offset += array->strides[iDim] * indexes[i].index.intp;
            iDim++;
            break;
        case NPY_INDEX_SLICE:
            {
                NpyIndexSlice *slice = &indexes[i].index.slice;

                if (iDim >= array->nd) {
                    NpyErr_SetString(NpyExc_IndexError,
                                     "too many indices");
                    return -1;
                }
                dimensions[nd_new] = NpyArray_SliceSteps(slice);
                strides[nd_new] = slice->step * array->strides[iDim];
                offset += array->strides[iDim]*slice->start;
                iDim++;
                nd_new++;
            }
            break;

        case NPY_INDEX_INTP_ARRAY:
            if (allow_arrays) {
                /* Treat arrays as a 0 index to get the subspace. */
                if (iDim >= array->nd) {
                    NpyErr_SetString(NpyExc_IndexError,
                                     "too many indices");
                    return -1;
                }
                iDim++;
                break;
            } else {
                NpyErr_SetString(NpyExc_IndexError,
                                 "Array indices are not allowed.");
                return -1;
            }
            break;

        case NPY_INDEX_NEWAXIS:
            dimensions[nd_new] = 1;
            strides[nd_new] = 0;
            nd_new++;
            break;

        case NPY_INDEX_SLICE_NOSTOP:
        case NPY_INDEX_BOOL_ARRAY:
        case NPY_INDEX_ELLIPSIS:
            NpyErr_SetString(NpyExc_IndexError,
                             "Index is not bound to an array.");
            return -1;
            break;
        case NPY_INDEX_STRING:
            NpyErr_SetString(NpyExc_IndexError,
                             "String indices not allowed.");
            return -1;
            break;

        default:
            assert(NPY_FALSE);
            NpyErr_SetString(NpyExc_IndexError,
                             "Illegal index type.");
            return -1;
        }
    }

    /* Add full slices for the rest of the array indices. */
    for (; iDim<array->nd; iDim++) {
        dimensions[nd_new] = array->dimensions[iDim];
        strides[nd_new] = array->strides[iDim];
        nd_new++;
    }

    *offset_ptr = offset;
    return nd_new;
}


NDARRAY_API npy_intp
NpyArray_SliceSteps(NpyIndexSlice *slice)
{
    if ((slice->step < 0 && slice->stop >= slice->start) ||
        (slice->step > 0 && slice->start >= slice->stop)) {
        return 0;
    } else if (slice->step < 0) {
        return ((slice->stop - slice->start + 1) / slice->step) + 1;
    } else {
        return ((slice->stop - slice->start - 1) / slice->step) + 1;
    }
}
