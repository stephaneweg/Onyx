# v3d_shaders.qasm -- the QPU shaders of kernel/sys/v3d.cpp (kapi v53 gpu_render), written by hand.
# Assembled by tools/qpu/qpuasm (cd tools/qpu && make) into v3d_shaders.inc, which is committed.
# The syntax is Mesa's disassembler's: "add op ; mul op ; signals"; every line is re-disassembled
# and checked against the V3D 4.2 instruction restrictions.
#
# The vertex (kapi_gpu_vertex3): position x y z w (clip space, floats), s t (floats), r g b a
# (normalized bytes). The VPM input of the vertex shader is those 10 values in that order; the
# coordinate shader reads the 4 of the position only.

# ---- vertex shader (render): Xs Ys (24.8 fixed point) Zs 1/Wc, then the varyings s t r g b a ----
# uniforms: the 4 x 4 matrix (row by row: clip = M * (x y z w)), then x scale, y scale (the half
# viewport * 256, y negated), z scale, z offset.
.name VS_CLIP vertex
ldvpmv_in rf1, 0              ; nop                         # x
ldvpmv_in rf2, 1              ; nop                         # y
ldvpmv_in rf3, 2              ; nop                         # z
ldvpmv_in rf4, 3              ; nop                         # w
ldvpmv_in rf5, 4              ; nop                         # s
ldvpmv_in rf6, 5              ; nop                         # t
ldvpmv_in rf7, 6              ; nop                         # r
ldvpmv_in rf8, 7              ; nop                         # g
ldvpmv_in rf9, 8              ; nop                         # b
ldvpmv_in rf10, 9             ; nop                         # a
nop                           ; nop                         ; ldunif          # m00
nop                           ; fmul r0, rf1, r5            ; ldunif          # m01
nop                           ; fmul r1, rf2, r5            ; ldunif          # m02
fadd r0, r0, r1                ; fmul r1, rf3, r5            ; ldunif          # m03
fadd r0, r0, r1                ; fmul r1, rf4, r5
fadd rf20, r0, r1              ; nop
nop                           ; nop                         ; ldunif          # m10
nop                           ; fmul r0, rf1, r5            ; ldunif          # m11
nop                           ; fmul r1, rf2, r5            ; ldunif          # m12
fadd r0, r0, r1                ; fmul r1, rf3, r5            ; ldunif          # m13
fadd r0, r0, r1                ; fmul r1, rf4, r5
fadd rf21, r0, r1              ; nop
nop                           ; nop                         ; ldunif          # m20
nop                           ; fmul r0, rf1, r5            ; ldunif          # m21
nop                           ; fmul r1, rf2, r5            ; ldunif          # m22
fadd r0, r0, r1                ; fmul r1, rf3, r5            ; ldunif          # m23
fadd r0, r0, r1                ; fmul r1, rf4, r5
fadd rf22, r0, r1              ; nop
nop                           ; nop                         ; ldunif          # m30
nop                           ; fmul r0, rf1, r5            ; ldunif          # m31
nop                           ; fmul r1, rf2, r5            ; ldunif          # m32
fadd r0, r0, r1                ; fmul r1, rf3, r5            ; ldunif          # m33
fadd r0, r0, r1                ; fmul r1, rf4, r5
fadd rf23, r0, r1              ; nop
nop                           ; nop                         ; ldunifrf.rf11   # x scale
nop                           ; mov recip, rf23             ; ldunifrf.rf12   # 1/w -> r4 (3 later) ; y scale
nop                           ; nop                         ; ldunifrf.rf13   # z scale
nop                           ; nop                         ; ldunifrf.rf14   # z offset
nop                           ; fmul r0, rf20, r4           # x/w
nop                           ; fmul r1, rf21, r4           # y/w
nop                           ; fmul r2, rf22, r4           # z/w
nop                           ; fmul r0, r0, rf11           # Xs (float)
ftoin r0, r0                  ; fmul r1, r1, rf12           # Xs ; Ys (float)
ftoin r1, r1                  ; fmul r2, r2, rf13           # Ys ; z/w * z scale
fadd r2, r2, rf14             ; nop                         # Zs
stvpmv 0, r0                  ; nop
stvpmv 1, r1                  ; nop
stvpmv 2, r2                  ; nop
stvpmv 3, r4                  ; nop                         # 1/Wc
stvpmv 4, rf5                 ; nop
stvpmv 5, rf6                 ; nop
stvpmv 6, rf7                 ; nop
stvpmv 7, rf8                 ; nop
stvpmv 8, rf9                 ; nop
stvpmv 9, rf10                ; nop
vpmwt -                       ; nop
nop                           ; nop                         ; thrsw
nop                           ; nop
nop                           ; nop

