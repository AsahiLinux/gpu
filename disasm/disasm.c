/*
 * Copyright (C) 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>

/* Opcode table? Speculative since I don't know the opcode size yet, but this
 * should help bootstrap... These opcodes correspond to the bottom 7-bits of
 * the first byte, with the 8th bit from the 8th bit of the *second* byte. This
 * is still a guess. */

enum agx_opcodes {
	OPC_FFMA_CMPCT_16 = 0x36,
	OPC_FFMA_CMPCT_SAT_16 = 0x76,
	OPC_FMUL_16 = 0x96,
	OPC_FADD_16 = 0xA6,
	OPC_FFMA_16 = 0xB6,
	OPC_FMUL_SAT_16 = 0xD6,
	OPC_FADD_SAT_16 = 0xE6,
	OPC_FFMA_SAT_16 = 0xF6,

	OPC_FROUND_32 = 0x0A,
	OPC_FFMA_CMPCT_32 = 0x3A,
	OPC_FFMA_CMPCT_SAT_32 = 0x7A,
	OPC_FMUL_32 = 0x9A,
	OPC_FADD_32 = 0xAA,
	OPC_FFMA_32 = 0xBA,
	OPC_FMUL_SAT_32 = 0xDA,
	OPC_FADD_SAT_32 = 0xEA,
	OPC_FFMA_SAT_32 = 0xFA,

	OPC_IADD = 0x0E,
	OPC_IMAD = 0x1E,
	OPC_ISHL = 0x2E,
	/* 0x3e seen with reverse_bits, and used in clz */
	OPC_IADDSAT = 0x4E,
	OPC_ISHR = 0xAE,
	OPC_I2F = 0xBE,

	OPC_LOAD = 0x05, // todo
	OPC_STORE = 0x45, // todo
	OPC_FCSEL = 0x02,
	OPC_ICSEL = 0x12,
	OPC_MOVI = 0x62,
	OPC_LD_COMPUTE = 0x72,
	OPC_BITOP = 0x7E,
	OPC_WAIT = 0x38, // seen after loads?
	OPC_STOP = 0x08,

	OPC_LD_VAR_NO_PERSPECTIVE = 0xA1,
	OPC_LD_VAR = 0xE1, // perspective
	OPC_ST_VAR = 0x11,
	OPC_UNKB1 = 0xB1, // seen in aux frag shader
	OPC_UNK48 = 0x48, // seen before blending
	OPC_BLEND = 0x09,

	// branching instructions, not understood
	OPC_UNKD2 = 0xD2,
	OPC_UNK42 = 0x42,
	OPC_UNK52 = 0x52,

	// not sure what this does, but appears to be 4 bytes
	OPC_UNK80 = 0x80,
};

#define I 0
#define C 1

static struct {
	const char *name;
	unsigned size;
	bool complete;
} agx_opcode_table[256] = {
	[OPC_FADD_16] = { "fadd.16", 6, I },
	[OPC_FADD_SAT_16] = { "fadd.sat.16", 6, I },
	[OPC_FMUL_16] = { "fmul.16", 6, I },
	[OPC_FMUL_SAT_16] = { "fmul.sat.16", 6, I },
	[OPC_FFMA_CMPCT_16] = { "ffma.cmpct.16", 6, I },
	[OPC_FFMA_CMPCT_SAT_16] = { "ffma.cmpct.sat.16", 6, I },
	[OPC_FFMA_16] = { "ffma.16", 8, I },
	[OPC_FFMA_SAT_16] = { "ffma.sat.16", 8, I },

	[OPC_FROUND_32] = { "fround.32", 6, I },
	[OPC_FADD_32] = { "fadd.32", 6, C },
	[OPC_FADD_SAT_32] = { "fadd.sat.32", 6, C },
	[OPC_FMUL_32] = { "fmul.32", 6, C },
	[OPC_FMUL_SAT_32] = { "fmul.sat.32", 6, C },
	[OPC_FFMA_32] = { "ffma.32", 8, I },
	[OPC_FFMA_SAT_32] = { "ffma.sat.32", 8, I },
	[OPC_FFMA_CMPCT_32] = { "ffma.cmpct.32", 6, I },
	[OPC_FFMA_CMPCT_SAT_32] = { "ffma.cmpct.sat.32", 6, I },

	[OPC_I2F] = { "i2f", 6, I },
	[OPC_IADD] = { "iadd", 8, I },
	[OPC_IMAD] = { "imad", 8, I },
	[OPC_ISHL] = { "ishl", 8, I },
	[OPC_IADDSAT] = { "iaddsat", 8, I },
	[OPC_ISHR] = { "ishr", 8, I },

	[OPC_LOAD] = { "load", 8, I },
	[OPC_LD_VAR_NO_PERSPECTIVE] = { "ld_var.no_perspective", 8, I },
	[OPC_LD_VAR] = { "ld_var", 8, I },
	[OPC_UNKB1] = { "unkb1", 10, I },
	[OPC_STORE] = { "store", 8, I },
	[OPC_ST_VAR] = { "st_var", 4, C },
	[OPC_FCSEL] = { "fcsel", 8, I },
	[OPC_ICSEL] = { "icsel", 8, I },
	[OPC_MOVI] = { "movi", 4, C },
	[OPC_LD_COMPUTE] = { "ld_compute", 4, C },
	[OPC_BITOP] = { "bitop", 6, I },
	[OPC_BLEND] = { "blend", 8, I },
	[OPC_STOP] = { "stop", 4, I },

	[OPC_WAIT] = { "wait", 2, I },
	[OPC_UNK48] = { "unk48", 4, I },
	[OPC_UNK42] = { "unk42", 6, I },
	[OPC_UNK52] = { "unk52", 6, I },
	[OPC_UNK80] = { "unk80", 4, I },
	[OPC_UNKD2] = { "unkD2", 12, I },
};

