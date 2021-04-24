#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <inttypes.h>
#include <time.h>
#include "tiling.h"
#include "demo.h"
#include "util.h"
#include "../agx_pack.h"

#define WIDTH 800
#define HEIGHT 600

static uint64_t
demo_zero(struct agx_allocator *allocator, size_t count)
{
	struct agx_ptr ptr = agx_allocate(allocator, count);
	memset(ptr.map, 0, count);
	return ptr.gpu_va;
}

float t = 0.0;

static uint64_t
demo_viewport(struct agx_allocator *allocator)
{
	struct agx_ptr t = agx_allocate(allocator, AGX_VIEWPORT_LENGTH);
	bl_pack(t.map, VIEWPORT, cfg) {
		cfg.translate_x = WIDTH / 2;
		cfg.scale_x = WIDTH / 2;
		cfg.translate_y = HEIGHT / 2;
		cfg.scale_y = -(HEIGHT / 2);
		cfg.near_z = 0.0f;
		cfg.far_z = 1.0f;
	};

	return t.gpu_va;
}

struct agx_allocation texture_payload;
unsigned tex_width = 300, tex_height = 198;

static uint64_t
demo_texture(struct agx_allocator *allocator)
{
	struct agx_ptr t = agx_allocate(allocator, AGX_TEXTURE_LENGTH);
	assert((texture_payload.gpu_va & 0xFF) == 0);
	bl_pack(t.map, TEXTURE, cfg) {
		cfg.format = 0xa22;
		cfg.swizzle_r = AGX_CHANNEL_1;
		cfg.swizzle_g = AGX_CHANNEL_G;
		cfg.swizzle_b = AGX_CHANNEL_B;
		cfg.swizzle_a = AGX_CHANNEL_A;
		cfg.width = tex_width;
		cfg.height = tex_height;
		cfg.depth = 1;
		cfg.unk_1 = (texture_payload.gpu_va >> 8); // a nibble enabling compression seems to be mixed in here
		cfg.unk_2 = 0x20000;
	};

	return t.gpu_va;
}

static uint64_t
demo_sampler(struct agx_allocator *allocator)
{
	struct agx_ptr t = agx_allocate(allocator, AGX_SAMPLER_LENGTH);
	bl_pack(t.map, SAMPLER, cfg) {
		cfg.wrap_s = AGX_WRAP_CLAMP_TO_EDGE;
		cfg.wrap_t = AGX_WRAP_CLAMP_TO_EDGE;
		cfg.wrap_r = AGX_WRAP_CLAMP_TO_EDGE;
		cfg.magnify_linear = true;
		cfg.minify_linear = true;
		cfg.compare_func = AGX_COMPARE_FUNC_NEVER;
	};

	uint64_t *m = (uint64_t *) (t.map + AGX_SAMPLER_LENGTH);
	m[3] = 0x40; // XXX

	return t.gpu_va;
}

/* FP16 */
static uint64_t
demo_clear_color(struct agx_allocator *allocator)
{
	__fp16 colour[] = {
		0.1f, 0.1f, 0.1f, 1.0f
	};

	return agx_upload(allocator, colour, sizeof(colour));
}

static uint64_t
demo_render_target(struct agx_allocator *allocator, struct agx_allocation *framebuffer)
{
	struct agx_ptr t = agx_allocate(allocator, AGX_RENDER_TARGET_LENGTH);
	bl_pack(t.map, RENDER_TARGET, cfg) {
		cfg.unk_0 = 0xa22;
		cfg.swizzle_r = AGX_CHANNEL_B;
		cfg.swizzle_g = AGX_CHANNEL_G;
		cfg.swizzle_b = AGX_CHANNEL_R;
		cfg.swizzle_a = AGX_CHANNEL_A;
		cfg.width = WIDTH;
		cfg.height = HEIGHT;
		cfg.buffer = framebuffer->gpu_va;
		cfg.unk_100 = 0x1000000;
	};

	return t.gpu_va;
}

/* Fed into fragment writeout */
static uint64_t
demo_unk0_5(struct agx_allocator *allocator)
{
	uint32_t unk[] = { 0, ~0 };
	return agx_upload(allocator, unk, sizeof(unk));
}

