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

#ifndef __AGX_IO_H
#define __AGX_IO_H

#include <stdbool.h>
#include <mach/mach.h>

enum agx_alloc_type {
	AGX_ALLOC_REGULAR = 0,
	AGX_ALLOC_MEMMAP = 1,
	AGX_ALLOC_CMDBUF = 2,
	AGX_NUM_ALLOC,
};

static const char *agx_alloc_types[AGX_NUM_ALLOC] = { "mem", "map", "cmd" };

struct agx_allocation {
	enum agx_alloc_type type;
	size_t size;

	/* Index unique only up to type */
	unsigned index;

	/* If CPU mapped, CPU address. NULL if not mapped */
	void *map;

	/* If type REGULAR, mapped GPU address */
	uint64_t gpu_va;
};

struct agx_allocation agx_alloc_mem(mach_port_t connection, size_t size);
struct agx_allocation agx_alloc_cmdbuf(mach_port_t connection, size_t size, bool cmdbuf);

#endif
