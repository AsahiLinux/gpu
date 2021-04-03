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

#ifndef __CMDSTREAM_H
#define __CMDSTREAM_H

#include <stdint.h>

struct agx_map_header {
	uint64_t cmdbuf_id; // GUID
	uint32_t unk2; // 01 00 00 00
	uint32_t unk3; // 28 05 00 80
	uint64_t encoder_id; // GUID
	uint32_t unk6; // 00 00 00 00
	uint32_t unk7; // 80 07 00 00
	uint32_t nr_entries_1;
	uint32_t nr_entries_2;
	uint32_t unka; // 0b 00 00 00
	uint32_t padding[4];
} __attribute__((packed));

struct agx_map_entry {
	uint32_t unkAAA; // 20 00 00 00
	uint32_t unk2; // 00 00 00 00 
	uint32_t unk3; // 00 00 00 00
	uint32_t unk4; // 00 00 00 00
	uint32_t unk5; // 00 00 00 00
	uint32_t unk6; // 00 00 00 00 
	uint32_t unkBBB; // 01 00 00 00
	uint32_t unk8; // 00 00 00 00
	uint32_t unk9; // 00 00 00 00
	uint32_t unka; // ff ff 01 00 
	uint32_t index;
	uint32_t padding[5];
} __attribute__((packed));

#endif
