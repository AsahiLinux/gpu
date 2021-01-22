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
#include <stdio.h>
#include <stdint.h>
#include <assert.h>

Display *d;
Window w;
XImage *image;
GC gc;

void slowfb_init(uint8_t *framebuffer, int width, int height) {
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
	image = XCreateImage(d, DefaultVisual(d, 0), 24, ZPixmap, 0, (void *) framebuffer, width, height, 32, 0);
}

void slowfb_update(int width, int height) {
	XPutImage(d, w, gc, image, 0, 0, 0, 0, width, height);
}
