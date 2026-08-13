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

    pri_strength: int | Sequence[int] = [3, 1],
    sec_strength: int | Sequence[int] = [1, 0],
    pri_damping:  int | Sequence[int] = 4,
    sec_damping:  int | Sequence[int] = 4,

    planes:       PlanesT             = [0, 1, 2]
)
```

For each parameters, check:  
* https://github.com/5fish/SVT-AV1/blob/main/Docs/Appendix-CDEF.md  
* https://arxiv.org/pdf/1602.05975  

Note that even though the AV1 codec only supports certain value ranges, the implementation supports using a value outside that range.  
