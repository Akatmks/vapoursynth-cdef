// vapoursynth-cdef
// Copyright (c) Akatsumekusa and contributors

// ---------------------------------------------------------------------
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
// 
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
// ---------------------------------------------------------------------

#include <algorithm>
#include <limits>
#include <cmath>
#include <memory>
#include <optional>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <VapourSynth4.h>
#include <VSHelper4.h>

#include "svt.hpp"

#define VS_MAX_PLANES 3

struct CDEFData {
    VSNode *clip;
    int64_t pri_strength;
    int64_t sec_strength;
    int64_t pri_damping;
    int64_t sec_damping;
    bool    planes[VS_MAX_PLANES];
};

static const VSFrame * VS_CC cdef_get_frame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    auto *d = static_cast<CDEFData *>(instanceData);

    if (activationReason == arInitial)
        vsapi->requestFrameFilter(n, d->clip, frameCtx);
    else if (activationReason == arAllFramesReady) {
        auto clip = vsapi->getFrameFilter(n, d->clip, frameCtx);
        if (!clip) {
            vsapi->setFilterError("vscdef: Failed to get frame from VapourSynth", frameCtx);
            return nullptr;
        }
        const auto height = vsapi->getFrameHeight(clip, 0);
        const auto width  = vsapi->getFrameWidth(clip, 0);
        auto       fi     = vsapi->getVideoFrameFormat(clip);
        if (height % 8 != 0 || width % 8 != 0) {
            vsapi->setFilterError("vscdef: Input clip must be mod8", frameCtx);
            vsapi->freeFrame(clip);
            return nullptr;
        }
        if (fi->subSamplingW > 1 || fi->subSamplingH > 1) {
            vsapi->setFilterError("vscdef: Chroma subsampling of the input clip must be between YUV444 and YUV420", frameCtx);
            vsapi->freeFrame(clip);
            return nullptr;
        }

        auto dst = vsapi->newVideoFrame(fi, width, height, clip, core);

        const uint16_t * VS_RESTRICT ori_srcp[VS_MAX_PLANES];
        std::ptrdiff_t               src_stride[VS_MAX_PLANES];
        uint16_t * VS_RESTRICT       ori_dstp[VS_MAX_PLANES];
        std::ptrdiff_t               dst_stride[VS_MAX_PLANES];

        auto __restrict ori_bufp           = new uint16_t[CDEF_INBUF_SIZE];
        const int32_t   buf_stride         = CDEF_BSTRIDE;
        int32_t         xdec[3];
        int32_t         ydec[3];
        uint8_t         dir[CDEF_NBLOCKS][CDEF_NBLOCKS];
        int32_t         dirinit;
        int32_t         var[CDEF_NBLOCKS][CDEF_NBLOCKS];
        CdefList        dlist[MI_SIZE_64X64 * MI_SIZE_64X64];
        int32_t         cdef_count;
        const int32_t   pri_strength       = d->pri_strength;
        const int32_t   sec_strength       = d->sec_strength;
        const int32_t   pri_damping        = d->pri_damping;
        const int32_t   sec_damping        = d->sec_damping;
        const int32_t   coeff_shift        = fi->bitsPerSample - 8;
        const uint8_t   subsampling_factor = 1;

        for (int pli = 0; pli < fi->numPlanes; pli++) {
            ori_srcp[pli]   = reinterpret_cast<const uint16_t *>(vsapi->getReadPtr(clip, pli));
            src_stride[pli] = vsapi->getStride(clip, pli) / sizeof(uint16_t);
            ori_dstp[pli]   = reinterpret_cast<uint16_t *>(vsapi->getWritePtr(dst, pli));
            dst_stride[pli] = vsapi->getStride(dst, pli) / sizeof(uint16_t);
            xdec[pli]       = !pli ? 0 : fi->subSamplingW;
            ydec[pli]       = !pli ? 0 : fi->subSamplingH;
        }

        // What on earth is this naming hell.
        // Height == Y == Vertical == Row
        // Width == X == Horizontal == Column
        const auto nvfb = (height + MAX_SB_SIZE - 1) >> MAX_SB_SIZE_LOG2;
        const auto nhfb = (width + MAX_SB_SIZE - 1) >> MAX_SB_SIZE_LOG2;
        std::remove_cvref_t<decltype(nvfb)> fbiy[4]; // image_left (0) - halo, image_left (0), image_right, image_right + halo
        std::remove_cvref_t<decltype(nhfb)> fbix[4]; // image_left (0) - halo, image_left (0), image_right, image_right + halo
        std::remove_cvref_t<decltype(nvfb)> fbby[4]; // block_left (0) - halo, block_left (0), block_right, block_right + halo
        std::remove_cvref_t<decltype(nhfb)> fbbx[4]; // block_left (0) - halo, block_left (0), block_right, block_right + halo
        for (int fbr = 0; fbr < nvfb; fbr++) {
            for (int fbc = 0; fbc < nhfb; fbc++) {
                dirinit    = 0;
                cdef_count = 0;
                for (int pli = 0; pli < fi->numPlanes; pli++) {
                    if (!pli || d->planes[pli]) {
                        if (fbr == 0)
                            fbiy[0] = 0;
                        else
                            fbiy[0] = -CDEF_HALO;
                        fbiy[1] = 0;
                        if (fbr == nvfb - 1) {
                            fbiy[2] = ((height - 1) % MAX_SB_SIZE + 1) >> ydec[pli];
                            fbiy[3] = fbiy[2];
                        }
                        else {
                            fbiy[2] = MAX_SB_SIZE >> ydec[pli];
                            fbiy[3] = fbiy[2] + CDEF_HALO;
                        }
                        fbby[0] = -CDEF_HALO;
                        fbby[1] = 0;
                        fbby[2] = MAX_SB_SIZE >> ydec[pli];
                        fbby[3] = fbby[2] + CDEF_HALO;
                        if (fbc == 0)
                            fbix[0] = 0;
                        else
                            fbix[0] = -CDEF_HALO;
                        fbix[1] = 0;
                        if (fbc == nhfb - 1) {
                            fbix[2] = ((width - 1) % MAX_SB_SIZE + 1) >> xdec[pli];
                            fbix[3] = fbix[2];
                        }
                        else {
                            fbix[2] = MAX_SB_SIZE >> xdec[pli];
                            fbix[3] = fbix[2] + CDEF_HALO;
                        }
                        fbbx[0] = -CDEF_HALO;
                        fbbx[1] = 0;
                        fbbx[2] = MAX_SB_SIZE >> xdec[pli];
                        fbbx[3] = fbbx[2] + CDEF_HALO;
    
                        auto VS_RESTRICT fb_srcp = ori_srcp[pli] + fbr * fbby[2] * src_stride[pli] + fbc * fbbx[2];
                        auto VS_RESTRICT fb_dstp = ori_dstp[pli] + fbr * fbby[2] * dst_stride[pli] + fbc * fbbx[2];
                        auto VS_RESTRICT fb_bufp = ori_bufp + CDEF_VBORDER * buf_stride + CDEF_HBORDER;
                        auto VS_RESTRICT bufp    = fb_bufp + fbby[0] * buf_stride;
                        std::remove_cvref_t<decltype(fbby[0])> y;
                        for (y = fbby[0]; y < fbiy[0]; y++) {
                            std::fill(bufp + fbbx[0], bufp + fbbx[3], CDEF_VERY_LARGE);
                            bufp += buf_stride;
                        }
                        auto VS_RESTRICT srcp    = fb_srcp + fbiy[0] * src_stride[pli];
                        for (; y < fbiy[3]; y++) {
                            std::fill(bufp + fbbx[0], bufp + fbix[0], CDEF_VERY_LARGE);
                            std::copy(srcp + fbix[0], srcp + fbix[3], bufp + fbix[0]);
                            std::fill(bufp + fbix[3], bufp + fbbx[3], CDEF_VERY_LARGE);
                            bufp += buf_stride;
                            srcp += src_stride[pli];
                        }
                        for (; y < fbby[3]; y++) {
                            std::fill(bufp + fbbx[0], bufp + fbbx[3], CDEF_VERY_LARGE);
                            bufp += buf_stride;
                        }
    
                        for (auto y_idx = fbiy[1] >> 3; y_idx < fbiy[2] >> 3; y_idx++) {
                            for (auto x_idx = fbix[1] >> 3; x_idx < fbix[2] >> 3; x_idx++) {
                                dlist[cdef_count].by = y_idx;
                                dlist[cdef_count].bx = x_idx;
                                cdef_count++;
                            }
                        }
    
                        svt_cdef_filter_fb(nullptr, fb_dstp, dst_stride[pli],
                                           fb_bufp, xdec[pli], ydec[pli],
                                           dir, &dirinit,
                                           var, pli, dlist, cdef_count,
                                           pri_strength, sec_strength, pri_damping, sec_damping,
                                           coeff_shift, subsampling_factor);
                    }
                }
            }
        }

        for (int pli = 0; pli < fi->numPlanes; pli++) {
            if (!d->planes[pli]) {
                const auto       pl_height = vsapi->getFrameHeight(clip, pli);
                const auto       pl_width  = vsapi->getFrameWidth(clip, pli);
                auto VS_RESTRICT srcp      = ori_srcp[pli];
                auto VS_RESTRICT dstp      = ori_dstp[pli];
                for (std::remove_cvref_t<decltype(pl_height)> y = 0; y < pl_height; y++) {
                    std::copy_n(srcp, pl_width, dstp);
                    srcp += src_stride[pli];
                    dstp += dst_stride[pli];
                }
            }
        }

        vsapi->freeFrame(clip);
        delete[] ori_bufp;

        return dst;
    }

    return nullptr;
}

