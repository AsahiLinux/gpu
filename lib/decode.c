/*
 * Copyright (C) 2017-2019 Alyssa Rosenzweig
 * Copyright (C) 2017-2019 Connor Abbott
 * Copyright (C) 2019 Collabora, Ltd.
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

#include <agx_pack.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <stdbool.h>
#include <stdarg.h>
#include <ctype.h>
#include <sys/mman.h>

#include "decode.h"
#include "io.h"

/* Memory handling, this can't pull in proper data structures so hardcode some
 * things, it should be "good enough" for most use cases */

#define MAX_MAPPINGS 4096

struct agx_allocation mmap_array[MAX_MAPPINGS];
unsigned mmap_count = 0;

struct agx_allocation *ro_mappings[MAX_MAPPINGS];
unsigned ro_mapping_count = 0;

static struct agx_allocation *
pandecode_find_mapped_gpu_mem_containing_rw(uint64_t addr)
{
        for (unsigned i = 0; i < mmap_count; ++i) {
                if (addr >= mmap_array[i].gpu_va && (addr - mmap_array[i].gpu_va) < mmap_array[i].size)
                        return mmap_array + i;
        }

        return NULL;
}

struct agx_allocation *
pandecode_find_mapped_gpu_mem_containing(uint64_t addr)
{
        struct agx_allocation *mem = pandecode_find_mapped_gpu_mem_containing_rw(addr);

        if (mem && mem->map && !mem->ro) {
                mprotect(mem->map, mem->size, PROT_READ);
                mem->ro = true;
                ro_mappings[ro_mapping_count++] = mem;
                assert(ro_mapping_count < MAX_MAPPINGS);
        }

        return mem;
}

static inline void *
__pandecode_fetch_gpu_mem(const struct agx_allocation *mem,
                          uint64_t gpu_va, size_t size,
                          int line, const char *filename)
{
        if (!mem)
                mem = pandecode_find_mapped_gpu_mem_containing(gpu_va);

        if (!mem) {
                fprintf(stderr, "Access to unknown memory %" PRIx64 " in %s:%d\n",
                        gpu_va, filename, line);
                assert(0);
        }

        assert(mem);
        assert(size + (gpu_va - mem->gpu_va) <= mem->size);

        return mem->map + gpu_va - mem->gpu_va;
}

#define pandecode_fetch_gpu_mem(mem, gpu_va, size) \
	__pandecode_fetch_gpu_mem(mem, gpu_va, size, __LINE__, __FILE__)

static void
pandecode_map_read_write(void)
{
        for (unsigned i = 0; i < ro_mapping_count; ++i) {
                ro_mappings[i]->ro = false;
                mprotect(ro_mappings[i]->map, ro_mappings[i]->size,
                                PROT_READ | PROT_WRITE);
        }

        ro_mapping_count = 0;
}

/* Helpers for parsing the cmdstream */

#define DUMP_UNPACKED(T, var, str) { \
        pandecode_log(str); \
        bl_print(pandecode_dump_stream, T, var, (pandecode_indent + 1) * 2); \
}

#define DUMP_CL(T, cl, str) {\
        bl_unpack(cl, T, temp); \
        DUMP_UNPACKED(T, temp, str "\n"); \
}

#define DUMP_SECTION(A, S, cl, str) { \
        pan_section_unpack(cl, A, S, temp); \
        pandecode_log(str); \
        pan_section_print(pandecode_dump_stream, A, S, temp, (pandecode_indent + 1) * 2); \
}

#define MAP_ADDR(T, addr, cl) \
        const uint8_t *cl = 0; \
        { \
                struct agx_allocation *mapped_mem = pandecode_find_mapped_gpu_mem_containing(addr); \
                cl = pandecode_fetch_gpu_mem(mapped_mem, addr, MALI_ ## T ## _LENGTH); \
        }

#define DUMP_ADDR(T, addr, str) {\
        MAP_ADDR(T, addr, cl) \
        DUMP_CL(T, cl, str); \
}

