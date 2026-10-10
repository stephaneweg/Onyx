//
// v3d/picavs.h -- a PICA200 vertex shader specialized for a draw (the 3DS core's Spec: a straight list of
// instructions on a vertex's values, guards for the blocks a vertex's comparison decides -- docs/3DS-EMULATOR-STUDY.md
// 9r, 9t) as a V3D vertex or coordinate shader, built with qpu.h. The core's list is copied into the plain
// structures below (this library knows nothing of the core).
//
// The program reads its vertex from the VPM -- the components of the input registers it uses, in the order
// build () says (In) --, its constants and the view's values from the uniform stream in the order build () says
// (Uni), and writes what the caller asks, in order (Write): raw components of the shader's output registers, or
// the position as a V3D vertex shader gives it (Xs Ys Zs 1/Wc) or a coordinate shader (Xc Yc Zc Wc Xs Ys).
// Everything not needed for what is written is left out (a coordinate shader is the position's chain alone).
//
// Each value lives in a register of the QPU's file while it is needed: a list with more values alive at once than
// the file holds is refused (Result::ok false) -- the draw then stays on the processor.
//
// The arithmetic is the QPU's: a product with a zero operand and an infinite one is not forced to 0 as the
// PICA does, the reciprocal and the inverse square root are the SFU's.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener (see docs/LICENSING.md)
//
#ifndef _v3d_picavs_h
#define _v3d_picavs_h
#include "v3d/qpu.h"

namespace picavs
{
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

// An instruction of the list. op: the PICA's (0x00 ADD, 0x01 DP3, 0x02 DP4, 0x03 / 0x18 DPH, 0x05 EX2, 0x06 LG2,
// 0x08 MUL, 0x09 / 0x1A SGE, 0x0A / 0x1B SLT, 0x0B FLR, 0x0C MAX, 0x0D MIN, 0x0E RCP, 0x0F RSQ, 0x13 MOV; shape 2:
// MAD), or one of the list's own below. d: the register written (dOut: an output 0..15, else a register 0..31),
// writes: its components (x = 8). An operand k: a register's components through sw[k] (the component each of the
// four positions takes), negated or not -- or a constant (kind 1: slot[k], four floats as the operand's positions).
enum { OP_PUT = 0xFE, OP_CMP = 0xFD, OP_GUARD = 0xFC, OP_FLAG = 0xFB };
// OP_PUT: the constant slot[0]'s components into d's. OP_CMP: the two comparison results from operands 0 and 1,
// positions x and y (slot[2]: x's code << 3 | y's -- 0 ==, 1 !=, 2 <, 3 <=, 4 >, 5 >=, else true).
// OP_GUARD: guard d = guard reg[0] (0: none) and the results read as a branch does (reg[1]: 0 x or y, 1 x and y,
// 2 x, 3 y; reg[2]: bit 1 the value x must have, bit 0 y's), the opposite when shape is 1.
// OP_FLAG: result d (0 x, 1 y) = reg[0].  guard: the instruction is done where that guard says yes (0: always).
struct Op
{
	u8 op, shape, d, writes, dOut, guard;
	u8 kind[3], reg[3]; u16 slot[3];
	u8 sw[3][4], neg[3];
};
struct List { const Op *ops; u32 nOps; };

// What the program writes: VPM output i is write i.
enum { W_RAW, W_ONE_MINUS, W_XS, W_YS, W_ZS, W_RW, W_XC, W_YC, W_ZC, W_WC };
struct Write { u8 kind, reg, comp; };		// RAW / ONE_MINUS: output register reg's component comp (0 x); the others: of the position in output register reg
// The uniforms it reads, in order: a list constant's component, or one of the view's values --
// the clip position is (x KX + w CX, y KY + w CY, z KZ + w CZ, w); on the screen x / w * SX and y / w * SY
// (rounded to the V3D's 24.8), z / w * SZ + OZ.
enum { U_CONST, U_VIEW };
enum { V_KX, V_CX, V_KY, V_CY, V_KZ, V_CZ, V_SX, V_SY, V_SZ, V_OZ, V_COUNT };
struct Uni { u8 kind, comp; u16 slot; };		// U_CONST: slot, comp; U_VIEW: slot = V_*
// The VPM inputs it reads: input i is the input register's component.
struct In { u8 reg, comp; };

struct Result { u32 nUni, nIn; bool ok; const char *why; int values, regsPeak; };

// The program for the version set (qpu::setVersion). -> false (r.why says): an instruction it does not do, too
// many values alive at once, more uniforms or inputs than there is room for.
bool build (qpu::Prog &p, const List &l, const Write *w, u32 nw, Uni *uni, u32 uniMax, In *in, u32 inMax, Result &r);
}

#endif
