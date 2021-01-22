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
#include <mach/mach.h>
#include <IOKit/IOKitLib.h>
#include "selectors.h"
#include "demo.h"

/* Sample code for opening a connection to the AGX kernel module via IOKit */

#define AGX_SERVICE_TYPE 0x100005

int main(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	kern_return_t ret;

	/* TODO: Support other models */
	CFDictionaryRef matching = IOServiceNameMatching("AGXAcceleratorG13G_B0");

	io_service_t service =
		IOServiceGetMatchingService(kIOMasterPortDefault, matching);

	if (!service) {
		fprintf(stderr, "G13 (B0) accelerator not found\n");
		return 1;
	}

	io_connect_t connection = 0;
	ret = IOServiceOpen(service, mach_task_self(), AGX_SERVICE_TYPE, &connection);

	if (ret) {
		fprintf(stderr, "Error from IOServiceOpen: %u\n", ret);
		return 1;
	}

	const char *api = "Equestria";
	char in[16] = { 0 };
	assert(strlen(api) < sizeof(in));
	memcpy(in, api, strlen(api));

	ret = IOConnectCallStructMethod(connection, AGX_SELECTOR_SET_API, in,
			sizeof(in), NULL, NULL);

	/* Oddly, the return codes are flipped for SET_API */
	if (ret != 1) {
		fprintf(stderr, "Error setting API: %u\n", ret);
		return 1;
	}

	char version[456] = { 0 };
	size_t version_len = sizeof(version);

	ret = IOConnectCallStructMethod(connection, AGX_SELECTOR_GET_VERSION, NULL, 0,
			version, &version_len);

	if (ret) {
		fprintf(stderr, "Error getting version: %u\n", ret);
		/* TODO: why? */
	}

	assert(version_len == sizeof(version));
	printf("Kext build date: %s\n", version + (25 * 8));

	demo(connection, getenv("DISPLAY") == NULL);

	ret = IOServiceClose(connection);

	if (ret) {
		fprintf(stderr, "Error from IOServiceClose: %u\n", ret);
		return 1;
	}
}
