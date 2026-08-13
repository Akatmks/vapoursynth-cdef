<h1 align="center">vapoursynth-cdef</h1>

VapourSynth port of Constrained Directional Enhancement Filter.  

### Usage

```py
from vscdef import cdef

clip = cdef(clip)
```

### Reference

The Python wrapper `vscdef.cdef` provides exactly the same features as the VapourSynth plugin `core.cdef.CDEF`.  
However, the VapourSynth plugin requires 12-bit or 10-bit integer input, and requires mod8 input. The Python wrapper converts it for you.  

```py
cdef(
    clip:         vs.VideoNode,

    # All these parameters accept 1 (for all planes), 2 (for luma and chroma), or 3 (for Y, U, and V) integers.
    pri_strength: int | Sequence[int] = [3, 1],
    sec_strength: int | Sequence[int] = [1, 0],
    pri_damping:  int | Sequence[int] = 2,
    sec_damping:  int | Sequence[int] = 1,
    # If the default is too strong, try disabling secondary filtering by setting `sec_strength` to `0` first.  
    # If the default is too weak, try increasing `pri_strength` (of the Y plane) first.  

    planes:       PlanesT             = [0, 1, 2]
)
```

For each parameters, check:  
* https://github.com/5fish/SVT-AV1/blob/main/Docs/Appendix-CDEF.md  
* https://arxiv.org/pdf/1602.05975  

Note that even though the AV1 codec only supports certain value ranges, the implementation itself supports a much wider range of values.  

### Examples

On a bad source with heavy mosquito noise, this might be useful.  
Note that on newer versions of vs-jetpack with MVUtensils, unfortunately the support for `SADMode.ADAPTIVE_SATD_DCT` is dropped. You can either modify it to do a different search, or rewrite it using raw MVTools calls if your setup doesn't support it.  

```py
from vsdenoise import MotionMode, MVTools, Prefilter, SADMode
from vscdef import cdef

dn = cdef(clip)

mv = MVTools(clip, search_clip=Prefilter.DFTTEST)

mv.analyze(tr=2, blksize=32, overlap=16, truemotion=MotionMode.COHERENCE, divide=2)
mv.recalculate(thsad=10, blksize=8, overlap=4, dct=SADMode.ADAPTIVE_SATD_DCT, truemotion=MotionMode.COHERENCE)

dn = mv.degrain(dn, clip, tr=2, thsad=18)
```
