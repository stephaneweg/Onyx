//
// qpusim.h -- a functional V3D 4.2 / 7.1 QPU simulator (PC only), for testing the shaders the apps
// generate (user/Libs/v3d, the GameCube's TEV) and the kernel's (kernel/sys/v3d_shaders*.qasm) without
// a Raspberry Pi: 16 lanes (pixels, or vertices) run one shader, as a QPU does. The instructions are
// decoded by Mesa (tools/qpu/mesa).
//
// V3D 7.1 (Run::version 71, the Pi 5): no accumulators, each input its own read address or the small
// immediate, ldunif / ldvary's C term -> rf0, the SFU as add operations, the payload W in rf3.
// Vertex shaders (runVertex): ldvpmv_in reads Pixel::vpmIn, stvpmv writes Pixel::vpmOut, vpmwt nothing.
//
// What is modelled: the accumulators r0..r5 and the register file (64), both ALUs reading before
// either writes, the add / mul operations the shaders use (float, integer, f16 pack / unpack,
// conversions), the flags (push Z / N / C, conditions ifa / ifb / ifna / ifnb), the signals
// ldvary (the varying -> rN, its C term -> r5 two instructions later), ldunif / ldunifrf,
// wrtmuc (the TMU configuration from the uniforms), ldtmu, thrsw (no threads: nothing), the SFU
// (recip, rsqrt, exp, log, sin -> r4 three instructions later), the TMU (a 2D RGBA8 texture:
// nearest or bilinear, repeat / clamp / mirror; the result as f16 pairs RG, BA), the TLB colour
// writes (f16 pairs -> RGBA8) and the multisample flags (setmsf: a pixel not written).
// The accumulators are garbage after a thread switch (as on the hardware). Not modelled: timing,
// threads, the flag updates (andz ...), branches, VPM, general TMU access.
//
#ifndef QPUSIM_H
#define QPUSIM_H
#include <stdint.h>
#include <vector>
#include <map>
#include <string>

namespace qpusim
{

struct Texture
{
	int w, h;
	std::vector<uint32_t> px;		// 0xAABBGGRR as the GPU reads it: R in the low byte (the kernel's layout)
	int wrapS, wrapT;			// 0 repeat, 1 clamp, 2 mirror
	bool linear;
};

struct Pixel
{
	std::vector<float> vary;		// the varyings in the order the shader reads them (p: ldvary gives p; with W and C below)
	std::vector<uint32_t> vpmIn, vpmOut;	// a vertex shader's VPM input (by index) and output
	float w = 1.0f;				// the payload's W (a varying = ldvary * W + C)
	std::vector<float> varyC;		// each varying's C term (absent: 0)
	uint32_t rgba;				// out: 0xAABBGGRR (R in the low byte), valid when written
	bool written;
	uint32_t tlbWords[4]; int nTlb;		// out: the raw TLB writes
};

struct Run
{
	std::vector<uint32_t> uniforms;
	std::map<uint32_t, Texture> textures;	// by the value of the uniform p0 & ~15 (the texture state's address)
	std::string error;			// set when the program does something not modelled
	struct Watch { int ip, reg; long long lo, hi; };	// (tests: after instruction ip, register reg -- 0..5 r0..r5,
	std::vector<Watch> watch;			//  6 + n rf n -- must be in lo..hi as a signed integer, else an error)
	long instructions;
	int version = 42;			// the QPU: 42 (V3D 4.2) or 71 (V3D 7.1)
};

// runs the fragment shader words[n] over the pixels (16 a group); false: error in run.error
bool runFragment (const uint64_t *words, int n, std::vector<Pixel> &pixels, Run &run);
// runs the vertex (or coordinate) shader words[n] over the vertices (vpmIn -> vpmOut)
bool runVertex (const uint64_t *words, int n, std::vector<Pixel> &vertices, Run &run);

// f16 helpers (also for the tests' references)
uint16_t toHalf (float f);
float fromHalf (uint16_t h);

} // namespace qpusim
#endif