static uint64_t
make_ptr40(uint8_t tag0, uint8_t tag1, uint8_t tag2, uint64_t ptr)
{
	assert(ptr < (1ull << 40));

	return (tag0 << 0) | (tag1 << 8) | (tag2 << 16) | (ptr << 24);
}

static uint64_t
demo_launch_fragment(struct agx_allocator *allocator, struct agx_allocation *fsbuf, struct agx_allocator *shaders)
{
	/* Varying descriptor */
	uint8_t unk_aux0[] = {
		0x03, 0x03, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x1D, 0x01, 0x01, 0x00,
		0x03, 0x03, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x1D, 0x01, 0x01, 0x00,
		0x03, 0x03, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x1D, 0x01, 0x01, 0x00,

		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00,

#if 0
		0x05, 0x05, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x1F, 0x01, 0x01, 0x00,
		0x05, 0x05, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x1F, 0x01, 0x01, 0x00,
		0x05, 0x05, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x1F, 0x01, 0x01, 0x00,

		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
#endif

#if 0
	       0x02, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
	       0x02, 0x01, 0x00, 0x00, 0x18, 0x01, 0x00, 0x00,
	       0x02, 0x01, 0x00, 0x00, 0x08, 0x01, 0x00, 0x00,
#endif
	};

	uint32_t unk[] = {
		0x800000,
		0x21212, // upper nibble is input count TODO: xmlify
		fsbuf->gpu_va + 0xC0, // XXX: dynalloc -- fragment shader
		agx_upload(shaders, unk_aux0, sizeof(unk_aux0)),
		0x0,
	};

	return agx_upload(allocator, unk, sizeof(unk));
}

static uint64_t
demo_unk8(struct agx_allocator *allocator)
{
	/* Varying related */
	uint32_t unk[] = {
		0x100c0000, 0x004, 0x0, 0x0, 0x0,
	};

	return agx_upload(allocator, unk, sizeof(unk));
}

static uint64_t
demo_linkage(struct agx_allocator *allocator)
{
	struct agx_ptr t = agx_allocate(allocator, AGX_LINKAGE_LENGTH);

	bl_pack(t.map, LINKAGE, cfg) {
		cfg.varying_count = 8;
		cfg.unk_1 = 0x10000; // varyings otherwise wrong
	};

	return t.gpu_va;
}

static uint64_t
demo_rasterizer(struct agx_allocator *allocator)
{
	struct agx_ptr t = agx_allocate(allocator, AGX_RASTERIZER_LENGTH);

	bl_pack(t.map, RASTERIZER, cfg) {
		/* Defaults are fine */
	};

	return t.gpu_va;
}

static uint64_t
demo_unk11(struct agx_allocator *allocator)
{
	/* OR'd with the rasterizer bits Metal sets this together but purpose unknown */
#define UNK11_READS_TIB (1 << 25)
#define UNK11_FILL_MODE_LINES_1 (1 << 26)

#define UNK11_FILL_MODE_LINES_2 (0x5004 << 16)
	uint32_t unk[] = {
		0x200004a,
		0x200 /*| UNK11_READS_TIB*/ /*| UNK11_FILL_MODE_LINES_1*/,
		0x7e00000 /*| UNK11_FILL_MODE_LINES_2*/,
		0x7e00000 /*| UNK11_FILL_MODE_LINES_2*/,

		/* if colour mask is all 0x1ffff. but if it's not all, just the
		 * colour mask in the first few bits, shifted over by the ffs()
		 * of the channels.. not really sure what happens to the others */
		0x1ffff
	};

	return agx_upload(allocator, unk, sizeof(unk));
}

static uint64_t
demo_unk12(struct agx_allocator *allocator)
{
	uint32_t unk[] = {
		0x410000,
		0x1e3ce508,
		0xa0
	};

	return agx_upload(allocator, unk, sizeof(unk));
}

static uint64_t
demo_cull(struct agx_allocator *allocator)
{
	struct agx_ptr t = agx_allocate(allocator, AGX_CULL_LENGTH);

	bl_pack(t.map, CULL, cfg) {
		cfg.cull_back = true;
		cfg.depth_clip = true;
	};

	return t.gpu_va;
}