#undef I
#undef C

static unsigned
agx_instr_bytes(uint8_t opc, uint8_t reg)
{
	/* For immediate moves, 32-bit immediates are larger */
	if (opc == OPC_MOVI && (reg & 0x1))
		return 6;
	else
		return agx_opcode_table[opc].size ?: 2;
}

/* Print float src, including modifiers */

struct agx_src {
	unsigned type : 2;
	unsigned reg;
	bool size32;
	bool abs;
	bool neg;
	unsigned unk;
};

static void
agx_print_src(FILE *fp, struct agx_src s)
{
	/* Known source types: immediates (8-bit only?), constant memory
	 * (indexing 64-bits at a time from preloaded memory), and general
	 * purpose registers */
	const char *types[] = { "#", "unk1:", "u", "" };

	fprintf(fp, ", %s%u%s%s%s%s",
			types[s.type], s.reg,
			(s.size32 || s.type == 0) ? "" : ((s.reg & 1) ? "h" : "l"),
			s.abs ? ".abs" : "", s.neg ? ".neg" : "",
			s.unk ? ".unk" : "");
}

static void
agx_print_float_src(FILE *fp, unsigned type, unsigned reg, bool size32, bool abs, bool neg)
{
	assert(type <= 3);
	agx_print_src(fp, (struct agx_src) {
			.type = type, .reg = reg, .size32 = size32,
			.abs = abs, .neg = neg
		});
}

/* Decode 12-bit packed float source */
static struct agx_src
agx_decode_float_src(uint16_t packed)
{
	return (struct agx_src) {
		.reg = (packed & 0x3F),
		.type = (packed & 0xC0) >> 6,
		.unk = (packed & 0x100),
		.size32 = (packed & 0x200),
		.abs = (packed & 0x400),
		.neg = (packed & 0x800),
	};
}

/* When we know more how the encodings relate to each other, these
 * per-instruction prints will hopefully disappear, assuming things are
 * sufficiently regular.
 *
 * fadd.f32 is 6 bytes. First two bytes are used for opcode/destination, so we
 * have 32-bits to decode here, or 16-bits per source. Since a register is at
 * least 6-bits, 2-bit type, 3-bits widen, that leaves only 10-bits unaccounted
 * for in the instruction.
 *
 * Byte 0: [2 - src0 type][6 - src0 value]
 * Byte 1: [4 - src1 value lo][1 - neg][1 - abs][1 - size][1 - unk]
 * Byte 2: [1 - neg][1 - abs][1 - size][1 - unk][2 - src1 type][2 - src1 value hi]
 * Byte 3: [8 - zero]
 *
 */

static void
agx_print_fadd_f32(FILE *fp, uint8_t *code)
{
	agx_print_src(fp, agx_decode_float_src(code[2] | ((code[3] & 0xF) << 8)));
	agx_print_src(fp, agx_decode_float_src((code[3] >> 4) | (code[4] << 4)));

	if (code[5])
		fprintf(fp, " /* unk5 = %02X */", code[5]);
}

static void
agx_print_ld_compute(uint8_t *code, FILE *fp)
{
	/* 4 bytes, first 2 used for opcode and dest reg, next few bits for the
	 * component, the rest is a selector for what to load */
	uint16_t arg = code[2] | (code[3] << 8);

	unsigned component = arg & 0x3;
	uint16_t selector = arg >> 2;

	fprintf(fp, ", ");

	switch (selector) {
	case 0x00:
		fprintf(fp, "[threadgroup_position_in_grid]");
		break;
	case 0x0c:
		fprintf(fp, "[thread_position_in_threadgroup]");
		break;
	case 0x0d:
		fprintf(fp, "[thread_position_in_simdgroup]");
		break;
	case 0x104:
		fprintf(fp, "[thread_position_in_grid]");
		break;
	default:
		fprintf(fp, "[unk_%X]", selector);
		break;
	}

	fprintf(fp, ".%c", "xyzw"[component]);
}

