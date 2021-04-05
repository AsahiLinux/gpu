#ifndef __DEMO_H
#define __DEMO_H

#include <assert.h>
#include "io.h"
#include "cmdstream.h"

/* Dumb watermark allocator for demo purposes */

struct agx_allocator {
	struct agx_allocation backing;
	unsigned offset;
};

struct agx_ptr {
	void *map;
	uint64_t gpu_va;
};

static struct agx_ptr
agx_allocate(struct agx_allocator *allocator, size_t size)
{
	allocator->offset = (allocator->offset & ~127) + 128;
	assert(size < (allocator->backing.size - allocator->offset));

	struct agx_ptr ptr = {
		.map = allocator->backing.map + allocator->offset,
		.gpu_va = allocator->backing.gpu_va + allocator->offset,
	};

	allocator->offset += size;
	return ptr;
}

static uint64_t
agx_upload(struct agx_allocator *allocator, void *data, size_t size)
{
	struct agx_ptr ptr = agx_allocate(allocator, size);
	memcpy(ptr.map, data, size);
	return ptr.gpu_va;
}

void demo(mach_port_t connection, bool offscreen);
uint32_t demo_vertex_shader(struct agx_allocator *allocator);
uint32_t demo_fragment_shader(struct agx_allocator *allocator);
uint32_t demo_vertex_pre(struct agx_allocator *allocator);
uint32_t demo_clear(struct agx_allocator *allocator);
uint32_t demo_frag_aux3(struct agx_allocator *allocator);

void slowfb_init(uint8_t *framebuffer, int width, int height);
void slowfb_update(int width, int height);

#endif
