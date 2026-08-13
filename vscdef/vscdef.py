# vapoursynth-cdef
# Copyright (c) Akatsumekusa and contributors

# ---------------------------------------------------------------------
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
# 
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
# BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
# ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
# ---------------------------------------------------------------------

from vstools import depth, DitherType

def cdef(clip, pri_strength=[3, 1], sec_strength=[1, 0], pri_damping=4, sec_damping=4, **kwargs):
    assert clip.format.bits_per_sample >= 9

    if clip.format.bits_per_sample > 12:
        pre_depth = depth(clip, 12, dither_type=DitherType.NONE)
    else:
        pre_depth = clip

    if clip.width % 8 != 0 or clip.height % 8 != 0:
        width  = (clip.width + 7) >> 3 << 3
        height = (clip.height + 7) >> 3 << 3
        left   = ((-clip.width & 7) + 1) >> 2 << 1 # >> 2 << 1 to avoid handling chroma subsampling
        top    = ((-clip.height & 7) + 1) >> 2 << 1
        pre_resample = pre_depth.fmtc.resample(kernel="point", w=width, h=height, sx=-left, sy=-top, sw=width, sh=height)
    else:
        pre_resample = pre_depth

    post = pre_resample.cdef.CDEF(pri_strength=pri_strength, sec_strength=sec_strength, pri_damping=pri_damping, sec_damping=sec_damping, **kwargs)

    if clip.width % 8 != 0 or clip.height % 8 != 0:
        post_resample = post.fmtc.resample(kernel="point", w=clip.width, h=clip.height, sx=left, sy=top, sw=clip.width, sh=clip.height)
    else:
        post_resample = post

    if clip.format.bits_per_sample > 12:
        post_depth = core.akarin.Expr([clip, depth(post_resample, clip.format), depth(pre_depth, clip.format)], "x y z - +")
    else:
        post_depth = post_resample

    return post_depth
