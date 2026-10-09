# v3d_shaders71.qasm -- the QPU shaders of kernel/sys/v3d.cpp for the V3D 7.1 (VideoCore VII, the
# Raspberry Pi 5), the same programs as v3d_shaders.qasm (V3D 4.2) instruction for instruction:
# tools/tests/v3d/kshaders_test.cpp runs both in the simulator and compares their outputs.
# Assembled by tools/qpu/qpuasm (cd tools/qpu && make) into v3d_shaders71.inc, which is committed.
#
# What V3D 7.1 changes here (docs/PI5-PORT.md §9.2): no accumulators -- r0..r3 become rf registers;
# ldunif writes rf0 (r5 on 4.2), ldvary's C term lands in rf0 one instruction after it (r5 on 4.2);
# the fragment payload's W is rf3 (rf0 on 4.2: copied into rf4 first, as Mesa does); recip is an add
# operation with a destination (4.2: the magic write, the result in r4); no vpmwt at the end (GFXH-1684
# is 4.2's); one small immediate an instruction, and none beside a signal. The fragment shaders run
# 4-way threaded: they use rf0..rf15 only (16 registers a thread); the vertex and coordinate shaders
# rf0..rf31 (v3d.cpp makes them 2-way on 7.1).
.version 71

# ---- vertex shader (render): Xs Ys (24.8 fixed point) Zs 1/Wc, then the varyings s t r g b a r2 g2 b2 a2
# uniforms: the 4 x 4 matrix (row by row: clip = M * (x y z w)), then x scale, y scale (the half
# viewport * 256 on 4.2 -- * 64 on 7.1: the clipper's 1/64 pixel --, y negated), z scale, z offset.
# rf24 rf25 rf26: 4.2's r0 r1 r2; rf27: 1/w (4.2's r4).
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
ldvpmv_in rf15, 10            ; nop                         # r2
ldvpmv_in rf16, 11            ; nop                         # g2
ldvpmv_in rf17, 12            ; nop                         # b2
ldvpmv_in rf18, 13            ; nop                         # a2
nop                           ; nop                         ; ldunif          # m00
nop                           ; fmul rf24, rf1, rf0         ; ldunif          # m01
nop                           ; fmul rf25, rf2, rf0         ; ldunif          # m02
fadd rf24, rf24, rf25         ; fmul rf25, rf3, rf0         ; ldunif          # m03
fadd rf24, rf24, rf25         ; fmul rf25, rf4, rf0
fadd rf20, rf24, rf25         ; nop
nop                           ; nop                         ; ldunif          # m10
nop                           ; fmul rf24, rf1, rf0         ; ldunif          # m11
nop                           ; fmul rf25, rf2, rf0         ; ldunif          # m12
fadd rf24, rf24, rf25         ; fmul rf25, rf3, rf0         ; ldunif          # m13
fadd rf24, rf24, rf25         ; fmul rf25, rf4, rf0
fadd rf21, rf24, rf25         ; nop
nop                           ; nop                         ; ldunif          # m20
nop                           ; fmul rf24, rf1, rf0         ; ldunif          # m21
nop                           ; fmul rf25, rf2, rf0         ; ldunif          # m22
fadd rf24, rf24, rf25         ; fmul rf25, rf3, rf0         ; ldunif          # m23
fadd rf24, rf24, rf25         ; fmul rf25, rf4, rf0
fadd rf22, rf24, rf25         ; nop
nop                           ; nop                         ; ldunif          # m30
nop                           ; fmul rf24, rf1, rf0         ; ldunif          # m31
nop                           ; fmul rf25, rf2, rf0         ; ldunif          # m32
fadd rf24, rf24, rf25         ; fmul rf25, rf3, rf0         ; ldunif          # m33
fadd rf24, rf24, rf25         ; fmul rf25, rf4, rf0
fadd rf23, rf24, rf25         ; nop
nop                           ; nop                         ; ldunifrf.rf11   # x scale
recip rf27, rf23              ; nop                         ; ldunifrf.rf12   # 1/w ; y scale
nop                           ; nop                         ; ldunifrf.rf13   # z scale
nop                           ; nop                         ; ldunifrf.rf14   # z offset
nop                           ; fmul rf24, rf20, rf27       # x/w
nop                           ; fmul rf25, rf21, rf27       # y/w
nop                           ; fmul rf26, rf22, rf27       # z/w
nop                           ; fmul rf24, rf24, rf11       # Xs (float)
ftoin rf24, rf24              ; fmul rf25, rf25, rf12       # Xs ; Ys (float)
ftoin rf25, rf25              ; fmul rf26, rf26, rf13       # Ys ; z/w * z scale
fadd rf26, rf26, rf14         ; nop                         # Zs
stvpmv 0, rf24                ; nop
stvpmv 1, rf25                ; nop
stvpmv 2, rf26                ; nop
stvpmv 3, rf27                ; nop                         # 1/Wc
stvpmv 4, rf5                 ; nop
stvpmv 5, rf6                 ; nop
stvpmv 6, rf7                 ; nop
stvpmv 7, rf8                 ; nop
stvpmv 8, rf9                 ; nop
stvpmv 9, rf10                ; nop
stvpmv 10, rf15               ; nop
stvpmv 11, rf16               ; nop
stvpmv 12, rf17               ; nop
stvpmv 13, rf18               ; nop
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
nop                           ; fmul rf24, rf1, rf0         ; ldunif          # m01
nop                           ; fmul rf25, rf2, rf0         ; ldunif          # m02
fadd rf24, rf24, rf25         ; fmul rf25, rf3, rf0         ; ldunif          # m03
fadd rf24, rf24, rf25         ; fmul rf25, rf4, rf0
fadd rf20, rf24, rf25         ; nop
nop                           ; nop                         ; ldunif          # m10
nop                           ; fmul rf24, rf1, rf0         ; ldunif          # m11
nop                           ; fmul rf25, rf2, rf0         ; ldunif          # m12
fadd rf24, rf24, rf25         ; fmul rf25, rf3, rf0         ; ldunif          # m13
fadd rf24, rf24, rf25         ; fmul rf25, rf4, rf0
fadd rf21, rf24, rf25         ; nop
nop                           ; nop                         ; ldunif          # m20
nop                           ; fmul rf24, rf1, rf0         ; ldunif          # m21
nop                           ; fmul rf25, rf2, rf0         ; ldunif          # m22
fadd rf24, rf24, rf25         ; fmul rf25, rf3, rf0         ; ldunif          # m23
fadd rf24, rf24, rf25         ; fmul rf25, rf4, rf0
fadd rf22, rf24, rf25         ; nop
nop                           ; nop                         ; ldunif          # m30
nop                           ; fmul rf24, rf1, rf0         ; ldunif          # m31
nop                           ; fmul rf25, rf2, rf0         ; ldunif          # m32
fadd rf24, rf24, rf25         ; fmul rf25, rf3, rf0         ; ldunif          # m33
fadd rf24, rf24, rf25         ; fmul rf25, rf4, rf0
fadd rf23, rf24, rf25         ; nop
nop                           ; nop                         ; ldunifrf.rf11
recip rf27, rf23              ; nop                         ; ldunifrf.rf12
stvpmv 0, rf20                ; nop
stvpmv 1, rf21                ; nop
stvpmv 2, rf22                ; nop
stvpmv 3, rf23                ; nop
nop                           ; fmul rf24, rf20, rf27
nop                           ; fmul rf25, rf21, rf27
nop                           ; fmul rf24, rf24, rf11
ftoin rf24, rf24              ; fmul rf25, rf25, rf12
ftoin rf25, rf25              ; nop
stvpmv 4, rf24                ; nop
stvpmv 5, rf25                ; nop
nop                           ; nop                         ; thrsw
nop                           ; nop
nop                           ; nop

