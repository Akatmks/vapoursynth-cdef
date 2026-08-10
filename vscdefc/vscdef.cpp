// vapoursynth-cdef
// Copyright (c) Akatsumekusa and contributors

// ---------------------------------------------------------------------
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in
//    the documentation and/or other materials provided with the
//    distribution.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
// COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
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

struct FindData {
    VSNode *clip;
    VSNode *ref;
    double  ref_thr;
};

template <typename T>
static inline constexpr T get_max_value() {
    if constexpr (std::is_integral_v<T>)
        return std::numeric_limits<T>::max();
    else
        return 1.0;
}
// template <typename T>
// static inline constexpr T get_min_value() {
//     if constexpr (std::is_integral_v<T>)
//         return std::numeric_limits<T>::min();
//     else
//         return 0.0;
// }

template <typename T>
static inline double calc_mean(const T * VS_RESTRICT srcp, int width) {
    constexpr T max_value = get_max_value<T>();
    // constexpr T min_value = get_min_value<T>();

    std::conditional_t<std::is_same_v<T, uint8_t>,
                       uint32_t,
                       std::conditional_t<std::is_same_v<T, uint16_t>,
                                          uint64_t,
                                          double>>
        sum = 0;
    // if constexpr (std::is_integral_v<T>) {
        #pragma clang loop vectorize(assume_safety) interleave(enable)
        for (int x = 0; x < width; x++) {
            #pragma clang fp reassociate(on)
            sum += srcp[x];
        }
    // }
    // else {
        // #pragma clang loop vectorize(assume_safety) interleave(enable)
        // for (int x = 0; x < width; x++) {
        //     #pragma clang fp reassociate(on)
        //     sum += std::clamp(srcp[x], min_value, max_value);
        // }
    // }

    return static_cast<double>(sum) / width / max_value;
}

// template <typename T>
// static inline double calc_15_power_mean(const T * VS_RESTRICT srcp, int width) {
//     constexpr T max_value = get_max_value<T>();
//     constexpr T min_value = get_min_value<T>();

//     double sum = 0.0;
//     if constexpr (std::is_integral_v<T>) {
//         #pragma clang loop vectorize(assume_safety) interleave(enable)
//         for (int x = 0; x < width; x++) {
//             #pragma clang fp reassociate(on)
//             const auto x_ = static_cast<double>(srcp[x]);
//             sum += x_ * std::sqrt(x_);
//         }
//     }
//     else {
//         #pragma clang loop vectorize(assume_safety) interleave(enable)
//         for (int x = 0; x < width; x++) {
//             #pragma clang fp reassociate(on)
//             const auto x_ = static_cast<double>(std::clamp(srcp[x], min_value, max_value));
//             sum += x_ * std::sqrt(x_);
//         }
//     }

//     return std::pow(sum / width, 2.0 / 3) / max_value;
// }

// template <typename T>
// static inline double calc_root_mean_square(const T * VS_RESTRICT srcp, int width) {
//     constexpr T max_value = get_max_value<T>();
//     constexpr T min_value = get_min_value<T>();

//     double sum = 0.0;
//     if constexpr (std::is_integral_v<T>) {
//         #pragma clang loop vectorize(assume_safety) interleave(enable)
//         for (int x = 0; x < width; x++) {
//             #pragma clang fp reassociate(on)
//             const auto x_ = static_cast<double>(srcp[x]);
//             sum += x_ * x_;
//         }
//     }
//     else {
//         #pragma clang loop vectorize(assume_safety) interleave(enable)
//         for (int x = 0; x < width; x++) {
//             #pragma clang fp reassociate(on)
//             const auto x_ = static_cast<double>(std::clamp(srcp[x], min_value, max_value));
//             sum += x_ * x_;
//         }
//     }

//     return std::sqrt(sum / width) / max_value;
// }

// Incremental calculation of weighted mean and variance, Tony Finch, 2009
template <double alpha = 0.05>
class ExponentiallyWeightedStats {
    static_assert(alpha > 0.0 && alpha < 1.0);
    static constexpr double one_minus_alpha = 1 - alpha;

