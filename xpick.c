#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <poll.h>
#include <time.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MINMAX(min, max, val) ((val) >= (min) && (val) <= (max) ? (val) : \
                               ((val) > (max) ? (max) : (min)))

void finish(int signal);
void usage(FILE *output);
long intarg(int *argc, char ***argv, char **opt);
void winchanged(void);
void wincontent(void);
void refresh(void);
void focus(void);
void grabpointer(void);
void printcolor(int newline);
void keypress(XKeyEvent *ev);

int done = 0;
char *cmd;
Display *dpy;
XImage *simg = NULL, *img = NULL;
GC gci, gcl;
Pixmap empty;
Cursor cursor;
Window win = 0, swin = 0, root;
int scr, sx, sy, sw, sh;
int x, y, w, h, scale, increment;
int opt_n, opt_m, opt_r;

void
finish(int signal)
{
	done = -1;
}

void
usage(FILE *output)
{
	fprintf(output, "Usage: %s [-amnr] [-s scale] [-i increment] [-l length]" \
	                " [-w width] [-g height] [-f windowid] [-h]\n", cmd);
}

long
intarg(int *argc, char ***argv, char **opt)
{
	char *str, *end;
	long ret;

	if ((*opt)[1]) {
		str = *opt + 1;
		*opt += strlen(*opt) - 1;
	} else if ((*argv)[1]) {
		*argc -= 1; *argv += 1;
		str = **argv;
	} else {
		fprintf(stderr, "-%c flag needs an argument.\n", **opt);
		exit(1);
	}

	ret = strtol(str, &end, 0);

	if (*end) {
		fprintf(stderr, "Cannot convert '%s' to int.\n", str);
		exit(1);
	}

	return ret;
}

void
winchanged()
{
	int lw = MINMAX(scale, sw, w/scale * scale);
	int lh = MINMAX(scale, sh, h/scale * scale);

	if (img)
		XDestroyImage(img);

	img = XCreateImage(dpy, DefaultVisual(dpy, scr), DefaultDepth(dpy, scr), \
	                   ZPixmap, 0, NULL, lw, lh, 32, 0);
	if (img) {
		img->data = malloc(img->bytes_per_line * lh);
		XInitImage(img);
	} else {
		return;
	}

	wincontent();
}

void
wincontent()
{
	int i, j, ii, jj;
	unsigned long p;
	int lw = MINMAX(scale, sw, w/scale * scale);
	int lh = MINMAX(scale, sh, h/scale * scale);

	XMoveResizeWindow(dpy, win, MINMAX(sx, sx + sw - lw, x - lw/2), \
	                  MINMAX(sy, sy + sh - lh, y - lh/2), lw, lh);

	for (i = 0; i < lw/scale; i++) {
		for (j = 0; j < lh/scale; j++) {
			p = XGetPixel(simg, MINMAX(0, sw - 1, x - sx + i - lw/scale/2), \
			              MINMAX(0, sh - 1, y - sy + j - lh/scale/2));
			for (ii = 0; ii < scale; ii++) for (jj = 0; jj < scale; jj++)
				XPutPixel(img, i*scale + ii, j*scale + jj, p);
		}
	}

	XPutImage(dpy, win, gci, img, 0, 0, 0, 0, lw, lh);
	if (!opt_m)
		XDrawRectangle(dpy, win, gcl, lw/scale/2*scale, \
		               lh/scale/2*scale, scale, scale);
	XDrawRectangle(dpy, win, gcl, 0, 0, lw-1, lh-1);
	XFlush(dpy);
}

void
refresh()
{
	XUnmapWindow(dpy, win);
	XSync(dpy, False);
	XDestroyImage(simg);
	simg = XGetImage(dpy, swin, 0, 0, sw, sh, AllPlanes, ZPixmap);
	XMapWindow(dpy, win);
	grabpointer();
}