# ---- fragment shaders ----------------------------------------------------------------------------------
# rf4 = W (the payload's rf3, copied); a varying = ldvary * W + C, C landing in rf0 one instruction
# after its ldvary (read two after). The varyings, in order: s t r g b a r2 g2 b2 a2 -> rf7..rf14 hold
# R G B A R2 G2 B2 A2; rf5: the ldvary's value, rf6: the product; rf15 the _AT threshold. The result:
# colour + colour2 (FS_COLOR) or texel * colour + colour2 (FS_TEX), each channel clamped to 1.

.name FS_COLOR frag
fmov rf4, rf3                 ; nop                         # W
nop                           ; nop                         ; ldvary.rf5      # s (unused)
nop                           ; nop
nop                           ; nop                         ; ldvary.rf5      # t (unused)
nop                           ; nop
nop                           ; nop                         ; ldvary.rf5      # r
nop                           ; fmul rf6, rf5, rf4
fadd rf7, rf6, rf0            ; nop                         ; ldvary.rf5      # R ; g
nop                           ; fmul rf6, rf5, rf4
fadd rf8, rf6, rf0            ; nop                         ; ldvary.rf5      # G ; b
nop                           ; fmul rf6, rf5, rf4
fadd rf9, rf6, rf0            ; nop                         ; ldvary.rf5      # B ; a
nop                           ; fmul rf6, rf5, rf4
fadd rf10, rf6, rf0           ; nop                         ; ldvary.rf5      # A ; r2
nop                           ; fmul rf6, rf5, rf4
fadd rf11, rf6, rf0           ; nop                         ; ldvary.rf5      # R2 ; g2
nop                           ; fmul rf6, rf5, rf4
fadd rf12, rf6, rf0           ; nop                         ; ldvary.rf5      # G2 ; b2
nop                           ; fmul rf6, rf5, rf4
fadd rf13, rf6, rf0           ; nop                         ; thrsw ; ldvary.rf5   # B2 ; a2
nop                           ; fmul rf6, rf5, rf4          ; thrsw           # (last segment)
fadd rf14, rf6, rf0           ; nop                         # A2
fadd rf7, rf7, rf11           ; nop
fadd rf8, rf8, rf12           ; nop
fadd rf9, rf9, rf13           ; nop
fadd rf10, rf10, rf14         ; nop
fmin rf7, rf7, 0x3f800000     ; nop
fmin rf8, rf8, 0x3f800000     ; nop
fmin rf9, rf9, 0x3f800000     ; nop
fmin rf10, rf10, 0x3f800000   ; nop
vfpack tlb, rf7, rf8          ; nop                         ; thrsw           # (end)
vfpack tlb, rf9, rf10         ; nop
nop                           ; nop