static uint64_t
demo_unk14(struct agx_allocator *allocator)
{
	uint32_t unk[] = {
		0x100, 0x0,
	};

	return agx_upload(allocator, unk, sizeof(unk));
}

/* TODO: there appears to be hidden support for line loops/triangle fans/quads
 * but still need to confirm on a more substantive workload, also I can't get
 * points/lines to work yet.. */

static uint64_t
demo_unk2(struct agx_allocator *allocator, struct agx_allocator *shaders, struct agx_allocation *vsbuf, struct agx_allocation *fsbuf)
{
	struct agx_ptr ptr = agx_allocate(allocator, 0x800);
	uint8_t *out = ptr.map;
	uint64_t temp = 0;

	assert(vsbuf->gpu_va < (1ull << 32));
	assert(fsbuf->gpu_va < (1ull << 32));

	// Bind vertex pipeline and start queueing commands
	uint32_t bind_vertex[] = {
		0x4000002e,
		0x1002,
		vsbuf->gpu_va,
		0x0808,
	};

	memcpy(out, bind_vertex, sizeof(bind_vertex));
	out += sizeof(bind_vertex);

	/* yes, really unaligned */
	*(out++) = 0x0;

	/* Remark: the first argument to each ptr40 is the number of 32-bit
	 * words pointed to. The data type is inferred at the source. In theory
	 * this means we can reorder blocks. We can also duplicate blocks.
	 * Exception: the first block which is tagged 0?  Duplication means
	 * this isn't by length, instead a special record at the end indicates
	 * the end. */

	temp = make_ptr40(0x00, 0x00, 0x00, demo_zero(allocator, 16));
	memcpy(out, &temp, 8);
	out += 8;

	temp = make_ptr40(0x05, 0x00, 0x00, demo_unk8(allocator));
	memcpy(out, &temp, 8);
	out += 8;

	temp = make_ptr40(0x05, 0x00, 0x00, demo_launch_fragment(allocator, fsbuf, shaders));
	memcpy(out, &temp, 8);
	out += 8;

	temp = make_ptr40(0x04, 0x00, 0x00, demo_linkage(allocator));
	memcpy(out, &temp, 8);
	out += 8;

	temp = make_ptr40(0x07, 0x00, 0x00, demo_rasterizer(allocator));
	memcpy(out, &temp, 8);
	out += 8;

	/* Note can be omitted */
	temp = make_ptr40(0x05, 0x00, 0x00, demo_unk11(allocator));
	memcpy(out, &temp, 8);
	out += 8;

	temp = make_ptr40(0x0a, 0x00, 0x00, demo_viewport(allocator));
	memcpy(out, &temp, 8);
	out += 8;

	temp = make_ptr40(0x03, 0x00, 0x00, demo_unk12(allocator));
	memcpy(out, &temp, 8);
	out += 8;

	temp = make_ptr40(0x02, 0x00, 0x00, demo_cull(allocator));
	memcpy(out, &temp, 8);
	out += 8;

	temp = make_ptr40(0x02, 0x00, 0x00, demo_unk14(allocator));
	memcpy(out, &temp, 8);
	out += 8;

	/* Must be after the rest */

	bl_pack(out, DRAW, cfg) {
		cfg.primitive = AGX_PRIMITIVE_TRIANGLES;
		cfg.vertex_start = 0;
		cfg.vertex_count = 36;
		cfg.instance_count = 1;
	};

	out += AGX_DRAW_LENGTH;

	uint8_t stop[] = {
		0x00, 0x00, 0x00, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, // Stop
	};

	memcpy(out, stop, sizeof(stop));
	out += sizeof(stop);

	return ptr.gpu_va;
}

/* Odd pattern */
static uint64_t
demo_unk6(struct agx_allocator *allocator)
{
	struct agx_ptr ptr = agx_allocate(allocator, 0x4000 * sizeof(uint64_t));
	uint64_t *buf = ptr.map;
        memset(buf, 0, sizeof(*buf));

        for (unsigned i = 1; i < 0x3ff; ++i)
                buf[i] = (i + 1);

	return ptr.gpu_va;
}

#define PTR40(a, b, c, ptr) make_ptr40(0x ## a, 0x ## b, 0x ##c, ptr)

