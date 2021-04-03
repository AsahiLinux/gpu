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
#include <assert.h>
#include <IOKit/IOKitLib.h>
#include "io.h"
#include "selectors.h"
#include "util.h"

struct agx_allocation
agx_alloc_mem(mach_port_t connection, size_t size, enum agx_memory_type type, bool write_combine)
{
	uint32_t mode = 0x430; // shared, ?
	uint32_t cache = write_combine ? 0x400 : 0x0;

	uint32_t args_in[24] = { 0 };
	args_in[1] = write_combine ? 0x400 : 0x0;
	args_in[2] = 0x2580320; //0x18000; // unk
	args_in[3] = 0x1; // unk;
	args_in[4] = 0x4000101; //0x1000101; // unk
	args_in[5] = mode;
	args_in[16] = size;
	args_in[20] = type;
	args_in[21] = 0x3;

	uint64_t out[10] = { 0 };
	size_t out_sz = sizeof(out);

	kern_return_t ret = IOConnectCallMethod(connection,
			AGX_SELECTOR_ALLOCATE_MEM, NULL, 0, args_in,
			sizeof(args_in), NULL, 0, out, &out_sz);

	assert(ret == 0);
	assert(out_sz == sizeof(out));

	return (struct agx_allocation) {
		.type = AGX_ALLOC_REGULAR,
		.guid = out[5],
		.index = (out[3] >> 32ull),
		.gpu_va = out[0],
		.map = (void *) out[1],
		.size = size
	};
}

struct agx_allocation
agx_alloc_cmdbuf(mach_port_t connection, size_t size, bool cmdbuf)
{
	struct agx_create_cmdbuf_resp out = {};
	size_t out_sz = sizeof(out);

	uint64_t inputs[2] = {
		size,
		cmdbuf ? 1 : 0
	};

	kern_return_t ret = IOConnectCallMethod(connection,
			AGX_SELECTOR_CREATE_CMDBUF, inputs, 2, NULL, 0, NULL,
			NULL, &out, &out_sz);

	assert(ret == 0);
	assert(out_sz == sizeof(out));
	assert(out.size == size);

	return (struct agx_allocation) {
		.type = cmdbuf ? AGX_ALLOC_CMDBUF : AGX_ALLOC_MEMMAP,
		.index = out.id,
		.map = out.map,
		.size = out.size,
		.guid = 0, /* TODO? */
	};
}

uint64_t
agx_cmdbuf_global_ids(mach_port_t connection)
{
	uint32_t out[4] = {};
	size_t out_sz = sizeof(out);

	kern_return_t ret = IOConnectCallStructMethod(connection,
			0x6,
			NULL, 0, &out, &out_sz);

	assert(ret == 0);
	assert(out_sz == sizeof(out));
	assert(out[2] == (out[0] + 0x1000000));

	/* Returns a 32-bit but is 64-bit in Instruments, extend with the
	 * missing high bit */
	return (out[0]) | (1ull << 32ull);
}

void
agx_submit_cmdbuf(mach_port_t connection, struct agx_allocation *cmdbuf, struct agx_allocation *mappings, uint64_t scalar)
{
	struct agx_submit_cmdbuf_req req = {
		.unk0 = 0x10,
		.unk1 = 0x1,
		.cmdbuf = cmdbuf->index,
		.mappings = mappings->index,
		.unk2 = 0x0,
		.unk3 = 0x1,
	};

	assert(sizeof(req) == 40);

	kern_return_t ret = IOConnectCallMethod(connection,
			AGX_SELECTOR_SUBMIT_COMMAND_BUFFERS, 
			&scalar, 1,
			&req, sizeof(req),
			NULL, 0, NULL, 0);

	assert(ret == 0);
	return;
}

struct agx_notification_queue
agx_create_notification_queue(mach_port_t connection)
{
	struct agx_create_notification_queue_resp resp;
	size_t resp_size = sizeof(resp);
	assert(resp_size == 0x10);

	kern_return_t ret = IOConnectCallStructMethod(connection,
			AGX_SELECTOR_CREATE_NOTIFICATION_QUEUE,
			NULL, 0, &resp, &resp_size);

	assert(resp_size == sizeof(resp));
	assert(ret == 0);

	mach_port_t notif_port = IODataQueueAllocateNotificationPort();
	IOConnectSetNotificationPort(connection, 0, notif_port, resp.unk2);

	return (struct agx_notification_queue) {
		.port = notif_port,
		.queue = resp.queue,
		.id = resp.unk2
	};
}

struct agx_command_queue
agx_create_command_queue(mach_port_t connection)
{
	struct agx_command_queue queue = {};

	{
		uint8_t buffer[1024 + 8] = { 0 };
		const char *path = "/tmp/a.out";
		assert(strlen(path) < 1022);
		memcpy(buffer + 0, path, strlen(path));

		/* Copy to the end */
		unsigned END_LEN = MIN2(strlen(path), 1024 - strlen(path));
		unsigned SKIP = strlen(path) - END_LEN;
		unsigned OFFS = 1024 - END_LEN;
		memcpy(buffer + OFFS, path + SKIP, END_LEN);

		buffer[1024] = 0x2;

		struct agx_create_command_queue_resp out = {};
		size_t out_sz = sizeof(out);

		kern_return_t ret = IOConnectCallStructMethod(connection,
				AGX_SELECTOR_CREATE_COMMAND_QUEUE, 
				buffer, sizeof(buffer),
				&out, &out_sz);

		assert(ret == 0);
		assert(out_sz == sizeof(out));

		queue.id = out.id;
		assert(queue.id);
	}

	queue.notif = agx_create_notification_queue(connection);

	{
		uint64_t scalars[2] = {
			queue.id,
			queue.notif.id
		};

		kern_return_t ret = IOConnectCallScalarMethod(connection,
				0x1D, 
				scalars, 2, NULL, NULL);

		assert(ret == 0);
	}

	{
		uint64_t scalars[2] = {
			queue.id,
			0x1ffffffffull
		};

		kern_return_t ret = IOConnectCallScalarMethod(connection,
				0x29, 
				scalars, 2, NULL, NULL);

		assert(ret == 0);
	}

	return queue;
}
