// vapoursynth-cdef

// Copyright (c) 2016, Alliance for Open Media. All rights reserved
//
// This source code is subject to the terms of the BSD 3-Clause Clear License and
// the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear License
// was not distributed with this source code in the LICENSE file, you can
// obtain it at https://www.aomedia.org/license. If the Alliance for Open
// Media Patent License 1.0 was not distributed with this source code in the
// PATENTS file, you can obtain it at https://www.aomedia.org/license/patent-license.

#ifndef SVT_HPP
#define SVT_HPP

#include <cstdint>


typedef uint64_t EbCpuFlags;

#define EB_CPU_FLAGS_INVALID (1ULL << (sizeof(EbCpuFlags) * 8ULL - 1ULL))
#define EB_CPU_FLAGS_ALL ((EB_CPU_FLAGS_INVALID >> 1) - 1)

void svt_aom_setup_common_rtcd_internal(EbCpuFlags flags);
void svt_aom_setup_rtcd_internal(EbCpuFlags flags);

#define MAX_SB_SIZE_LOG2 7
#define CDEF_NBLOCKS ((1 << MAX_SB_SIZE_LOG2) / 8)

typedef struct {
    uint8_t by;
    uint8_t bx;
} CdefList;

#define MI_SIZE_LOG2 2
#define MI_SIZE_64X64 (64 >> MI_SIZE_LOG2)

void svt_cdef_filter_fb(uint8_t *dst8, uint16_t *dst16, int32_t dstride, uint16_t *in, int32_t xdec, int32_t ydec,
                        uint8_t dir[CDEF_NBLOCKS][CDEF_NBLOCKS], int32_t *dirinit,
                        int32_t var[CDEF_NBLOCKS][CDEF_NBLOCKS], int32_t pli, CdefList *dlist, int32_t cdef_count,
                        int32_t level, int32_t sec_strength, int32_t pri_damping, int32_t sec_damping,
                        int32_t coeff_shift, uint8_t subsampling_factor);


#define MAX_SB_SIZE (1 << MAX_SB_SIZE_LOG2)

#define ALIGN_POWER_OF_TWO(value, n) (((value) + ((1 << (n)) - 1)) & ~((1 << (n)) - 1))

/* We need to buffer three vertical lines. */
#define CDEF_VBORDER (3)
/* We only need to buffer three horizontal pixels too, but let's align to
16 bytes (8 x 16 bits) to make vectorization easier. */
#define CDEF_HBORDER (8)
// CDEF taps reach at most +-CDEF_HALO pixels (see eb_cdef_directions), so the recon copies/narrows in
// svt_av1_cdef_frame only need a 2-px halo; the buffer stays HBORDER/VBORDER-padded for aligned loads.
#define CDEF_HALO 2
#define CDEF_BSTRIDE ALIGN_POWER_OF_TWO((1 << MAX_SB_SIZE_LOG2) + 2 * CDEF_HBORDER, 3)
// Value is chosen so that memset can be used in cdef_seg_search().  Must be a large
// int16_t value.
#define CDEF_VERY_LARGE ((uint8_t)~0 >> 1 | ((uint8_t)~0 >> 1) << 8)
#define CDEF_INBUF_SIZE (CDEF_BSTRIDE * ((1 << MAX_SB_SIZE_LOG2) + 2 * CDEF_VBORDER))


#endif // SVT_HPP