/* Set arguments to a vertex/compute shader (attribute table or
 * kernel arguments respectively). start/sz are word-sized */

static uint64_t
demo_bind_arg_words(uint64_t gpu_va, unsigned start, unsigned sz)
{
	assert(sz < 8);
	assert(gpu_va < (1ull << 40));
	assert(start < 0x80); /* TODO: oliver */

	return 0x1d | (start << 9) | (sz << 21) | (gpu_va << 24);
}

static void
demo_vsbuf(uint64_t *buf, struct agx_allocator *allocator, struct agx_allocator *shader_pool)
{
	uint32_t vs_offs = demo_vertex_shader(shader_pool);

	float uni[4 * 36 * 2];
	unsigned count = 0;

	float verts[8][4] = {
		{ -1.0, -1.0, -1.0, 1.0 },
		{ +1.0, -1.0, -1.0, 1.0 },
		{ -1.0, +1.0, -1.0, 1.0 },
		{ +1.0, +1.0, -1.0, 1.0 },
		{ -1.0, -1.0, +1.0, 1.0 },
		{ +1.0, -1.0, +1.0, 1.0 },
		{ -1.0, +1.0, +1.0, 1.0 },
		{ +1.0, +1.0, +1.0, 1.0 },
	};

	float vert_texcoord[4][4] = {
		{ 0.0, 0.0, 0.0, 0.0 },
		{ 0.0, 1.0, 0.0, 0.0 },
		{ 1.0, 1.0, 0.0, 0.0 },
		{ 1.0, 0.0, 0.0, 0.0 },
	};

	unsigned quads[6][4] = {
		{ 0, 2, 3, 1 },
		{ 1, 3, 7, 5 },
		{ 5, 7, 6, 4 },
		{ 4, 6, 2, 0 },
		{ 4, 0, 1, 5 },
		{ 2, 6, 7, 3 }
	};

	unsigned visit[6] = {0, 1, 2, 2, 3, 0 };
	for (unsigned i = 0; i < 6; ++i) {
		for (unsigned j = 0; j < 6; ++j) {
			unsigned vert = quads[i][visit[j]];
			memcpy(uni + count + 0, verts[vert], 16);
			memcpy(uni + count + 4, vert_texcoord[visit[j]], 16);
			count += 8;
		}
	}

	uint64_t colour_ptr = agx_upload(allocator, uni, sizeof(uni));
	uint64_t colour_ptr_ptr = agx_upload(allocator, &colour_ptr, sizeof(colour_ptr));

	float time = t;
	t += 0.05f;
	uint64_t time_ptr = agx_upload(allocator, &time, sizeof(time));
	uint64_t time_ptr_ptr = agx_upload(allocator, &time_ptr, sizeof(time_ptr));

	buf[0] = demo_bind_arg_words(time_ptr_ptr, 0, 2);
	buf[1] = demo_bind_arg_words(colour_ptr_ptr, 2, 2);
	buf[2] = 0x0000904d | (0x80dull << 32) | ((uint64_t) (vs_offs & 0xFFFF) << 48);
	buf[3] = (vs_offs >> 16) | (0x0F8d << 16) | (0x00880100ull << 32);
}



static void
demo_fsbuf(uint64_t *buf, struct agx_allocator *allocator, struct agx_allocation *framebuffer, struct agx_allocator *shader_pool)
{
	uint32_t clear_offs = demo_clear(shader_pool);
	uint32_t aux3_offs = demo_frag_aux3(shader_pool);
	uint32_t fs_offs = demo_fragment_shader(shader_pool);

	memset(buf, 0, 128 * 8);

	/* Clear shader */
	buf[ 8] = demo_bind_arg_words(demo_clear_color(allocator), 6, 2);
	buf[ 9] = 0x2010bd4d | (0x40dull << 32) | ((uint64_t) (clear_offs & 0xFFFF) << 48);
	buf[10] = ((uint64_t) clear_offs >> 16) | (0x18d << 16) | (0x00880100ull << 32);

	/* AUX3 */
	buf[16] = PTR40(dd, 00, 10, demo_render_target(allocator, framebuffer));
	buf[17] = demo_bind_arg_words(demo_unk0_5(allocator), 2, 2);
	buf[18] = 0x2010bd4d | (0x000dull << 32) | ((uint64_t) (aux3_offs & 0xFFFF) << 48);
	buf[19] = ((uint64_t) aux3_offs >> 16) | (0x18d << 16) | (0x00880100ull << 32);

	/* Fragment shader */
	buf[24] = PTR40(dd, 00, 10, demo_texture(allocator));
	buf[25] = PTR40(9d, 00, 10, demo_sampler(allocator));
	buf[26] = 0x2010bd4d | (0x10dull << 32) | ((uint64_t) (fs_offs & 0xFFFF) << 48);
	buf[27] = (fs_offs >> 16) | (0x208d << 16) | (0xf3580100ull << 32);
	buf[28] = 0x00880002 | (0xc080ull << 32);
}