# ---- coordinate shader (binning): Xc Yc Zc Wc, then Xs Ys -----------------------------------------------
# uniforms: the matrix, x scale, y scale.
.name CS_CLIP coord
ldvpmv_in rf1, 0              ; nop
ldvpmv_in rf2, 1              ; nop
ldvpmv_in rf3, 2              ; nop
ldvpmv_in rf4, 3              ; nop
nop                           ; nop                         ; ldunif          # m00
nop                           ; fmul r0, rf1, r5            ; ldunif          # m01
nop                           ; fmul r1, rf2, r5            ; ldunif          # m02
fadd r0, r0, r1                ; fmul r1, rf3, r5            ; ldunif          # m03
fadd r0, r0, r1                ; fmul r1, rf4, r5
fadd rf20, r0, r1              ; nop
nop                           ; nop                         ; ldunif          # m10
nop                           ; fmul r0, rf1, r5            ; ldunif          # m11
nop                           ; fmul r1, rf2, r5            ; ldunif          # m12
fadd r0, r0, r1                ; fmul r1, rf3, r5            ; ldunif          # m13
fadd r0, r0, r1                ; fmul r1, rf4, r5
fadd rf21, r0, r1              ; nop
nop                           ; nop                         ; ldunif          # m20
nop                           ; fmul r0, rf1, r5            ; ldunif          # m21
nop                           ; fmul r1, rf2, r5            ; ldunif          # m22
fadd r0, r0, r1                ; fmul r1, rf3, r5            ; ldunif          # m23
fadd r0, r0, r1                ; fmul r1, rf4, r5
fadd rf22, r0, r1              ; nop
nop                           ; nop                         ; ldunif          # m30
nop                           ; fmul r0, rf1, r5            ; ldunif          # m31
nop                           ; fmul r1, rf2, r5            ; ldunif          # m32
fadd r0, r0, r1                ; fmul r1, rf3, r5            ; ldunif          # m33
fadd r0, r0, r1                ; fmul r1, rf4, r5
fadd rf23, r0, r1              ; nop
nop                           ; nop                         ; ldunifrf.rf11
nop                           ; mov recip, rf23             ; ldunifrf.rf12
stvpmv 0, rf20                ; nop
stvpmv 1, rf21                ; nop
stvpmv 2, rf22                ; nop
stvpmv 3, rf23                ; nop
nop                           ; fmul r0, rf20, r4
nop                           ; fmul r1, rf21, r4
nop                           ; fmul r0, r0, rf11
ftoin r0, r0                  ; fmul r1, r1, rf12
ftoin r1, r1                  ; nop
stvpmv 4, r0                  ; nop
stvpmv 5, r1                  ; nop
vpmwt -                       ; nop
nop                           ; nop                         ; thrsw
nop                           ; nop
nop                           ; nop

# ---- fragment shader: the interpolated colour ------------------------------------------------------
# rf0 = W (payload); a varying = ldvary * W + C, C landing in r5 two instructions after its ldvary.
# s and t are read (in order) and left unused.
.name FS_COLOR frag
nop                           ; nop                         ; ldvary.r0       # s (unused)
nop                           ; nop
nop                           ; nop                         ; ldvary.r0       # t (unused)
nop                           ; nop
nop                           ; nop                         ; ldvary.r0       # r
nop                           ; fmul r1, r0, rf0
fadd rf3, r1, r5              ; nop                         ; ldvary.r0       # R ; g
nop                           ; fmul r1, r0, rf0
fadd rf4, r1, r5              ; nop                         ; ldvary.r0       # G ; b
nop                           ; fmul r1, r0, rf0
fadd rf5, r1, r5              ; nop                         ; thrsw ; ldvary.r0   # B ; a
nop                           ; fmul r1, r0, rf0            ; thrsw           # (last segment)
fadd rf6, r1, r5              ; nop                         # A
vfpack tlb, rf3, rf4          ; nop                         ; thrsw           # (end)
vfpack tlb, rf5, rf6          ; nop
nop                           ; nop

# ---- fragment shader: texture * colour --------------------------------------------------------------
# uniforms: TMU config p0 (texture state | 3 = two words returned: RG, BA as f16), p1 (sampler state),
# written (wrtmuc) between the T and the S coordinates, as Mesa does.
.name FS_TEX frag
nop                           ; nop                         ; ldvary.r0            # s
nop                           ; fmul r1, r0, rf0
fadd r1, r1, r5               ; nop                         ; ldvary.r0            # S ; t
nop                           ; fmul r2, r0, rf0
fadd r2, r2, r5               ; nop                         ; ldvary.r0            # T ; r
nop                           ; mov tmut, r2                # T (Mesa's order: T, the configs, S)
nop                           ; fmul r3, r0, rf0            ; wrtmuc               # p0
fadd rf3, r3, r5              ; nop                         ; ldvary.r0 ; wrtmuc   # R ; g ; p1
nop                           ; mov tmus, r1                # S: the lookup starts
nop                           ; fmul r3, r0, rf0
fadd rf4, r3, r5              ; nop                         ; ldvary.r0            # G ; b
nop                           ; fmul r3, r0, rf0
fadd rf5, r3, r5              ; nop                         ; ldvary.r0            # B ; a
nop                           ; fmul r3, r0, rf0            ; thrsw                # (wait for the TMU)
fadd rf6, r3, r5              ; nop                         ; thrsw                # A (last segment)
nop                           ; nop
nop                           ; nop                         ; ldtmu.r0             # texel R G
nop                           ; fmul r1, r0.l, rf3          ; ldtmu.r2             # R ; texel B A
nop                           ; fmul r0, r0.h, rf4          # G
nop                           ; fmul r3, r2.l, rf5          # B
nop                           ; fmul r2, r2.h, rf6          # A
vfpack tlb, r1, r0            ; nop                         ; thrsw                # (end)
vfpack tlb, r3, r2            ; nop
nop                           ; nop