static void
agx_print_bitop_src(uint16_t value, FILE *fp)
{
	/* different encoding from float srcs -- slightly smaller */
	uint16_t mode = (value >> 6) & 0x0f;
	uint16_t v = (value & 0x3f) | ((value >> 4) & 0xc0);

	switch (mode) {
	case 0x0:
		// 8-bit immediate
		fprintf(fp, "#0x%x", v);
		break;
	case 0x3:
		// 16b register
		fprintf(fp, "h%d", v);
		break;
	case 0xb:
		// 32b register
		assert((v&1) == 0);
		fprintf(fp, "w%d", v >> 1);
		break;
	default:
		fprintf(fp, "unk_%x", value);
		break;
	}
}

static void
agx_print_bitop(uint8_t *code, FILE *fp)
{
	/* 6 bytes */
	/* Universal bitop instruction. Control bits express operation as
	 * sum-of-products: a&b, ~a&b, a&~b, ~a&~b */

	/* XXX: dst encoding may not be quite correct either, but is done
	 * in common code before this point */
	/* XXX: disassemble to "friendly" pseudoop ? */

	uint8_t control = (code[3] >> 2) & 0x3;
	control |= (code[4] >> 4) & 0xc; 
	fprintf(fp, ", #0x%x, ", control);

	uint16_t src1_bits = code[2] | ((uint16_t)(code[3]&3) << 8) |
		((uint16_t)code[5]&0xc)<<8;
	uint16_t src2_bits = (code[3] >> 4) | (((uint16_t)code[4]&0x3f)<<4) |
		(((uint16_t)code[5]&0x3)<<10);

	agx_print_bitop_src(src1_bits, fp);
	fprintf(fp, ", ");
	agx_print_bitop_src(src2_bits, fp);
}

static float
agx_decode_float_imm8(uint16_t src)
{
	float sign = (src & 0x80) ? -1.0f : 1.0f;
	int e = ((src & 0x70) >> 4);

	if (e == 0) {
		/* denorm */
		return sign * (src & 0x0f) / 64.0f;
	}
	else {
		return sign * ldexpf((src & 0x0f) | 0x10, e - 7);
	}
}

static void
agx_print_fp16_src(uint16_t src, uint16_t type, FILE *fp)
{
	/* XXX: type&2 bit may be something odd like code[0]&0x80 */

	switch (type & 5) {
	case 0x0:
		/* packed float8 immediate */
		fprintf(fp, "#%ff", agx_decode_float_imm8(src));
		break;
	case 0x1:
		/* half register */
		fprintf(fp, "h%d", src);
		break;
	case 0x4:
	case 0x5:
		/* constant space; extra bit packed in
		 * bottom bit of type */
		fprintf(fp, "const_%d", ((type&1)<<8) | src);
		break;
	default:
		fprintf(fp, "unk_%x:%x", type, src);
		break;
	}

	if (type & 0x8)
		fprintf(fp, ".abs");
	if (type & 0x10)
		fprintf(fp, ".neg");

}

static void
agx_print_fadd16(uint8_t *code, FILE *fp)
{
	/* 6 bytes */
	uint16_t src1 = (code[2] & 0x3f) | ((code[5] & 0x0c)<<4);
	uint16_t type1 = (code[2] >> 6) | ((code[3] & 0x0f)<<2);

	uint16_t src2 = (code[3] >> 4) | ((code[4] & 0x3)<<4) | ((code[5] & 0x3)<<6);
	uint16_t type2 = (code[4] >> 2);

	fprintf(fp, ", ");
	agx_print_fp16_src(src1, type1, fp);
	fprintf(fp, ", ");
	agx_print_fp16_src(src2, type2, fp);
}

static void
agx_print_st_var(uint8_t *code, FILE *fp)
{
	/* 4 bytes, first for opcode. Second for source register  third
	 * indicates the destination, fourth unknown */
	if (code[1] & 0x1)
		fprintf(fp, ".unk");

	fprintf(fp, ", index:%u", code[2] & 0xF);

	if ((code[2] & 0xF0) != 0x80)
		fprintf(fp, ", unk2=%X", code[2] >> 4);

	if (code[3] != 0x80)
		fprintf(fp, ", unk3=%X", code[3]);
}

/* Disassembles a single instruction */

