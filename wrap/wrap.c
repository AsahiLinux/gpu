/*
 * Copyright (c) 2020 Asahi Linux contributors
 * Copyright (c) 1998-2014 Apple Computer, Inc. All rights reserved.
 * Copyright (c) 2005 Apple Computer, Inc. All rights reserved.
 *
 * IOKit prototypes and stub implementations from upstream IOKitLib sources.
 * DYLD_INTERPOSE macro from dyld source code.  All other code in the file is
 * by Asahi Linux contributors.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <dlfcn.h>
#include <assert.h>
#include <inttypes.h>

#include <mach/mach.h>
#include <IOKit/IOKitLib.h>

#include "selectors.h"
#include "cmdstream.h"
#include "io.h"
#include "decode.h"
#include "util.h"

/* Apple macro */

#define DYLD_INTERPOSE(_replacment,_replacee) \
	__attribute__((used)) static struct{ const void* replacment; const void* replacee; } _interpose_##_replacee \
	__attribute__ ((section ("__DATA,__interpose"))) = { (const void*)(unsigned long)&_replacment, (const void*)(unsigned long)&_replacee };

mach_port_t metal_connection = 0;

kern_return_t
wrap_IOConnectCallMethod(
	mach_port_t	 connection,		// In
	uint32_t	 selector,		// In
	const uint64_t	*input,			// In
	uint32_t	 inputCnt,		// In
	const void	*inputStruct,		// In
	size_t		 inputStructCnt,	// In
	uint64_t	*output,		// Out
	uint32_t	*outputCnt,		// In/Out
	void		*outputStruct,		// Out
	size_t		*outputStructCntP)	// In/Out
{
	/* Heuristic guess which connection is Metal, skip over I/O from everything else */
	bool bail = false;

	if (metal_connection == 0) {
		if (selector == AGX_SELECTOR_SET_API)
			metal_connection = connection;
		else
			bail = true;
	} else if (metal_connection != connection)
		bail = true;

	if (bail)
		return IOConnectCallMethod(connection, selector, input, inputCnt, inputStruct, inputStructCnt, output, outputCnt, outputStruct, outputStructCntP);

	/* Check the arguments make sense */
	assert((input != NULL) == (inputCnt != 0));
	assert((inputStruct != NULL) == (inputStructCnt != 0));
	assert((output != NULL) == (outputCnt != 0));
	assert((outputStruct != NULL) == (outputStructCntP != 0));

	/* Dump inputs */
	switch (selector) {
	case AGX_SELECTOR_SET_API:
		assert(input == NULL && output == NULL && outputStruct == NULL);
		assert(inputStruct != NULL && inputStructCnt == 16);
		assert(((uint8_t *) inputStruct)[15] == 0x0);

		printf("%X: SET_API(%s)\n", connection, (const char *) inputStruct);
		break;

	case AGX_SELECTOR_SUBMIT_COMMAND_BUFFERS:
		assert(output == NULL && outputStruct == NULL);
		assert(inputStructCnt == 40);
		assert(inputCnt == 1);
		
		printf("%X: SUBMIT_COMMAND_BUFFERS command queue id:%llx %p\n", connection, input[0], inputStruct);

		const struct agx_submit_cmdbuf_req *req = inputStruct;

		pandecode_cmdstream(req->cmdbuf, false);

		if (getenv("ASAHI_DUMP"))
			pandecode_dump_mappings();

		/* fallthrough */
	default:
		printf("%X: call %s (out %p, %zu)", connection, wrap_selector_name(selector), outputStructCntP, outputStructCntP ? *outputStructCntP : 0);

		for (uint64_t u = 0; u < inputCnt; ++u)
			printf(" %llx", input[u]);

		if(inputStructCnt) {
			printf(", struct:\n");
			hexdump(stdout, inputStruct, inputStructCnt, true);
		} else {
			printf("\n");
		}
		
		break;
	}

	/* Invoke the real method */
	kern_return_t ret = IOConnectCallMethod(connection, selector, input, inputCnt, inputStruct, inputStructCnt, output, outputCnt, outputStruct, outputStructCntP);

	printf("return %u", ret);

	/* Dump the outputs */
	if(outputCnt) {
		printf("%u scalars: ", *outputCnt);

		for (uint64_t u = 0; u < *outputCnt; ++u)
			printf("%llx ", output[u]);

		printf("\n");
	}

	if(outputStructCntP) {
		printf(" struct\n");
		hexdump(stdout, outputStruct, *outputStructCntP, true);

		if (selector == 2) {
			/* Dump linked buffer as well */
			void **o = outputStruct;
			hexdump(stdout, *o, 64, true);
		}
	}

	printf("\n");

	/* Track allocations for later analysis (dumping, disassembly, etc) */
	switch (selector) {
	case AGX_SELECTOR_CREATE_CMDBUF: {
		assert(inputCnt == 2);
		assert((*outputStructCntP) == 0x10);
		uint64_t *inp = (uint64_t *) input;
		assert(inp[1] == 1 || inp[1] == 0);
		uint64_t *ptr = (uint64_t *) outputStruct;
		uint32_t *words = (uint32_t *) (ptr + 1);

		pandecode_track_alloc((struct agx_allocation) {
			.index = words[1],
			.map = (void *) *ptr,
			.size = words[0],
			.type = inp[1] ? AGX_ALLOC_CMDBUF : AGX_ALLOC_MEMMAP
		});
		break;
	}
	
	case AGX_SELECTOR_ALLOCATE_MEM: {
		assert((*outputStructCntP) == 0x50);
		uint64_t *iptrs = (uint64_t *) inputStruct;
		uint64_t *ptrs = (uint64_t *) outputStruct;
		uint64_t gpu_va = ptrs[0];
		uint64_t cpu = ptrs[1];
		uint64_t cpu_fixed_1 = iptrs[6];
		uint64_t cpu_fixed_2 = iptrs[7]; /* xxx what's the diff? */
		if (cpu && cpu_fixed_1)
			assert(cpu == cpu_fixed_1);
#if 0
		/* TODO: what about this case? */
		else if (cpu == 0)
			cpu = cpu_fixed_1;
#endif
		uint64_t size = ptrs[4];
		uint32_t *iwords = (uint32_t *) inputStruct;
		const char *type = agx_memory_type_name(iwords[20]);
		printf("allocate gpu va %llx, cpu %llx, 0x%llx bytes ", gpu_va, cpu, size);
		if (type)
			printf(" %s\n", type);
		else
			printf(" unknown type %08X\n", iwords[20]);

		pandecode_track_alloc((struct agx_allocation) {
			.type = AGX_ALLOC_REGULAR,
			.size = size,
			.index = ptrs[3] >> 32ull,
			.gpu_va = gpu_va,
			.map = (void *) cpu,
		});
	}

	default:
		break;
	}

	return ret;
}