struct cmdbuf {
	uint32_t *map;
	unsigned offset;
};

static void
EMIT32(struct cmdbuf *cmdbuf, uint32_t val)
{
	cmdbuf->map[cmdbuf->offset++] = val;
}

static void
EMIT64(struct cmdbuf *cmdbuf, uint64_t val)
{
	EMIT32(cmdbuf, (val & 0xFFFFFFFF));
	EMIT32(cmdbuf, (val >> 32));
}

static void
EMIT_WORDS(struct cmdbuf *cmdbuf, uint8_t *buf, size_t count)
{
	assert((count & 0x3) == 0);

	for (unsigned i = 0; i < count; i += 4) {
		uint32_t u32 =
			(buf[i + 3] << 24) |
			(buf[i + 2] << 16) |
			(buf[i + 1] <<  8) |
			(buf[i + 0] <<  0);

		EMIT32(cmdbuf, u32);
	}
}

static void
EMIT_ZERO_WORDS(struct cmdbuf *cmdbuf, size_t words)
{
	memset(cmdbuf->map + cmdbuf->offset, 0, words * 4);
	cmdbuf->offset += words;
}

static void
demo_cmdbuf(uint64_t *buf, struct agx_allocator *allocator,
		struct agx_allocation *vsbuf,
		struct agx_allocation *fsbuf,
		struct agx_allocation *framebuffer,
		struct agx_allocator *shaders)
{
	demo_vsbuf((uint64_t *) vsbuf->map, allocator, shaders);
	demo_fsbuf((uint64_t *) fsbuf->map, allocator, framebuffer, shaders);

	struct cmdbuf _cmdbuf = {
		.map = (uint32_t *) buf,
		.offset = 0
	};

	struct cmdbuf *cmdbuf = &_cmdbuf;

	/* Vertex stuff */
	EMIT32(cmdbuf, 0x10000);
	EMIT32(cmdbuf, 0x780); // Compute: 0x188
	EMIT32(cmdbuf, 0x7);
	EMIT_ZERO_WORDS(cmdbuf, 5);
	EMIT32(cmdbuf, 0x758); // Compute: 0x180
	EMIT32(cmdbuf, 0x18);  // Compute: 0x0
	EMIT32(cmdbuf, 0x758); // Compute: 0x0
	EMIT32(cmdbuf, 0x728); // Compute: 0x150

	EMIT32(cmdbuf, 0x30); /* 0x30 */
	EMIT32(cmdbuf, 0x01); /* 0x34. Compute: 0x03 */

	/* Pointer to data about the vertex and fragment shaders */
	EMIT64(cmdbuf, demo_unk2(allocator, shaders, vsbuf, fsbuf));

	EMIT_ZERO_WORDS(cmdbuf, 20);

	EMIT64(cmdbuf, 0); /* 0x90, compute blob - some zero */
	EMIT64(cmdbuf, 0); // blob - 0x540 bytes of zero, compute blob - null
	EMIT64(cmdbuf, 0); // blob - 0x280 bytes of zero
	EMIT64(cmdbuf, 0); // a8, compute blob - zero pointer

	EMIT64(cmdbuf, 0); // compute blob - zero pointer
	EMIT64(cmdbuf, 0); // compute blob - zero pointer
	EMIT64(cmdbuf, 0); // compute blob - zero pointer

	// while zero for vertex, used to include the odd unk6 pattern for compute
	EMIT64(cmdbuf, 0); // compute blob - 0x1
	EMIT64(cmdbuf, 0); // d0,  ompute blob - pointer to odd pattern, compare how it's done later for frag