unsigned
agx_disassemble_instr(uint8_t *code, bool *stop, bool verbose, FILE *fp)
{
	/* Decode the opcode first, requires 2 bytes */
	uint8_t opc = (code[0] & 0x7F) | (code[1] & 0x80);

	/* Guess the size */
	unsigned bytes = agx_instr_bytes(opc, code[1]);

	/* Hexdump the instruction */

	if (verbose || !agx_opcode_table[opc].complete) {
		fprintf(fp, "#");
		for (unsigned i = 0; i < bytes; ++i)
			fprintf(fp, " %02X", code[i]);
		fprintf(fp, "\n");
	}

	unsigned op_unk80 = code[0] & 0x80; /* XXX: what is this? */
	fprintf(fp, "%c", op_unk80 ? '+' : '-'); /* Stay concise.. */

	if (agx_opcode_table[opc].name)
		fputs(agx_opcode_table[opc].name, fp);
	else
		fprintf(fp, "op_%02X", opc);

	if (opc == OPC_ICSEL) {
		unsigned mode = (code[7] & 0xF0) >> 4;
		if (mode == 0x1)
			fprintf(fp, ".eq"); // output 16-bit bool
		else if (mode == 0x2)
			fprintf(fp, ".imin");
		else if (mode == 0x3)
			fprintf(fp, ".ult"); // output 16-bit bool
		else if (mode == 0x4)
			fprintf(fp, ".imax");
		else if (mode == 0x5)
			fprintf(fp, ".ugt"); // output 16-bit bool
		else
			fprintf(fp, ".unk%X", mode);
	} else if (opc == OPC_FCSEL) {
		unsigned mode = (code[7] & 0xF0) >> 4;

		if (mode == 0x6)
			fprintf(fp, ".fmin");
		else if (mode == 0xE)
			fprintf(fp, ".fmax");
		else
			fprintf(fp, ".unk%X", mode);
	}

	/* Decode destination register, common to all ALUs (and maybe more?) */
	uint8_t dest = code[1];
	bool dest_32 = dest & 0x1; /* clear for 16-bit */
	unsigned dest_reg = (dest >> 1) & 0x3F;

	/* Maybe it's a 32-bit opcode */
	if (opc == OPC_ST_VAR)
		dest_32 = !dest_32;

	fprintf(fp, " %s%u",
			dest_32 ? "w" : "h",
			dest_reg);

	/* Decode other stuff, TODO */
	switch (opc) {
	case OPC_ST_VAR:
		agx_print_st_var(code, fp);
		break;
	case OPC_LD_COMPUTE:
		agx_print_ld_compute(code, fp);
		break;
	case OPC_BITOP:
		agx_print_bitop(code, fp);
		break;
	case OPC_FADD_16:
	case OPC_FADD_SAT_16:
	case OPC_FMUL_16:
	case OPC_FMUL_SAT_16:
		agx_print_fadd16(code, fp);
		break;
	case OPC_MOVI: {
		uint32_t imm = code[2] | (code[3] << 8);

		if (dest_32)
			imm |= (code[4] << 16) | (code[5] << 24);

		fprintf(fp, ", #0x%X", imm);
		break;
	}
	case OPC_FADD_32:
	case OPC_FADD_SAT_32:
	case OPC_FMUL_32:
	case OPC_FMUL_SAT_32:
		agx_print_fadd_f32(fp, code);
		break;
	default: {
		/* Make some guesses */
		bool iadd = opc == OPC_IADD;

		if (bytes > 2) {
			agx_print_float_src(fp,
				(code[2] & 0xC0) >> 6,
				(code[2] & 0x3F) |
					(iadd ? ((code[5] & 0x0C) << 4) : 0),
				       	// TODO: why overlap?
				code[3] & 0x20,
				code[3] & 0x04,
				code[3] & 0x08);

			agx_print_float_src(fp,
				(code[4] & 0x0C) >> 2,
				((code[3] >> 4) & 0xF) | ((code[4] & 0x3) << 4) | ((code[7] & 0x3) << 6),
				code[4] & 0x20,
				code[4] & 0x40,
				code[4] & 0x80);
		}

		if (bytes > 6 && !iadd) {
			agx_print_float_src(fp,
				(code[5] & 0xC0) >> 6,
				(code[5] & 0x3F) | (code[6] & 0xC0),
				code[6] & 0x20,
				code[6] & 0x04,
				code[6] & 0x08);
		}

		break;
	}
	}

	fprintf(fp, "\n");

	if (code[0] == (OPC_STOP | 0x80))
		*stop = true;

	return bytes;
}

/* Disassembles a shader */

void
agx_disassemble(void *_code, size_t maxlen, FILE *fp)
{
	if (maxlen > 256)
		maxlen = 256;

	uint8_t *code = _code;

	bool stop = false;
	unsigned bytes = 0;
	bool verbose = getenv("ASAHI_VERBOSE") != NULL;

	while((bytes + 8) < maxlen && !stop)
		bytes += agx_disassemble_instr(code + bytes, &stop, verbose, fp);

	if (!stop)
		fprintf(fp, "// error: stop instruction not found\n");
}
