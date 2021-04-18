/*
 * Copyright (C) 2018 Alyssa Rosenzweig <alyssa@rosenzweig.io>
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

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <X11/extensions/XShm.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include "demo.h"

Display *d;
Window w;
XImage *image;
XShmSegmentInfo shminfo;
GC gc;


void slowfb_cleanup() {
	XShmDetach(d, &shminfo);
	XDestroyImage(image);
	shmdt(shminfo.shmaddr);
	shmctl(shminfo.shmid, IPC_RMID, 0);
}

void handle_sig(int sig) {
	slowfb_cleanup();
	exit(0);
}

struct slowfb slowfb_init(int width, int height) {
	d = XOpenDisplay(NULL);
	assert(d != NULL);
	int black = BlackPixel(d, DefaultScreen(d));
	w = XCreateSimpleWindow(d, DefaultRootWindow(d), 0, 0, width, height, 0, black, black);
	XSelectInput(d, w, StructureNotifyMask);
	XMapWindow(d, w);
	gc = XCreateGC(d, w, 0, NULL);
	for (;;) {
		XEvent e;
		XNextEvent(d, &e);
		if (e.type == MapNotify) break;
	}
	printf("ok\n");
	image = XShmCreateImage(d, DefaultVisual(d, 0), 24, ZPixmap, 0, &shminfo, width, height);
	assert(image != NULL);
	shminfo.shmid = shmget(IPC_PRIVATE, image->bytes_per_line * image->height, IPC_CREAT|S_IRUSR|S_IWUSR);
	if (shminfo.shmid < 0) {
		printf("uh oh %u\n", errno);
		exit(1);
	}
	printf("shm id %u of size %u\n", shminfo.shmid, image->bytes_per_line * image->height);
	shminfo.shmaddr = image->data = shmat(shminfo.shmid, 0, 0);
	printf("shmat %p\n", image->data);
	shminfo.readOnly = 0;
	XShmAttach(d, &shminfo);
	if (!image->data)
		assert(0);
	signal(SIGINT, handle_sig);
	return (struct slowfb) {
		.map = image->data,
		.stride = image->bytes_per_line
	};
}

void slowfb_update(int width, int height) {
	XShmPutImage(d, w, gc, image, 0, 0, 0, 0, width, height, 1);
	for (;;) {
		XEvent e;
		XNextEvent(d, &e);
		if (e.type == (XShmGetEventBase(d) + ShmCompletion)) break;
	}

}