	// compute 8 bytes of zero, then reconverge at *

	EMIT32(cmdbuf, 0x6b0003); // d8
	EMIT32(cmdbuf, 0x3a0012); // dc

	/* Possibly the funny pattern but not actually pointed to for vertex */
	EMIT64(cmdbuf, 1); // e0
	EMIT64(cmdbuf, 0); // e8

	EMIT_ZERO_WORDS(cmdbuf, 44);

	EMIT64(cmdbuf, 0); // blob - 0x20 bytes of zero
	EMIT64(cmdbuf, 1); // 1a8

	// * compute reconverges here at 0xe0 in my trace
	EMIT32(cmdbuf, 0x1c); // 1b0

	// compute 0xe4: [encoder ID -- from selector6 + 2 with blob], 0, 0, 0xffffffff, done for a while
	// compute 0x120: 0x9 | 0x128: 0x40

	EMIT32(cmdbuf, 0); // 1b0 - compute: 0x10000
	EMIT64(cmdbuf, 0x0); // 1b8 -- compute 0x10000
	EMIT32(cmdbuf, 0xffffffff); // note we can zero!
	EMIT32(cmdbuf, 0xffffffff); // note we can zero! compute 0
	EMIT32(cmdbuf, 0xffffffff); // note we can zero! compute 0
	EMIT32(cmdbuf, 0);

	EMIT_ZERO_WORDS(cmdbuf, 40);

	EMIT32(cmdbuf, 0xffff8002); // 0x270
	EMIT32(cmdbuf, 0);
	EMIT64(cmdbuf, fsbuf->gpu_va + 0x44);// clear -- XXX: dynalloc
	EMIT32(cmdbuf, 0);
	EMIT32(cmdbuf, 0);
	EMIT32(cmdbuf, 0);
	EMIT32(cmdbuf, 0x12);
	EMIT64(cmdbuf, fsbuf->gpu_va + 0x84); // AUX3 -- 0x290 -- XXX: dynalloc
	EMIT64(cmdbuf, demo_zero(allocator, 0x1000));
	EMIT64(cmdbuf, demo_zero(allocator, 0x1000));
	EMIT64(cmdbuf, 0);

	EMIT_ZERO_WORDS(cmdbuf, 48);

	EMIT64(cmdbuf, 4);
	EMIT64(cmdbuf, 0xc000);

	/* Note: making these smallers scissors polygons but not clear colour */
	EMIT32(cmdbuf, WIDTH);
	EMIT32(cmdbuf, HEIGHT);
	EMIT64(cmdbuf, demo_zero(allocator, 0x8000));

	EMIT_ZERO_WORDS(cmdbuf, 48);

	float depth_clear = 1.0;
	uint8_t stencil_clear = 0;

	EMIT64(cmdbuf, 0); // 0x450
	EMIT32(cmdbuf, fui(depth_clear));
	EMIT32(cmdbuf, (0x3 << 8) | stencil_clear);
	EMIT64(cmdbuf, 0);
	EMIT64(cmdbuf, 0x1000000);
	EMIT32(cmdbuf, 0xffffffff);
	EMIT32(cmdbuf, 0xffffffff);
	EMIT32(cmdbuf, 0xffffffff);
	EMIT32(cmdbuf, 0);

	EMIT_ZERO_WORDS(cmdbuf, 8);

	EMIT64(cmdbuf, 0); // 0x4a0
	EMIT32(cmdbuf, 0xffff8212);
	EMIT32(cmdbuf, 0);

	EMIT64(cmdbuf, fsbuf->gpu_va + 0x4);// XXX: dynalloc -- not referenced
	EMIT64(cmdbuf, 0);

	EMIT32(cmdbuf, 0);
	EMIT32(cmdbuf, 0x12);
	EMIT32(cmdbuf, fsbuf->gpu_va + 0x84); // AUX3
	EMIT32(cmdbuf, 0);

	EMIT_ZERO_WORDS(cmdbuf, 44);

	EMIT64(cmdbuf, 1); // 0x580
	EMIT64(cmdbuf, 0);
	EMIT_ZERO_WORDS(cmdbuf, 4);