void
focus()
{
	struct timespec ts = {.tv_sec = 0, .tv_nsec = 10000000};
	static int count = 0;
	Window current;
	int i = 0;

	if (count++ > 100)
		return;

	while (i++ < 100) {
		XGetInputFocus(dpy, &current, &(int){0});
		if (current == win)
			return;
		XSetInputFocus(dpy, win, RevertToParent, CurrentTime);
		XRaiseWindow(dpy, win);
		nanosleep(&ts, NULL);
	}
}

void
grabpointer()
{
	if (XGrabPointer(dpy, win, True, NoEventMask, GrabModeAsync, \
	                 GrabModeAsync, win, None, CurrentTime) != GrabSuccess)
		fputs("Warning: cannot grab the pointer.\n", stdout);
}

void
printcolor(int newline)
{
	XColor c;
	c.pixel = XGetPixel(img, img->width/2, img->height/2);
	XQueryColor(dpy, DefaultColormap(dpy, scr), &c);
	printf("#%02x%02x%02x", c.red >> 8, c.green >> 8, c.blue >> 8);
	if (newline)
		putc('\n', stdout);
}

void
keypress(XKeyEvent *ev)
{
	KeySym key = XkbKeycodeToKeysym(dpy, ev->keycode, 0, 0);
	switch (key) {
	case XK_Return:
		done = 1;
		printcolor(opt_n);
		break;
	case XK_space:
		printcolor(1);
		break;
	/* XWarpPointer should generate MotionNotify */
	case XK_k:
	case XK_Up:
		XWarpPointer(dpy, None, None, 0, 0, 0, 0, \
			     0, -MAX(1, h/scale/2 - 3));
		break;
	case XK_j:
	case XK_Down:
		XWarpPointer(dpy, None, None, 0, 0, 0, 0, \
			     0, MAX(1, h/scale/2 - 3));
		break;
	case XK_h:
	case XK_Left:
		XWarpPointer(dpy, None, None, 0, 0, 0, 0, \
			     -MAX(1, w/scale/2 - 3), 0);
		break;
	case XK_l:
	case XK_Right:
		XWarpPointer(dpy, None, None, 0, 0, 0, 0, \
			     MAX(1, w/scale/2 - 3), 0);
		break;
	case XK_r:
		if (ev->state & ShiftMask) {
			opt_r ^= 1;
		} else {
			refresh();
			wincontent();
		}
		break;
	case XK_m:
		opt_m ^= 1;
		wincontent();
		break;
	case XK_p:
		w = h = MIN(w, h);
		scale = MAX(1, w/9);
		winchanged();
		break;
	case XK_i:
		if (ev->state & ShiftMask)
			scale = MAX(2, scale * (h + increment) / h);
		h += increment;
		w += increment;
		winchanged();
		break;
	case XK_d:
		if (ev->state & ShiftMask)
			scale = MAX(1, scale * MAX(1, h - increment) / h);
		h = MAX(1, h - increment);
		w = MAX(1, w - increment);
		winchanged();
		break;
	case XK_minus:
	case XK_s:
		scale = MAX(1, scale - 1);
		winchanged();
		break;
	case XK_equal:
	case XK_o:
		scale = scale + 1;
		winchanged();
		break;
	case XK_1:
		scale = 1;
		winchanged();
		break;
	case XK_2:
		scale = 2;
		winchanged();
		break;
	case XK_3:
		scale = 3;
		winchanged();
		break;
	case XK_4:
		scale = 4;
		winchanged();
		break;
	case XK_5:
		scale = 5;
		winchanged();
		break;
	case XK_6:
		scale = 6;
		winchanged();
		break;
	case XK_7:
		scale = 7;
		winchanged();
		break;
	case XK_8:
		scale = 8;
		winchanged();
		break;
	case XK_9:
		scale = 9;
		winchanged();
		break;
	case XK_0:
		scale = 10;
		winchanged();
		break;
	/* Ignore Shift modifier keys */
	case XK_Shift_L:
	case XK_Shift_R:
		break;
	case XK_Escape:
	case XK_q:
		done = 1;
		break;
	default:
		fprintf(stderr, "Unknown key '%s'. Exitting.\n", \
			XKeysymToString(key));
		done = -1;
		break;
	}
}