    bool   init = false;
    double _mean;
    double _var;

public:
    void add_data(double data) {
        if (!init) {
            init  = true;
            _mean = data;
            _var  = 0.0;
        }
        else {
            const double diff =  data - _mean;
            const double incr =  alpha * diff;
            _mean             += incr;
            _var              =  one_minus_alpha * (_var + diff * incr);
        }
    }
    std::optional<double> mean() {
        if (!init)
            return std::nullopt;
        else
            return _mean;
    }
    std::optional<double> stddev() {
        if (!init)
            return std::nullopt;
        else
            return std::sqrt(_var);
    }
};

template <typename T>
static const VSFrame * VS_CC letterbox_search_get_frame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    auto *d = static_cast<FindData *>(instanceData);

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

static void VS_CC letterbox_search_free(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    auto *d = static_cast<FindData *>(instanceData);
    vsapi->freeNode(d->clip);
    vsapi->freeNode(d->ref);
    delete d;
}

static void VS_CC letterbox_search_create(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<FindData> d(new FindData);

    d->clip           = vsapi->mapGetNode(in, "clip", 0, nullptr);
    const auto vi     = vsapi->getVideoInfo(d->clip);
    d->ref            = vsapi->mapGetNode(in, "ref", 0, nullptr);
    const auto ref_vi = vsapi->getVideoInfo(d->ref);
    if (!vsh::isConstantVideoFormat(vi) || !vsh::isConstantVideoFormat(ref_vi) ||
        vi->format.sampleType != ref_vi->format.sampleType ||
        vi->format.bitsPerSample != ref_vi->format.bitsPerSample ||
        vi->height != ref_vi->height ||
        vi->width != ref_vi->width) {
        vsapi->mapSetError(out, "vsletterbox: Both input must be of constant format, and of the same sample type, bits per sample, height, and width");
        vsapi->freeNode(d->clip);
        vsapi->freeNode(d->ref);
        return;
    }

    d->ref_thr = vsapi->mapGetFloat(in, "ref_thr", 0, nullptr);
    
    VSFilterDependency deps[] = {{d->clip, rpStrictSpatial}, {d->ref, rpStrictSpatial}};
    int num_deps = 2;

    if (vi->format.sampleType == stInteger && vi->format.bitsPerSample == 32)
        vsapi->createVideoFilter(out, "Find", vi, letterbox_search_get_frame<uint32_t>, letterbox_search_free, fmParallel, deps, num_deps, d.release(), core);
    else if (vi->format.sampleType == stInteger && vi->format.bitsPerSample == 16)
        vsapi->createVideoFilter(out, "Find", vi, letterbox_search_get_frame<uint16_t>, letterbox_search_free, fmParallel, deps, num_deps, d.release(), core);
    else if (vi->format.sampleType == stInteger && vi->format.bitsPerSample == 8)
        vsapi->createVideoFilter(out, "Find", vi, letterbox_search_get_frame<uint8_t>, letterbox_search_free, fmParallel, deps, num_deps, d.release(), core);
    else if (vi->format.sampleType == stFloat && vi->format.bitsPerSample == 32)
        vsapi->createVideoFilter(out, "Find", vi, letterbox_search_get_frame<float>, letterbox_search_free, fmParallel, deps, num_deps, d.release(), core);
    else {
        vsapi->mapSetError(out, "vsletterbox: Only 32-bit, 16-bit, or 8-bit integer format or 32-bit float format are supported");
        vsapi->freeNode(d->clip);
        vsapi->freeNode(d->ref);
        return;
    }
}

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->configPlugin("aka.letterbox", "letterbox", "Letterbox Detection and Cleaning", VS_MAKE_VERSION(1, 0), VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("Find", "clip:vnode;"
                                     "ref:vnode;"
                                     "ref_thr:float;", "clip:vnode;", letterbox_search_create, NULL, plugin);
}