	/* Compare compute case ,which has a bit of reordering, but we can swap */
	EMIT32(cmdbuf, 0x1c); // 0x5a0
	EMIT32(cmdbuf, 0);
	EMIT64(cmdbuf, 0xCAFECAFE); // encoder ID XXX: don't fix
	EMIT32(cmdbuf, 0);
	EMIT32(cmdbuf, 0xffffffff);

	// remark: opposite order for compute, but we can swap the orders
	EMIT32(cmdbuf, 1);
	EMIT32(cmdbuf, 0);
	EMIT64(cmdbuf, 0);
	EMIT64(cmdbuf, demo_unk6(allocator) );

	/* note: width/height act like scissor, but changing the 0s doesn't
	 * seem to affect (maybe scissor enable bit missing), _and this affects
	 * the clear_ .. bbox maybe */
	EMIT32(cmdbuf, 0);
	EMIT32(cmdbuf, 0);
	EMIT32(cmdbuf, WIDTH); // can increase up to 16384
	EMIT32(cmdbuf, HEIGHT);

	EMIT32(cmdbuf, 1);
	EMIT32(cmdbuf, 8);
	EMIT32(cmdbuf, 8);
	EMIT32(cmdbuf, 0);

	EMIT_ZERO_WORDS(cmdbuf, 12);

	EMIT32(cmdbuf, 0); // 0x620
	EMIT32(cmdbuf, 8);
	EMIT32(cmdbuf, 0x20);
	EMIT32(cmdbuf, 0x20);
	EMIT32(cmdbuf, 0x1);
	EMIT32(cmdbuf, 0);
	EMIT64(cmdbuf, 0);

	EMIT_ZERO_WORDS(cmdbuf, 72);

	EMIT32(cmdbuf, 0); // 0x760
	EMIT32(cmdbuf, 0x1); // number of attachments (includes depth/stencil) stored to

	/* A single attachment follows, depth/stencil have their own attachments */
	{
		EMIT64(cmdbuf, 0x100 | (framebuffer->gpu_va << 16));
		EMIT32(cmdbuf, 0xa0000);
		EMIT32(cmdbuf, 0x4c000000); // 80000000 also observed, and 8c000 and.. offset into the tilebuffer I imagine
		EMIT32(cmdbuf, 0x0c001d); // C0020  also observed
		EMIT32(cmdbuf, 0x640000);
	}
}

static struct agx_map_entry
demo_map_entry(struct agx_allocation *alloc)
{
	return (struct agx_map_entry) {
		.unkAAA = 0x20,
		.unkBBB = 0x1,
		.unka = 0x1ffff,
		.index = alloc->index,
	};
}

static struct agx_map_header
demo_map_header(uint64_t cmdbuf_id, uint64_t encoder_id, unsigned count)
{
	return (struct agx_map_header) {
		.cmdbuf_id = cmdbuf_id,
		.unk2 = 0x1,
		.unk3 = 0x528, // 1320
		.encoder_id = encoder_id,
		.unk6 = 0x0,
		.unk7 = 0x780, // 1920

		/* +1 for the sentinel ending */
		.nr_entries_1 = count + 1,
		.nr_entries_2 = count + 1,
		.unka = 0x0b,
	};
}

static void
demo_mem_map(void *map, struct agx_allocation *allocs, unsigned count,
		uint64_t cmdbuf_id, uint64_t encoder_id)
{
	struct agx_map_header *header = map;
	struct agx_map_entry *entries = (struct agx_map_entry *) (map + 0x40);

	/* Header precedes the entry */
	*header = demo_map_header(cmdbuf_id, encoder_id, count);

	/* Add an entry for each BO mapped */
	for (unsigned i = 0; i < count; ++i) {
		if (allocs[i].type != AGX_ALLOC_REGULAR)
			continue;

		entries[i] = (struct agx_map_entry) {
			.unkAAA = 0x20,
			.unkBBB = 0x1,
			.unka = 0x1ffff,
			.index = allocs[i].index
		};
	}

	/* Final entry is a sentinel */
	entries[count] = (struct agx_map_entry) {
		.unkAAA = 0x40,
		.unkBBB = 0x1,
		.unka = 0x1ffff,
		.index = 0
	};
}