FILE *pandecode_dump_stream;

#define pandecode_log(str) fputs(str, pandecode_dump_stream)
#define pandecode_msg(str) fprintf(pandecode_dump_stream, "// %s", str)

unsigned pandecode_indent = 0;

/* To check for memory safety issues, validates that the given pointer in GPU
 * memory is valid, containing at least sz bytes. The goal is to detect
 * GPU-side memory bugs (NULL pointer dereferences, buffer overflows, or buffer
 * overruns) by statically validating pointers.
 */

static void
pandecode_validate_buffer(uint64_t addr, size_t sz)
{
        if (!addr) {
                pandecode_msg("XXX: null pointer deref");
                return;
        }

        /* Find a BO */

        struct agx_allocation *bo =
                pandecode_find_mapped_gpu_mem_containing(addr);

        if (!bo) {
                pandecode_msg("XXX: invalid memory dereference\n");
                return;
        }

        /* Bounds check */

        unsigned offset = addr - bo->gpu_va;
        unsigned total = offset + sz;

        if (total > bo->size) {
                fprintf(pandecode_dump_stream, "// XXX: buffer overrun. "
                                "Chunk of size %zu at offset %d in buffer of size %zu. "
                                "Overrun by %zu bytes. \n",
                                sz, offset, bo->size, total - bo->size);
                return;
        }
}

static struct agx_allocation *
pandecode_find_cmdbuf(unsigned cmdbuf_index)
{
	for (unsigned i = 0; i < mmap_count; ++i) {
		if (mmap_array[i].type != AGX_ALLOC_CMDBUF)
			continue;

		if (mmap_array[i].index != cmdbuf_index)
			continue;

		return &mmap_array[i];
	}

	return NULL;
}

static void
pandecode_dump_bo(struct agx_allocation *bo, const char *name)
{
	fprintf(pandecode_dump_stream, "%s %s (%u)\n", name, bo->name ?: "", bo->index);
	hexdump(pandecode_dump_stream, bo->map, bo->size, false);
}

/* Abstraction for command stream parsing */
typedef unsigned (*decode_cmd)(const uint8_t *map);

#define STATE_DONE (0xFFFFFFFFu)

static void
pandecode_stateful(uint64_t va, const char *label, decode_cmd decoder)
{
	struct agx_allocation *alloc = pandecode_find_mapped_gpu_mem_containing(va);
	assert(alloc != NULL && "nonexistant object");

	pandecode_dump_bo(alloc, label);

	uint8_t *map = alloc->map;
	uint8_t *end = map;

	 for (unsigned i = 0; i < alloc->size; ++i) {
		 if (map[i] != 0)
			 end = map + i;
	 }

	 while (map < end) {
		 unsigned count = decoder(map);

		 /* If we fail to decode, default to a hexdump (don't hang) */
		 if (count == 0) {
			hexdump(pandecode_dump_stream, map, 8, false);
			count = 8;
		 }

		 map += count;

		 if (count == STATE_DONE)
			 break;
	 }
}

static unsigned
pandecode_pipeline(UNUSED const uint8_t *map)
{
	uint8_t zeroes[16] = { 0 };

	if (map[0] == 0x4D && map[1] == 0x90 && map[2] == 0x00) {
		hexdump(pandecode_dump_stream, map, 16, false);
		return 16;
	} else if (memcmp(map, zeroes, 16) == 0) {
		/* TODO: Termination */
		return STATE_DONE;
	} else {
		return 0;
	}
}

static unsigned
pandecode_cmd(const uint8_t *map)
{
	if (map[0] == 0x02 && map[1] == 0x10 && map[2] == 0x00 && map[3] == 0x00) {
		 bl_unpack(map, LAUNCH, cmd);
		 DUMP_UNPACKED(LAUNCH, cmd, "Launch\n");
		 pandecode_stateful(cmd.pipeline, "Pipeline", pandecode_pipeline);
		 return AGX_LAUNCH_LENGTH;
	} else {
		return 0;
	}
}

