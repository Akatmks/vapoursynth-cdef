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
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <VapourSynth4.h>
#include <VSHelper4.h>

#define MAX_SB_SIZE_LOG2 7
#define CDEF_NBLOCKS ((1 << MAX_SB_SIZE_LOG2) / 8)
void svt_cdef_filter_fb(uint8_t *dst8, uint16_t *dst16, int32_t dstride, uint16_t *in, int32_t xdec, int32_t ydec,
                        uint8_t dir[CDEF_NBLOCKS][CDEF_NBLOCKS], int32_t *dirinit,
                        int32_t var[CDEF_NBLOCKS][CDEF_NBLOCKS], int32_t pli, CdefList *dlist, int32_t cdef_count,
                        int32_t level, int32_t sec_strength, int32_t pri_damping, int32_t sec_damping,
                        int32_t coeff_shift, uint8_t subsampling_factor);

struct CDEFData {
    VSNode *clip;
    int64_t pri_strength;
    int64_t sec_strength;
    int64_t pri_damping;
    int64_t sec_damping;
};

static const VSFrame * VS_CC cdef_get_frame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    auto *d = static_cast<CDEFData *>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->clip, frameCtx);
        vsapi->requestFrameFilter(n, d->ref, frameCtx);
    }
    else if (activationReason == arAllFramesReady) {
        const auto clip = vsapi->getFrameFilter(n, d->clip, frameCtx);
        if (!clip) {
            vsapi->setFilterError("vsletterbox: Failed to get frame from VapourSynth", frameCtx);
            return nullptr;
        }
        const auto ref  = vsapi->getFrameFilter(n, d->ref, frameCtx);
        if (!ref) {
            vsapi->freeFrame(clip);
            vsapi->setFilterError("vsletterbox: Failed to get frame from VapourSynth", frameCtx);
            return nullptr;
        }

        const int height = vsapi->getFrameHeight(clip, 0);
        const int width  = vsapi->getFrameWidth(clip, 0);
        if (height != vsapi->getFrameHeight(ref, 0) ||
            width != vsapi->getFrameWidth(ref, 0)) {
            vsapi->freeFrame(clip);
            vsapi->freeFrame(ref);
            vsapi->setFilterError("vsletterbox: Both inputs must have the same height and width", frameCtx);
            return nullptr;
        }

        auto dst   = vsapi->copyFrame(clip, core);
        auto props = vsapi->getFramePropertiesRW(dst);

        const T * VS_RESTRICT ori_srcp   = reinterpret_cast<const T *>(vsapi->getReadPtr(clip, 0));
        const auto            src_stride = vsapi->getStride(clip, 0) / sizeof(T);
        const T * VS_RESTRICT ori_refp   = reinterpret_cast<const T *>(vsapi->getReadPtr(ref, 0));
        const auto            ref_stride = vsapi->getStride(ref, 0) / sizeof(T);

        int start_y = 0;
        int end_y = height - 1;

        auto srcp   = ori_srcp;
        auto refp   = ori_refp;
        auto stats  = ExponentiallyWeightedStats<>();
        auto detect = false;
        for (; start_y < height; start_y++) {
            const auto src_mean = calc_mean<T>(srcp, width);

            const auto st_mean = stats.mean();
            const auto st_stddev = stats.stddev();
            if (st_mean && st_stddev &&
                src_mean > *st_mean + 5 * std::max(*st_stddev, 0.005)) {

                const auto ref_mean = calc_mean<T>(refp, width);
                if (ref_mean > d->ref_thr) {
                    detect = true;
                    break;
                }
            }

            stats.add_data(src_mean);
            srcp += src_stride;
            refp += ref_stride;
        }
        if (!detect)
            start_y = 0;
        
        srcp   = ori_srcp + end_y * src_stride;
        refp   = ori_refp + end_y * ref_stride;
        stats  = ExponentiallyWeightedStats<>();
        detect = false;
        for (; end_y >= 0; end_y--) {
            const auto src_mean = calc_mean<T>(srcp, width);

            const auto st_mean = stats.mean();
            const auto st_stddev = stats.stddev();
            if (st_mean && st_stddev &&
                src_mean > *st_mean + 5 * std::max(*st_stddev, 0.005)) {

                const auto ref_mean = calc_mean<T>(refp, width);
                if (ref_mean > d->ref_thr) {
                    detect = true;
                    break;
                }
            }

            stats.add_data(src_mean);
            srcp -= src_stride;
            refp -= ref_stride;
        }
        if (!detect)
            end_y = height - 1;

        constexpr auto start_y_prop = "VSLETTERBOX_TOP_ROW";
        constexpr auto end_y_prop   = "VSLETTERBOX_BOTTOM_ROW";
        vsapi->mapSetInt(props, start_y_prop, start_y, maReplace);
        vsapi->mapSetInt(props, end_y_prop, end_y, maReplace);
            
        vsapi->freeFrame(clip);
        vsapi->freeFrame(ref);
    
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

    d->clip       = vsapi->mapGetNode(in, "clip", 0, nullptr);
    const auto vi = vsapi->getVideoInfo(d->clip);

    d->pri_strength = vsapi->mapGetInt(in, "pri_strength", 0, nullptr);
    d->sec_strength = vsapi->mapGetInt(in, "sec_strength", 0, nullptr);
    d->pri_damping  = vsapi->mapGetInt(in, "pri_damping", 0, nullptr);
    d->sec_damping  = vsapi->mapGetInt(in, "sec_damping", 0, nullptr);
    
    VSFilterDependency deps[] = {{d->clip, rpStrictSpatial}};
    int num_deps = 1;

    if (vi->format.sampleType == stInteger && (vi->format.bitsPerSample == 12 || vi->format.bitsPerSample == 10))
        vsapi->createVideoFilter(out, "CDEF", vi, cdef_get_frame, cdef_free, fmParallel, deps, num_deps, d.release(), core);
    else {
        vsapi->mapSetError(out, "vsletterbox: Only 12-bit and 10-bit integer format are supported");
        vsapi->freeNode(d->clip);
        return;
    }
}

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->configPlugin("aka.cdef", "cdef", "Constrained Directional Enhancement Filter", VS_MAKE_VERSION(1, 0), VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("CDEF", "clip:vnode;"
                                     "pri_strength:int;"
                                     "sec_strength:int;"
                                     "pri_damping:int"
                                     "sec_damping:int", "clip:vnode;", cdef_create, NULL, plugin);
}