.name FS_COLOR_AT frag
fmov rf4, rf3                 ; nop                         # W
nop                           ; nop                         ; ldvary.rf5      # s (unused)
nop                           ; nop                         ; ldunifrf.rf15   # the alpha threshold
nop                           ; nop                         ; ldvary.rf5      # t (unused)
nop                           ; nop
nop                           ; nop                         ; ldvary.rf5      # r
nop                           ; fmul rf6, rf5, rf4
fadd rf7, rf6, rf0            ; nop                         ; ldvary.rf5      # R ; g
nop                           ; fmul rf6, rf5, rf4
fadd rf8, rf6, rf0            ; nop                         ; ldvary.rf5      # G ; b
nop                           ; fmul rf6, rf5, rf4
fadd rf9, rf6, rf0            ; nop                         ; ldvary.rf5      # B ; a
nop                           ; fmul rf6, rf5, rf4
fadd rf10, rf6, rf0           ; nop                         ; ldvary.rf5      # A ; r2
nop                           ; fmul rf6, rf5, rf4
fadd rf11, rf6, rf0           ; nop                         ; ldvary.rf5      # R2 ; g2
nop                           ; fmul rf6, rf5, rf4
fadd rf12, rf6, rf0           ; nop                         ; ldvary.rf5      # G2 ; b2
nop                           ; fmul rf6, rf5, rf4
fadd rf13, rf6, rf0           ; nop                         ; thrsw ; ldvary.rf5   # B2 ; a2
nop                           ; fmul rf6, rf5, rf4          ; thrsw           # (last segment)
fadd rf14, rf6, rf0           ; nop                         # A2
fadd rf7, rf7, rf11           ; nop
fadd rf8, rf8, rf12           ; nop
fadd rf9, rf9, rf13           ; nop
fadd rf10, rf10, rf14         ; nop
fmin rf7, rf7, 0x3f800000     ; nop
fmin rf8, rf8, 0x3f800000     ; nop
fmin rf9, rf9, 0x3f800000     ; nop
fmin rf10, rf10, 0x3f800000   ; nop
fsub.pushn -, rf10, rf15      ; nop                         # alpha < threshold: flag A
setmsf.ifa -, 0               ; nop                         # those pixels are not written
vfpack tlb, rf7, rf8          ; nop                         ; thrsw           # (end)
vfpack tlb, rf9, rf10         ; nop
nop                           ; nop

