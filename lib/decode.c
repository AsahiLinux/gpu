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

/* Infrastructure for tracking mapped memory */
struct pandecode_mapped_memory {
        size_t length;
        void *addr;
        uint64_t gpu_va;
        bool ro;
        char name[32];
};

/* Memory handling, this can't pull in proper data structures so hardcode some
 * things, it should be "good enough" for most use cases */

#define MAX_MAPPINGS 4096

struct pandecode_mapped_memory mmap_array[MAX_MAPPINGS];
unsigned mmap_count = 0;

struct pandecode_mapped_memory *ro_mappings[MAX_MAPPINGS];
unsigned ro_mapping_count = 0;

static struct pandecode_mapped_memory *
pandecode_find_mapped_gpu_mem_containing_rw(uint64_t addr)
{
        for (unsigned i = 0; i < mmap_count; ++i) {
                if (addr >= mmap_array[i].gpu_va && (addr - mmap_array[i].gpu_va) < mmap_array[i].length)
                        return mmap_array + i;
        }

        return NULL;
}

struct pandecode_mapped_memory *
pandecode_find_mapped_gpu_mem_containing(uint64_t addr)
{
        struct pandecode_mapped_memory *mem = pandecode_find_mapped_gpu_mem_containing_rw(addr);

        if (mem && mem->addr && !mem->ro) {
                mprotect(mem->addr, mem->length, PROT_READ);
                mem->ro = true;
                ro_mappings[ro_mapping_count++] = mem;
                assert(ro_mapping_count < MAX_MAPPINGS);
        }

        return mem;
}

static inline void *
__pandecode_fetch_gpu_mem(const struct pandecode_mapped_memory *mem,
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
        assert(size + (gpu_va - mem->gpu_va) <= mem->length);

        return mem->addr + gpu_va - mem->gpu_va;
}

#define pandecode_fetch_gpu_mem(mem, gpu_va, size) \
	__pandecode_fetch_gpu_mem(mem, gpu_va, size, __LINE__, __FILE__)

static void
pandecode_map_read_write(void)
{
        for (unsigned i = 0; i < ro_mapping_count; ++i) {
                ro_mappings[i]->ro = false;
                mprotect(ro_mappings[i]->addr, ro_mappings[i]->length,
                                PROT_READ | PROT_WRITE);
        }

        ro_mapping_count = 0;
}

/* Helpers for parsing the cmdstream */

#define DUMP_UNPACKED(T, var, str) { \
        pandecode_log(str); \
        pan_print(pandecode_dump_stream, T, var, (pandecode_indent + 1) * 2); \
}

#define DUMP_CL(T, cl, str) {\
        pan_unpack(cl, T, temp); \
        DUMP_UNPACKED(T, temp, str); \
}

#define DUMP_SECTION(A, S, cl, str) { \
        pan_section_unpack(cl, A, S, temp); \
        pandecode_log(str); \
        pan_section_print(pandecode_dump_stream, A, S, temp, (pandecode_indent + 1) * 2); \
}

#define MAP_ADDR(T, addr, cl) \
        const uint8_t *cl = 0; \
        { \
                struct pandecode_mapped_memory *mapped_mem = pandecode_find_mapped_gpu_mem_containing(addr); \
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

        struct pandecode_mapped_memory *bo =
                pandecode_find_mapped_gpu_mem_containing(addr);

        if (!bo) {
                pandecode_msg("XXX: invalid memory dereference\n");
                return;
        }

        /* Bounds check */

        unsigned offset = addr - bo->gpu_va;
        unsigned total = offset + sz;

        if (total > bo->length) {
                fprintf(pandecode_dump_stream, "// XXX: buffer overrun. "
                                "Chunk of size %zu at offset %d in buffer of size %zu. "
                                "Overrun by %zu bytes. \n",
                                sz, offset, bo->length, total - bo->length);
                return;
        }
}

void
pandecode_cmdstream(uint64_t gpu_va)
{
        pandecode_dump_file_open();

	printf("Dumping %"PRIx64, gpu_va);
	/* Do whatever */

        pandecode_map_read_write();
}

static void
pandecode_add_name(struct pandecode_mapped_memory *mem, uint64_t gpu_va, const char *name)
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
pandecode_inject_mmap(uint64_t gpu_va, void *cpu, unsigned sz, const char *name)
{
        /* First, search if we already mapped this and are just updating an address */

        struct pandecode_mapped_memory *existing =
                pandecode_find_mapped_gpu_mem_containing_rw(gpu_va);

        if (existing && existing->gpu_va == gpu_va) {
                existing->length = sz;
                existing->addr = cpu;
                pandecode_add_name(existing, gpu_va, name);
                return;
        }

        assert((mmap_count + 1) < MAX_MAPPINGS);
        mmap_array[mmap_count++] = (struct pandecode_mapped_memory) {
                .addr = cpu,
                .gpu_va = gpu_va,
                .length = sz,
        };
}

static char *
pointer_as_memory_reference(uint64_t ptr)
{
        struct pandecode_mapped_memory *mapped;
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

static bool force_stderr = false;

void
pandecode_dump_file_open(void)
{
        if (pandecode_dump_stream)
                return;

        /* This does a getenv every frame, so it is possible to use
         * setenv to change the base at runtime.
         */
        const char *dump_file_base = getenv("PANDECODE_DUMP_FILE") ?: "pandecode.dump";
        if (force_stderr || !strcmp(dump_file_base, "stderr"))
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
pandecode_initialize(bool to_stderr)
{
        force_stderr = to_stderr;
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