void
pandecode_cmdstream(unsigned cmdbuf_index)
{
        pandecode_dump_file_open();

	struct agx_allocation *cmdbuf = pandecode_find_cmdbuf(cmdbuf_index);
	assert(cmdbuf != NULL && "nonexistant command buffer");

	pandecode_dump_bo(cmdbuf, "Command buffer");

	/* TODO: What else is in here? */
	uint64_t *encoder = ((uint64_t *) cmdbuf->map) + 7;
	pandecode_stateful(*encoder, "Encoder", pandecode_cmd);

        pandecode_map_read_write();
}

void
pandecode_dump_mappings(void)
{
        pandecode_dump_file_open();

	for (unsigned i = 0; i < mmap_count; ++i) {
		if (!mmap_array[i].map || !mmap_array[i].size)
			continue;

		assert(mmap_array[i].type < AGX_NUM_ALLOC);

		fprintf(pandecode_dump_stream, "Buffer: type %s, gpu %llx, index %u.bin:\n\n",
			agx_alloc_types[mmap_array[i].type],
			mmap_array[i].gpu_va, mmap_array[i].index);

		hexdump(pandecode_dump_stream, mmap_array[i].map, mmap_array[i].size, false);
		fprintf(pandecode_dump_stream, "\n");
	}
}



static void
pandecode_add_name(struct agx_allocation *mem, uint64_t gpu_va, const char *name)
{
        if (!name) {
                /* If we don't have a name, assign one */

                snprintf(mem->name, sizeof(mem->name) - 1,
                         "memory_%" PRIx64, gpu_va);
        } else {
                assert((strlen(name) + 1) < sizeof(mem->name));
                memcpy(mem->name, name, strlen(name) + 1);
        }
}

void
pandecode_track_alloc(struct agx_allocation alloc)
{
        assert((mmap_count + 1) < MAX_MAPPINGS);
        mmap_array[mmap_count++] = alloc;
}

static char *
pointer_as_memory_reference(uint64_t ptr)
{
        struct agx_allocation *mapped;
        char *out = malloc(128);

        /* Try to find the corresponding mapped zone */

        mapped = pandecode_find_mapped_gpu_mem_containing_rw(ptr);

        if (mapped) {
                snprintf(out, 128, "%s + %d", mapped->name, (int) (ptr - mapped->gpu_va));
                return out;
        }

        /* Just use the raw address if other options are exhausted */

        snprintf(out, 128, "0x%" PRIx64, ptr);
        return out;

}

static int pandecode_dump_frame_count = 0;

void
pandecode_dump_file_open(void)
{
        if (pandecode_dump_stream)
                return;

        /* This does a getenv every frame, so it is possible to use
         * setenv to change the base at runtime.
         */
        const char *dump_file_base = getenv("PANDECODE_DUMP_FILE") ?: "pandecode.dump";
        if (!strcmp(dump_file_base, "stderr"))
                pandecode_dump_stream = stderr;
        else {
                char buffer[1024];
                snprintf(buffer, sizeof(buffer), "%s.%04d", dump_file_base, pandecode_dump_frame_count);
                printf("pandecode: dump command stream to file %s\n", buffer);
                pandecode_dump_stream = fopen(buffer, "w");
                if (!pandecode_dump_stream)
                        fprintf(stderr,
                                "pandecode: failed to open command stream log file %s\n",
                                buffer);
        }
}

static void
pandecode_dump_file_close(void)
{
        if (pandecode_dump_stream && pandecode_dump_stream != stderr) {
                fclose(pandecode_dump_stream);
                pandecode_dump_stream = NULL;
        }
}

void
pandecode_next_frame(void)
{
        pandecode_dump_file_close();
        pandecode_dump_frame_count++;
}

void
pandecode_close(void)
{
        pandecode_dump_file_close();
}