int
main(int argc, char *argv[])
{
	XSetWindowAttributes sattr;
	XWindowAttributes attr;
	XGCValues gcval;
	XEvent ev;
	struct pollfd fds[1];
	struct sigaction action;
	int square = 1;
	w = h = -30; scale = 5; increment = -5;
	opt_n = 1; opt_m = 0; opt_r = 0;

	cmd = argv[0];
	for (argc--, argv++; argv[0]; argc--, argv++) {
		if (argv[0][0] != '-') {
			fprintf(stderr, "Unknown argument %s.\n", argv[0]);
			usage(stderr);
			return 1;
		}
		for (char *opt = ++argv[0]; opt[0]; opt++) {
			switch (*opt) {
			case 'a':
				/* use the window under the pointer (see -f) */
				swin = ~0;
				break;
			case 'f':
				/* pick a color from a window, not root */
				swin = intarg(&argc, &argv, &opt);
				break;
			case 'm':
				/* use the program as a magnifier */
				opt_m = 1;
				break;
			case 'n':
				/* don't print a newline after the last color */
				opt_n = 0;
				break;
			case 'r':
				/* refresh on pointer motion */
				opt_r = 1;
				break;
			case 's':
				/* magnification factor */
				scale = intarg(&argc, &argv, &opt);
				if (scale < 1) {
					fputs("scale must be 1 or more.\n", stderr);
					return 1;
				}
				break;
			case 'i':
				/* magnifying area increment. in percent when < 0 */
				increment = intarg(&argc, &argv, &opt);
				if (increment == 0) {
					fputs("increment cannot be zero.\n", stderr);
					return 1;
				}
				break;
			case 'l':
				/* side length. in percent when < 0 */
				w = h = intarg(&argc, &argv, &opt);
				if (w == 0) {
					fputs("length cannot be zero.\n", stderr);
					return 1;
				}
				square = 1;
				break;
			case 'w':
				/* width */
				w = intarg(&argc, &argv, &opt);
				if (w == 0) {
					fputs("width cannot be zero.\n", stderr);
					return 1;
				}
				square = 0;
				break;
			case 'g':
				/* height */
				h = intarg(&argc, &argv, &opt);
				if (h == 0) {
					fputs("height cannot be zero.\n", stderr);
					return 1;
				}
				square = 0;
				break;
			case 'h':
				usage(stdout);
				return 0;
			default:
				fprintf(stderr, "Unknown option -%c.\n", *opt);
				usage(stderr);
				return 1;
			}
		}
	}

	memset(&action, 0, sizeof(struct sigaction));
	action.sa_handler = finish;
	sigaction(SIGINT, &action, NULL);
	sigaction(SIGTERM, &action, NULL);

	if (!(dpy = XOpenDisplay(NULL))) {
		fputs("Failed to open display.\n", stderr);
		return 1;
	}
	scr = DefaultScreen(dpy);
	root = DefaultRootWindow(dpy);

	if (swin == ~0)
		XQueryPointer(dpy, root, &(Window){0}, &swin, &x, &y, \
		              &(int){0}, &(int){0}, &(unsigned int){0});
	else
		XQueryPointer(dpy, root, &(Window){0}, &(Window){0}, &x, &y, \
		              &(int){0}, &(int){0}, &(unsigned int){0});

	if (!swin)
		swin = root;
	if (!XGetWindowAttributes(dpy, swin, &attr)) {
		fprintf(stderr, "Failed to get window attributes: 0x%lx.\n", swin);
		done = -1;
		goto close;
	}

	sx = attr.x; sy = attr.y; sw = attr.width; sh = attr.height;

	if (increment < 0)
		increment = -increment * MIN(sw, sh) / 100;
	if (square) {
		if (w < 0)
			w = h = -w * MIN(sw, sh) / 100;
	} else {
		if (w < 0)
			w = -w * sw / 100;
		if (h < 0)
			h = -h * sh / 100;
	}

	if (x < sx || x >= sx + sw || y < sy || y >= sy + sh) {
		x = sx + sw/2; y = sy + sh/2;
		XWarpPointer(dpy, None, swin, 0, 0, 0, 0, sw/2, sh/2);
	}


	empty = XCreateBitmapFromData(dpy, root, &(char){0}, 1, 1);
	cursor = XCreatePixmapCursor(dpy, empty, empty, &(XColor){0}, &(XColor){0}, 0, 0);

	sattr.event_mask = ButtonPressMask | PointerMotionMask | KeyPressMask \
	                   | FocusChangeMask;
	sattr.background_pixel = BlackPixel(dpy, scr);
	sattr.override_redirect = True;
	sattr.cursor = cursor;
	win = XCreateWindow(dpy, root, 0, 0, 1, 1, 0, CopyFromParent, InputOutput, \
	                    CopyFromParent, CWEventMask | CWBackPixel \
	                    | CWOverrideRedirect | CWCursor, &sattr);
	if (!win) {
		fputs("Failed to create window.\n", stderr);
		done = -1;
		goto notwin;
	}
	XMapWindow(dpy, win);
	focus();

	gcval.function = GXcopy;
	gcval.plane_mask = AllPlanes;
	gcval.subwindow_mode = IncludeInferiors;
	gcval.foreground = XWhitePixel(dpy, DefaultScreen(dpy));
	gcval.background = XBlackPixel(dpy, DefaultScreen(dpy));
	gci = XCreateGC(dpy, root, GCFunction | GCPlaneMask | GCSubwindowMode \
	                | GCForeground | GCBackground, &gcval);

	gcval.function = GXxor;
	gcval.line_width = 1;
	gcl = XCreateGC(dpy, root, GCFunction | GCLineWidth | GCSubwindowMode \
	                | GCForeground | GCBackground, &gcval);

	if (!(simg = XGetImage(dpy, swin, 0, 0, sw, sh, AllPlanes, ZPixmap))) {
		fputs("Failed to get source image.\n", stderr);
		done = -1;
		goto notsimg;
	}
	winchanged();
	if (!img) {
		fputs("Failed to create auxiliary XImage.\n", stderr);
		done = -1;
		goto notimg;
	}

	grabpointer();

	fds[0].fd = ConnectionNumber(dpy);
	fds[0].events = POLLIN;
	while (!done) {
		poll(fds, 1, -1);
		while (!done && XPending(dpy)) {
			XNextEvent(dpy, &ev);
			switch (ev.type) {
			case MotionNotify:
				x = ev.xmotion.x_root;
				y = ev.xmotion.y_root;
				if (opt_r)
					refresh();
				wincontent();
				break;
			case ButtonPress:
				if (ev.xbutton.button == Button4) {
					scale = scale + 1;
					winchanged();
				} else if (ev.xbutton.button == Button5) {
					scale = MAX(1, scale - 1);
					winchanged();
				} else if (ev.xbutton.button < Button4){
					done = 1;
					if (!opt_m)
						printcolor(opt_n);
				}
				break;
			case KeyPress:
				keypress(&ev.xkey);
				break;
			case FocusOut:
				focus();
				break;
			default:
				break;
			}
		}
	}

	/* It can be called even when the pointer is not grabbed. */
	XUngrabPointer(dpy, CurrentTime);
	XDestroyImage(img);
notimg:
	XDestroyImage(simg);
notsimg:
	XFreeGC(dpy, gcl);
	XFreeGC(dpy, gci);
	XDestroyWindow(dpy, win);
notwin:
	XFreeCursor(dpy, cursor);
close:
	XCloseDisplay(dpy);

	return done < 0 ? 1 : 0;
}