static void VS_CC cdef_free(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    auto *d = static_cast<CDEFData *>(instanceData);
    vsapi->freeNode(d->clip);
    delete d;
}

static void VS_CC cdef_create(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<CDEFData> d(new CDEFData);

    d->clip = vsapi->mapGetNode(in, "clip", 0, nullptr);
    auto vi = vsapi->getVideoInfo(d->clip);

    if (vi->width % 8 != 0 || vi->height % 8 != 0) {
        vsapi->mapSetError(out, "vscdef: Input clip must be mod8");
        vsapi->freeNode(d->clip);
        return;
    }
    if (vi->format.sampleType != stInteger || (vi->format.bitsPerSample != 12 && vi->format.bitsPerSample != 10)) {
        vsapi->mapSetError(out, "vscdef: Only 12-bit and 10-bit integer format are supported");
        vsapi->freeNode(d->clip);
        return;
    }

    d->pri_strength = vsapi->mapGetInt(in, "pri_strength", 0, nullptr);
    d->sec_strength = vsapi->mapGetInt(in, "sec_strength", 0, nullptr);
    d->pri_damping  = vsapi->mapGetInt(in, "pri_damping", 0, nullptr);
    d->sec_damping  = vsapi->mapGetInt(in, "sec_damping", 0, nullptr);

    const int num_i = vsapi->mapNumElements(in, "planes");
    if (num_i == -1) {
        for (int plane = 0; plane < VS_MAX_PLANES; plane++)
            d->planes[plane] = true;
    }
    else {
        for (int plane = 0; plane < VS_MAX_PLANES; plane++)
            d->planes[plane] = false;
        for (int i = 0; i < num_i; i++) {
            int64_t plane = vsapi->mapGetInt(in, "planes", i, nullptr);
            if (plane < VS_MAX_PLANES)
                d->planes[plane] = true;
            else {
                vsapi->mapSetError(out, "vscdef: Invalid element in planes parameter");
                vsapi->freeNode(d->clip);
                return;
            }
        }
    }

    VSFilterDependency deps[] = {{d->clip, rpStrictSpatial}};
    int num_deps = 1;
    
    svt_aom_setup_common_rtcd_internal(EB_CPU_FLAGS_ALL);
    svt_aom_setup_rtcd_internal(EB_CPU_FLAGS_ALL);

    vsapi->createVideoFilter(out, "CDEF", vi, cdef_get_frame, cdef_free, fmParallel, deps, num_deps, d.release(), core);
}

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->configPlugin("aka.cdef", "cdef", "Constrained Directional Enhancement Filter", VS_MAKE_VERSION(1, 0), VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("CDEF", "clip:vnode;"
                                     "pri_strength:int;"
                                     "sec_strength:int;"
                                     "pri_damping:int;"
                                     "sec_damping:int;"
                                     "planes:int[]:opt", "clip:vnode;", cdef_create, NULL, plugin);
}