kern_return_t
wrap_IOConnectCallAsyncMethod(
        mach_port_t      connection,            // In
        uint32_t         selector,              // In
        mach_port_t      wakePort,              // In
        uint64_t        *reference,             // In
        uint32_t         referenceCnt,          // In
        const uint64_t  *input,                 // In
        uint32_t         inputCnt,              // In
        const void      *inputStruct,           // In
        size_t           inputStructCnt,        // In
        uint64_t        *output,                // Out
        uint32_t        *outputCnt,             // In/Out
        void            *outputStruct,          // Out
        size_t          *outputStructCntP)      // In/Out
{
	/* Check the arguments make sense */
	assert((input != NULL) == (inputCnt != 0));
	assert((inputStruct != NULL) == (inputStructCnt != 0));
	assert((output != NULL) == (outputCnt != 0));
	assert((outputStruct != NULL) == (outputStructCntP != 0));

	printf("%X: call %X, wake port %X (out %p, %zu)", connection, selector, wakePort, outputStructCntP, outputStructCntP ? *outputStructCntP : 0);

	for (uint64_t u = 0; u < inputCnt; ++u)
		printf(" %llx", input[u]);

	if(inputStructCnt) {
		printf(", struct:\n");
		hexdump(stdout, inputStruct, inputStructCnt, true);
	} else {
		printf("\n");
	}

	printf(", references: ");
	for (unsigned i = 0; i < referenceCnt; ++i)
		printf(" %llx", reference[i]);
	printf("\n");

	kern_return_t ret = IOConnectCallAsyncMethod(connection, selector, wakePort, reference, referenceCnt, input, inputCnt, inputStruct, inputStructCnt, output, outputCnt, outputStruct, outputStructCntP);

	printf("return %u", ret);

 	if(outputCnt) {
		printf("%u scalars: ", *outputCnt);

		for (uint64_t u = 0; u < *outputCnt; ++u)
			printf("%llx ", output[u]);

		printf("\n");
	}

	if(outputStructCntP) {
		printf(" struct\n");
		hexdump(stdout, outputStruct, *outputStructCntP, true);

		if (selector == 2) {
			/* Dump linked buffer as well */
			void **o = outputStruct;
			hexdump(stdout, *o, 64, true);
		}
	}

	printf("\n");
	return ret;
}

kern_return_t
wrap_IOConnectCallStructMethod(
        mach_port_t      connection,            // In
        uint32_t         selector,              // In
        const void      *inputStruct,           // In
        size_t           inputStructCnt,        // In
        void            *outputStruct,          // Out
        size_t          *outputStructCntP)       // In/Out
{
	return wrap_IOConnectCallMethod(connection, selector, NULL, 0, inputStruct, inputStructCnt, NULL, NULL, outputStruct, outputStructCntP);
}