# the texture: uniforms p0 (texture state | 3 = two words returned: RG, BA as f16), p1 (sampler
# state), written (wrtmuc) between the T and the S coordinates, as Mesa does; _AT: the threshold.
# rf1 S then the texel's R G, rf2 T then its B A (4.2's r1 r0 / r2); rf5 the ldvary's value, rf6 the product.
.name FS_TEX frag
fmov rf4, rf3                 ; nop                         # W
nop                           ; nop                         ; ldvary.rf5           # s
nop                           ; fmul rf1, rf5, rf4
fadd rf1, rf1, rf0            ; nop                         ; ldvary.rf5           # S ; t
nop                           ; fmul rf2, rf5, rf4
fadd rf2, rf2, rf0            ; nop                         ; ldvary.rf5           # T ; r
nop                           ; mov tmut, rf2               # T (Mesa's order: T, the configs, S)
nop                           ; fmul rf6, rf5, rf4          ; wrtmuc               # p0
fadd rf7, rf6, rf0            ; nop                         ; ldvary.rf5 ; wrtmuc  # R ; g ; p1
nop                           ; mov tmus, rf1               # S: the lookup starts
nop                           ; fmul rf6, rf5, rf4
fadd rf8, rf6, rf0            ; nop                         ; ldvary.rf5           # G ; b
nop                           ; fmul rf6, rf5, rf4
fadd rf9, rf6, rf0            ; nop                         ; ldvary.rf5           # B ; a
nop                           ; fmul rf6, rf5, rf4
fadd rf10, rf6, rf0           ; nop                         ; ldvary.rf5           # A ; r2
nop                           ; fmul rf6, rf5, rf4
fadd rf11, rf6, rf0           ; nop                         ; ldvary.rf5           # R2 ; g2
nop                           ; fmul rf6, rf5, rf4
fadd rf12, rf6, rf0           ; nop                         ; ldvary.rf5           # G2 ; b2
nop                           ; fmul rf6, rf5, rf4
fadd rf13, rf6, rf0           ; nop                         ; ldvary.rf5           # B2 ; a2
nop                           ; fmul rf6, rf5, rf4          ; thrsw                # (wait for the TMU)
fadd rf14, rf6, rf0           ; nop                         ; thrsw                # A2 (last segment)
nop                           ; nop
nop                           ; nop                         ; ldtmu.rf1            # texel R G
nop                           ; fmul rf5, rf1.l, rf7        ; ldtmu.rf2            # R ; texel B A
fadd rf5, rf5, rf11           ; fmul rf6, rf1.h, rf8        # R + R2 ; G
fadd rf6, rf6, rf12           ; fmul rf7, rf2.l, rf9        # G + G2 ; B
fadd rf7, rf7, rf13           ; fmul rf8, rf2.h, rf10       # B + B2 ; A
fadd rf8, rf8, rf14           ; nop                         # A + A2
fmin rf5, rf5, 0x3f800000     ; nop
fmin rf6, rf6, 0x3f800000     ; nop
fmin rf7, rf7, 0x3f800000     ; nop
fmin rf8, rf8, 0x3f800000     ; nop
vfpack tlb, rf5, rf6          ; nop                         ; thrsw                # (end)
vfpack tlb, rf7, rf8          ; nop
nop                           ; nop

