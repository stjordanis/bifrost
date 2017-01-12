/*
 * Copyright (c) 2016, The Bifrost Authors. All rights reserved.
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * * Neither the name of The Bifrost Authors nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <bifrost/common.h>
#include <bifrost/memory.h>
#include <bifrost/array.h>
#include "cuda.hpp"

#include <stdexcept>
#include <cstring> // For ::memcpy

#define BF_DTYPE_IS_COMPLEX(dtype) bool((dtype) & BF_DTYPE_COMPLEX_BIT)
// **TODO: Add support for string type that encodes up to 255 bytes
#define BF_DTYPE_NBIT(dtype) \
	(((dtype) & BF_DTYPE_NBIT_BITS) * (BF_DTYPE_IS_COMPLEX(dtype)+1))
#define BF_DTYPE_NBYTE(dtype) (BF_DTYPE_NBIT(dtype)/8)

// TODO: Check that these wrap/overflow properly
inline BFoffset round_up(BFoffset val, BFoffset mult) {
	return (val == 0 ?
	        0 :
	        ((val-1)/mult+1)*mult);
}
inline BFoffset round_up_pow2(BFoffset a) {
    size_t r = a-1;
    for( int i=1; i<=(int)sizeof(BFoffset)*8/2; i<<=1 ) r |= r >> i;
    return r+1;
}

inline BFbool space_accessible_from(BFspace space, BFspace from) {
#if !defined BF_CUDA_ENABLED || !BF_CUDA_ENABLED
	return space == BF_SPACE_SYSTEM;
#else
	switch( from ) {
	case BF_SPACE_SYSTEM: return (space == BF_SPACE_SYSTEM ||
	                              space == BF_SPACE_CUDA_HOST ||
	                              space == BF_SPACE_CUDA_MANAGED);
	case BF_SPACE_CUDA:   return (space == BF_SPACE_CUDA ||
	                              space == BF_SPACE_CUDA_MANAGED);
	// TODO: Need to use something else here?
	default: throw std::runtime_error("Internal error");
	}
#endif
}

inline int get_dtype_nbyte(BFdtype dtype) {
	int  nbit    = dtype & BF_DTYPE_NBIT_BITS;
	bool complex = dtype & BF_DTYPE_COMPLEX_BIT;
	if( complex ) {
		nbit *= 2;
	}
	//assert(nbit % 8 == 0);
	return nbit / 8;
}

inline bool shapes_equal(const BFarray* a,
                         const BFarray* b) {
	if( a->ndim != b->ndim ) {
		return false;
	}
	for( int d=0; d<a->ndim; ++d ) {
		if( a->shape[d] != b->shape[d] ) {
			return false;
		}
	}
	return true;
}

inline BFsize capacity_bytes(const BFarray* array) {
	return array->strides[0] * array->shape[0];
}
inline bool is_contiguous(const BFarray* array) {
	BFsize logical_size = get_dtype_nbyte(array->dtype);
	for( int d=0; d<array->ndim; ++d ) {
		logical_size *= array->shape[d];
	}
	BFsize physical_size = capacity_bytes(array);
	return logical_size == physical_size;
}
inline BFsize num_contiguous_elements(const BFarray* array ) {
	// Assumes array is contiguous
	return capacity_bytes(array) / BF_DTYPE_NBYTE(array->dtype);
}

// Merges together contiguous dimensions
//   Copies (shallow) 'in' to 'out' and writes new ndim, shape, and strides
inline void squeeze_contiguous_dims(BFarray const* in,
                                    BFarray*       out) {
	::memcpy(out, in, sizeof(BFarray));
	int odim = 0;
	int32_t osize = 1;
	for( int idim=0; idim<in->ndim; ++idim ) {
		osize *= in->shape[idim];
		int32_t logical_stride = in->strides[idim+1]*in->shape[idim+1];
		bool is_padded_dim = (in->strides[idim] != logical_stride);
		bool is_last_dim = (idim == in->ndim-1);
		if( is_last_dim || is_padded_dim ) {
			out->shape[odim]   = osize;
			out->strides[odim] = in->strides[idim];
			osize = 1;
			++odim;
		}
	}
	out->ndim = odim;
}

template<int NBIT, typename ConvertType=float, typename AccessType=char>
struct NbitReader {
	typedef ConvertType value_type;
	enum { MASK = (1<<NBIT)-1 };
	AccessType const* __restrict__ data;
	NbitReader(void* data_) : data((AccessType*)data_) {}
#if BF_CUDA_ENABLED
	__host__ __device__
#endif
	inline ConvertType operator[](int n) const {
		// TODO: Beware of overflow here
		AccessType word = data[n * NBIT / (sizeof(AccessType)*8)];
		int k = n % ((sizeof(AccessType)*8) / NBIT);
		return (word >> (k*NBIT)) & MASK;
	}
	inline ConvertType operator*() const {
		return (*this)[0];
	}
};

template<typename T> struct value_type           { typedef typename T::value_type type; };
template<typename T> struct value_type<T*>       { typedef T       type; };
template<typename T> struct value_type<T const*> { typedef T const type; };