kern_return_t
wrap_IOConnectCallAsyncStructMethod(
        mach_port_t      connection,            // In
        uint32_t         selector,              // In
        mach_port_t      wakePort,              // In
        uint64_t        *reference,             // In
        uint32_t         referenceCnt,          // In
        const void      *inputStruct,           // In
        size_t           inputStructCnt,        // In
        void            *outputStruct,          // Out
        size_t          *outputStructCnt)       // In/Out
{
    return wrap_IOConnectCallAsyncMethod(connection,   selector, wakePort,
                                    reference,    referenceCnt,
                                    NULL,         0,
                                    inputStruct,  inputStructCnt,
                                    NULL,         NULL,
                                    outputStruct, outputStructCnt);
}

kern_return_t
wrap_IOConnectCallScalarMethod(
        mach_port_t      connection,            // In
        uint32_t         selector,              // In
        const uint64_t  *input,                 // In
        uint32_t         inputCnt,              // In
        uint64_t        *output,                // Out
        uint32_t        *outputCnt)             // In/Out
{
    return wrap_IOConnectCallMethod(connection, selector,
                               input,      inputCnt,
                               NULL,       0,
                               output,     outputCnt,
                               NULL,       NULL);
}

kern_return_t
wrap_IOConnectCallAsyncScalarMethod(
        mach_port_t      connection,            // In
        uint32_t         selector,              // In
        mach_port_t      wakePort,              // In
        uint64_t        *reference,             // In
        uint32_t         referenceCnt,          // In
        const uint64_t  *input,                 // In
        uint32_t         inputCnt,              // In
        uint64_t        *output,                // Out
        uint32_t        *outputCnt)             // In/Out
{
    return wrap_IOConnectCallAsyncMethod(connection, selector, wakePort,
                                    reference,  referenceCnt,
                                    input,      inputCnt,
                                    NULL,       0,
                                    output,    outputCnt,
                                    NULL,      NULL);
}

kern_return_t
wrap_IOConnectSetNotificationPort(
	io_connect_t	connect,
	uint32_t	type,
	mach_port_t	port,
	uintptr_t	reference )
{
	printf("connect %X, type %X, to notification port %X, with reference %lx\n", connect, type, port, reference);
	kern_return_t ret = IOConnectSetNotificationPort(connect, type, port, reference);
	printf("return %u\n", ret);
	return ret;
}

kern_return_t
wrap_IOSetNotificationPort(
	mach_port_t	connect,
	uint32_t	type,
	mach_port_t	port )
{
	return wrap_IOConnectSetNotificationPort(connect, type, port, 0);
}

IONotificationPortRef
wrap_IONotificationPortCreate(
	mach_port_t	masterPort )
{
	IONotificationPortRef ref = IONotificationPortCreate(masterPort);
	printf("creating notification port from master %X --> %p\n", masterPort, ref);
	return ref;
}

void
wrap_IONotificationPortSetDispatchQueue(IONotificationPortRef notify, dispatch_queue_t queue)
{
	printf("set dispatch queue %p to queue %p\n", notify, queue);
	IONotificationPortSetDispatchQueue(notify, queue);
}

mach_port_t
wrap_IODataQueueAllocateNotificationPort()
{
	mach_port_t ret = IODataQueueAllocateNotificationPort();
	printf("data queue notif port %X\n", ret);
	return ret;
}

IOReturn
wrap_IODataQueueSetNotificationPort(IODataQueueMemory *dataQueue, mach_port_t notifyPort)
{
	IOReturn ret = IODataQueueSetNotificationPort(dataQueue, notifyPort);
	printf("data queue %p set notif port %X -> %X\n", dataQueue, notifyPort, ret);
	return ret;
}

DYLD_INTERPOSE(wrap_IOConnectCallMethod, IOConnectCallMethod);
DYLD_INTERPOSE(wrap_IOConnectCallAsyncMethod, IOConnectCallAsyncMethod);
DYLD_INTERPOSE(wrap_IOConnectCallStructMethod, IOConnectCallStructMethod);
DYLD_INTERPOSE(wrap_IOConnectCallAsyncStructMethod, IOConnectCallAsyncStructMethod);
DYLD_INTERPOSE(wrap_IOConnectCallScalarMethod, IOConnectCallScalarMethod);
DYLD_INTERPOSE(wrap_IOConnectCallAsyncScalarMethod, IOConnectCallAsyncScalarMethod);
DYLD_INTERPOSE(wrap_IOConnectSetNotificationPort, IOConnectSetNotificationPort);
//DYLD_INTERPOSE(wrap_IOSetNotificationPort, IOSetNotificationPort);
DYLD_INTERPOSE(wrap_IONotificationPortCreate, IONotificationPortCreate);
DYLD_INTERPOSE(wrap_IONotificationPortSetDispatchQueue, IONotificationPortSetDispatchQueue);
DYLD_INTERPOSE(wrap_IODataQueueAllocateNotificationPort, IODataQueueAllocateNotificationPort);
DYLD_INTERPOSE(wrap_IODataQueueSetNotificationPort, IODataQueueSetNotificationPort);