.name FS_TEX_AT frag
fmov rf4, rf3                 ; nop                         # W
nop                           ; nop                         ; ldvary.rf5           # s
nop                           ; fmul rf1, rf5, rf4
fadd rf1, rf1, rf0            ; nop                         ; ldvary.rf5           # S ; t
nop                           ; fmul rf2, rf5, rf4
fadd rf2, rf2, rf0            ; nop                         ; ldvary.rf5           # T ; r
nop                           ; mov tmut, rf2               # T (Mesa's order: T, the configs, S)
nop                           ; fmul rf6, rf5, rf4          ; wrtmuc               # p0
fadd rf7, rf6, rf0            ; nop                         ; ldvary.rf5 ; wrtmuc  # R ; g ; p1
nop                           ; mov tmus, rf1               # S: the lookup starts
nop                           ; fmul rf6, rf5, rf4          ; ldunifrf.rf15        # the alpha threshold
fadd rf8, rf6, rf0            ; nop                         ; ldvary.rf5           # G ; b
nop                           ; fmul rf6, rf5, rf4
fadd rf9, rf6, rf0            ; nop                         ; ldvary.rf5           # B ; a
nop                           ; fmul rf6, rf5, rf4
fadd rf10, rf6, rf0           ; nop                         ; ldvary.rf5           # A ; r2
nop                           ; fmul rf6, rf5, rf4
fadd rf11, rf6, rf0           ; nop                         ; ldvary.rf5           # R2 ; g2
nop                           ; fmul rf6, rf5, rf4
fadd rf12, rf6, rf0           ; nop                         ; ldvary.rf5           # G2 ; b2
nop                           ; fmul rf6, rf5, rf4
fadd rf13, rf6, rf0           ; nop                         ; ldvary.rf5           # B2 ; a2
nop                           ; fmul rf6, rf5, rf4          ; thrsw                # (wait for the TMU)
fadd rf14, rf6, rf0           ; nop                         ; thrsw                # A2 (last segment)
nop                           ; nop
nop                           ; nop                         ; ldtmu.rf1            # texel R G
nop                           ; fmul rf5, rf1.l, rf7        ; ldtmu.rf2            # R ; texel B A
fadd rf5, rf5, rf11           ; fmul rf6, rf1.h, rf8        # R + R2 ; G
fadd rf6, rf6, rf12           ; fmul rf7, rf2.l, rf9        # G + G2 ; B
fadd rf7, rf7, rf13           ; fmul rf8, rf2.h, rf10       # B + B2 ; A
fadd rf8, rf8, rf14           ; nop                         # A + A2
fmin rf5, rf5, 0x3f800000     ; nop
fmin rf6, rf6, 0x3f800000     ; nop
fmin rf7, rf7, 0x3f800000     ; nop
fmin rf8, rf8, 0x3f800000     ; nop
fsub.pushn -, rf8, rf15       ; nop                         # alpha < threshold: flag A
setmsf.ifa -, 0               ; nop                         # those pixels are not written
vfpack tlb, rf5, rf6          ; nop                         ; thrsw                # (end)
vfpack tlb, rf7, rf8          ; nop
nop                           ; nop