void demo(mach_port_t connection, bool offscreen)
{
	struct agx_command_queue command_queue = agx_create_command_queue(connection);

	// XXX: why do BO ids below 6 mess things up..?
	for (unsigned i = 0; i < 6; ++i) {
		struct agx_allocation dummy = agx_alloc_mem(connection, 4096, AGX_MEMORY_TYPE_FRAMEBUFFER, false);
	}

	struct agx_allocation shader = agx_alloc_mem(connection, 0x10000, AGX_MEMORY_TYPE_SHADER, false);

	struct agx_allocator shader_pool = { .backing = shader, };

	struct agx_allocation bo = agx_alloc_mem(connection, 1920*1080*4*2, AGX_MEMORY_TYPE_FRAMEBUFFER, false);
	struct agx_allocator allocator = { .backing = bo };

	struct agx_allocation vsbuf = agx_alloc_mem(connection, 0x8000, AGX_MEMORY_TYPE_CMDBUF_32, false);
	struct agx_allocation fsbuf = agx_alloc_mem(connection, 0x8000, AGX_MEMORY_TYPE_CMDBUF_32, false);
	struct agx_allocation framebuffer = agx_alloc_mem(connection, 
		ALIGN_POT(WIDTH, 64) * ALIGN_POT(HEIGHT, 64) * 4,
		AGX_MEMORY_TYPE_FRAMEBUFFER, false);

	struct agx_allocation cmdbuf = agx_alloc_cmdbuf(connection, 0x4000, true);

	struct agx_allocation memmap = agx_alloc_cmdbuf(connection, 0x4000, false);

	{ 
		texture_payload = agx_alloc_mem(connection, ((((tex_width + 64) * (tex_height + 64) * 4) + 64) + 4095) & ~4095,
				AGX_MEMORY_TYPE_FRAMEBUFFER, false);
		uint32_t *rgba = malloc((tex_width + 64) * (tex_height + 64) * 4);

		FILE *fp = fopen("/Users/bloom/Downloads/Hooves.bmp", "rb");
		fseek(fp, 0x8a, SEEK_SET);
		fread(rgba, 1, tex_width * tex_height * 4, fp);
		fclose(fp);

		ash_tile(texture_payload.map, rgba,
				tex_width, 32, tex_width,
				0, 0, tex_width, tex_height);
		free(rgba);
	}

	uint64_t global_ids = agx_cmdbuf_global_ids(connection);

	struct agx_allocation allocs[] = {
		shader,
		bo,
		vsbuf,
		fsbuf,
		framebuffer,
		texture_payload
	};

	demo_mem_map(memmap.map, allocs, sizeof(allocs) / sizeof(allocs[0]),
			0xDEADBEEF, 0xCAFECAFE); // (unk6 + 1, unk6 + 2) but it doesn't really matter

	uint32_t *linear;
	unsigned stride = WIDTH * 4;

	if (offscreen) {
		linear = malloc(WIDTH * HEIGHT * 4);
	} else {
		struct slowfb fb = slowfb_init(WIDTH, HEIGHT);
		linear = fb.map;
		stride = fb.stride;
	}

	for (;;) {
		demo_cmdbuf(cmdbuf.map, &allocator, &vsbuf, &fsbuf, &framebuffer, &shader_pool);
		agx_submit_cmdbuf(connection, &cmdbuf, &memmap, command_queue.id);

		/* Block until it's done */
		IOReturn ret = IODataQueueWaitForAvailableData(command_queue.notif.queue, command_queue.notif.port);
		while (IODataQueueDataAvailable(command_queue.notif.queue))
			ret = IODataQueueDequeue(command_queue.notif.queue, NULL, 0);

		/* Dump the framebuffer */
		ash_detile(framebuffer.map, linear,
				WIDTH, 32, stride / 4,
				0, 0, WIDTH, HEIGHT);

		shader_pool.offset = 0;
		allocator.offset = 0;

		if (offscreen) {
			FILE *fp = fopen("fb.bin", "wb");
			fwrite(linear, 1, WIDTH * HEIGHT * 4, fp);
			fclose(fp);

			break;
		} else {
			slowfb_update(WIDTH, HEIGHT);
		}
	}
}
