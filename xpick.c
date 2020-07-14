#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/select.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MINMAX(min, max, val) ((val) > (min) && (val) < (max) ? (val) : \
                               ((val) - ((max)+(min))/2 > 0 ? (max) : (min)))
#define rootw(dpy) WidthOfScreen(DefaultScreenOfDisplay(dpy))
#define rooth(dpy) HeightOfScreen(DefaultScreenOfDisplay(dpy))
#define rootimg(dpy) XGetImage(dpy, DefaultRootWindow(dpy), 0, 0, \
                               rootw(dpy), rooth(dpy), AllPlanes, ZPixmap);
#define XevKeysym(dpy, ev) XkbKeycodeToKeysym(dpy, (ev).xkey.keycode, 0, 0)

int done = 0;
char *cmd;

void
finish(int signal)
{
	done = 1;
}

void
usage(FILE *output)
{
	fprintf(output, "Usage: %s [-mr] [-s scale] [-i increment] [-l length]" \
	                " [-w width] [-g height] [-h]\n", cmd);
}

XImage *
allocimg(Display *dpy, int w, int h, int s)
{
	static XImage *img;
	w = MAX(s, w/s * s);
	h = MAX(s, h/s * s);

	img = XCreateImage(dpy, DefaultVisual(dpy, DefaultScreen(dpy)), \
	                   DefaultDepth(dpy, DefaultScreen(dpy)), \
	                   ZPixmap, 0, NULL, w, h, 32, 0);
	if (img) {
		img->data = malloc(img->bytes_per_line * h);
		XInitImage(img);
	}

	return img;
}

void
wincontent(Display *dpy, Window win, GC gci, GC gcl, XImage *src, XImage *img, \
           int x, int y, int w, int h, int s, int mflag)
{
	int i, j, ii, jj;
	unsigned long p;
	w = MAX(s, w/s * s);
	h = MAX(s, h/s * s);

	XMoveResizeWindow(dpy, win, MINMAX(0, rootw(dpy) - w, x - w/2), \
	                  MINMAX(0, rooth(dpy) - h, y - h/2), w, h);

	for (i = 0; i < w/s; i++) {
		for (j = 0; j < h/s; j++) {
			p = XGetPixel(src, MINMAX(0, src->width - 1, x + i - w/s/2), \
			              MINMAX(0, src->height - 1, y + j - h/s/2));
			for (ii = 0; ii < s; ii++) for (jj = 0; jj < s; jj++)
				XPutPixel(img, i*s + ii, j*s + jj, p);
		}
	}

	XPutImage(dpy, win, gci, img, 0, 0, 0, 0, w, h);
	if (!mflag)
		XDrawRectangle(dpy, win, gcl, w/s/2*s, h/s/2*s, s, s);
	XDrawRectangle(dpy, win, gcl, 0, 0, w-1, h-1);
	XFlush(dpy);
}

void
refresh(Display *dpy, Window win, XImage **orig)
{
	XUnmapWindow(dpy, win);
	XSync(dpy, False);
	XDestroyImage(*orig);
	*orig = rootimg(dpy);
	XMapWindow(dpy, win);
}

void
printcolor(Display *dpy, XImage *img, FILE *output)
{
	XColor c;
	c.pixel = XGetPixel(img, img->width/2, img->height/2);
	XQueryColor(dpy, DefaultColormap(dpy, DefaultScreen(dpy)), &c);
	fprintf(output, "#%02x%02x%02x", c.red >> 8, c.green >> 8, c.blue >> 8);
	if (!done)
		fprintf(output, "\n");
}

int
intarg(int *argc, char **argv[], char **opt)
{
	int ret;

	if ((*opt)[1]) {
		*opt += 1;
		ret = (int)strtol(*opt, NULL, 0);
		*opt += strlen(*opt) - 1;
	} else if ((*argv)[1]) {
		*argc -= 1; *argv += 1;
		ret = (int)strtol((*argv)[0], NULL, 0);
	} else {
		fprintf(stderr, "-%c flag needs an argument.\n", **opt);
		exit(1);
	}

	return ret;
}

int
main(int argc, char *argv[])
{
	XImage *orig, *img;
	Display *dpy;
	Window win;
	XSetWindowAttributes sattr;
	XEvent ev;
	GC gci, gcl;
	XGCValues gcval;
	Pixmap empty;
	Cursor cursor;
	int fd;
	fd_set fds;
	struct sigaction action;
	int grab, x, y, w = -30, h = -30, scale = 5, increment = -5;
	int mflag = 0, rflag = 0, square = 1;

	cmd = argv[0];
	for (argc--, argv++; argv[0] && argv[0][0] == '-' && argv[0][1]; argc--, argv++) {
		for (char *opt = ++argv[0]; opt[0]; opt++) {
			switch (*opt) {
			case 'm':
				/* use the program as a magnifier */
				mflag = 1;
				break;
			case 'r':
				/* refresh on pointer motion */
				rflag = 1;
				break;
			case 's':
				/* magnification factor */
				scale = intarg(&argc, &argv, &opt);
				if (scale < 1) {
					fprintf(stderr, "scale must be 1 or more.\n");
					return 1;
				}
				break;
			case 'i':
				/* magnifying area increment. in percent when < 0 */
				increment = intarg(&argc, &argv, &opt);
				if (increment == 0) {
					fprintf(stderr, "increment cannot be zero.\n");
					return 1;
				}
				break;
			case 'l':
				/* side length. in percent when < 0 */
				w = h = intarg(&argc, &argv, &opt);
				if (w == 0) {
					fprintf(stderr, "length cannot be zero.\n");
					return 1;
				}
				square = 1;
				break;
			case 'w':
				/* width */
				w = intarg(&argc, &argv, &opt);
				if (w == 0) {
					fprintf(stderr, "width cannot be zero.\n");
					return 1;
				}
				square = 0;
				break;
			case 'g':
				/* height */
				h = intarg(&argc, &argv, &opt);
				if (h == 0) {
					fprintf(stderr, "height cannot be zero.\n");
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

	dpy = XOpenDisplay(NULL);
	if (dpy == NULL) {
		fprintf(stderr, "Failed to open display.\n");
		return 1;
	}

	if (increment < 0)
		increment = -increment * MIN(rootw(dpy), rooth(dpy)) / 100;
	if (square) {
		if (w < 0)
			w = h = -w * MIN(rootw(dpy), rooth(dpy)) / 100;
	} else {
		if (w < 0)
			w = -w * rootw(dpy) / 100;
		if (h < 0)
			h = -h * rooth(dpy) / 100;
	}

	orig = rootimg(dpy);
	if (orig == NULL) {
		fprintf(stderr, "Failed to get root image.\n");
		done = -1;
		goto close;
	}
	img = allocimg(dpy, w, h, scale);
	if (img == NULL) {
		fprintf(stderr, "Failed to create an auxiliary XImage.\n");
		done = -1;
		goto notimg;
	}

	empty = XCreateBitmapFromData(dpy, DefaultRootWindow(dpy), &(char){0}, 1, 1);
	cursor = XCreatePixmapCursor(dpy, empty, empty, &(XColor){0}, &(XColor){0}, 0, 0);

	XQueryPointer(dpy, DefaultRootWindow(dpy), &(Window){0}, &(Window){0}, \
	              &x, &y, &(int){0}, &(int){0}, &(unsigned int){0});

	sattr.event_mask = ButtonPressMask | PointerMotionMask | KeyPressMask;
	sattr.background_pixel = BlackPixel(dpy, DefaultScreen(dpy));
	sattr.override_redirect = True;
	sattr.cursor = cursor;
	win = XCreateWindow(dpy, DefaultRootWindow(dpy), 0, 0, 1, 1, 0, \
	                    CopyFromParent, InputOutput, CopyFromParent, CWEventMask \
	                    | CWBackPixel | CWOverrideRedirect | CWCursor, &sattr);
	if (!win) {
		fprintf(stderr, "Failed to create a window.\n");
		done = -1;
		goto notwin;
	}
	XMapWindow(dpy, win);
	XSetInputFocus(dpy, win, RevertToPointerRoot, CurrentTime);

	gcval.function = GXcopy;
	gcval.plane_mask = AllPlanes;
	gcval.subwindow_mode = IncludeInferiors;
	gcval.foreground = XWhitePixel(dpy, DefaultScreen(dpy));
	gcval.background = XBlackPixel(dpy, DefaultScreen(dpy));
	gci = XCreateGC(dpy, DefaultRootWindow(dpy), GCFunction | GCPlaneMask \
			| GCSubwindowMode | GCForeground | GCBackground, &gcval);

	gcval.function = GXxor;
	gcval.line_width = 1;
	gcl = XCreateGC(dpy, DefaultRootWindow(dpy), GCFunction | GCLineWidth \
			| GCSubwindowMode | GCForeground | GCBackground, &gcval);

	wincontent(dpy, win, gci, gcl, orig, img, x, y, w, h, scale, mflag);

	/* try to grab pointer to not let it out of the window */
	grab = XGrabPointer(dpy, win, True, NoEventMask, GrabModeAsync, \
	                    GrabModeAsync, win, None, CurrentTime);

	fd = ConnectionNumber(dpy);
	while (!done) {
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		select(fd + 1, &fds, NULL, NULL, NULL);
		while (XPending(dpy)) {
			XNextEvent(dpy, &ev);
			switch (ev.type) {
			case MotionNotify:
				x = ev.xbutton.x_root;
				y = ev.xbutton.y_root;
				if (rflag)
					refresh(dpy, win, &orig);
				wincontent(dpy, win, gci, gcl, orig, img, \
				           x, y, w, h, scale, mflag);
				break;
			case ButtonPress:
				done = 1;
				if (!mflag)
					printcolor(dpy, img, stdout);
				break;
			case KeyPress:
				switch (XevKeysym(dpy, ev)) {
				case XK_Return:
					done = 1;
					printcolor(dpy, img, stdout);
					break;
				case XK_space:
					printcolor(dpy, img, stdout);
					break;
				/* XWrapPointer should generate MotionNotify */
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
					break;
				case XK_r:
					refresh(dpy, win, &orig);
					wincontent(dpy, win, gci, gcl, orig, img, \
					           x, y, w, h, scale, mflag);
					break;
				case XK_m:
					mflag = !mflag;
					wincontent(dpy, win, gci, gcl, orig, img, \
					           x, y, w, h, scale, mflag);
					break;
				case XK_t:
					rflag = !rflag;
					break;
				case XK_p:
					w = h = MIN(w, h);
					scale = w/9;
					goto changed;
				case XK_i:
					h += increment;
					w += increment;
					goto changed;
				case XK_d:
					h = MAX(1, h - increment);
					w = MAX(1, w - increment);
					goto changed;
				case XK_minus:
				case XK_s:
					scale = MAX(1, scale - 1);
					goto changed;
				case XK_equal:
				case XK_o:
					scale = scale + 1;
					goto changed;
				case XK_1:
					scale = 1;
					goto changed;
				case XK_2:
					scale = 2;
					goto changed;
				case XK_3:
					scale = 3;
					goto changed;
				case XK_4:
					scale = 4;
					goto changed;
				case XK_5:
					scale = 5;
					goto changed;
				case XK_6:
					scale = 6;
					goto changed;
				case XK_7:
					scale = 7;
					goto changed;
				case XK_8:
					scale = 8;
					goto changed;
				case XK_9:
					scale = 9;
					goto changed;
				case XK_0:
					scale = 10;
					goto changed;
changed:
					XDestroyImage(img);
					img = allocimg(dpy, w, h, scale);
					wincontent(dpy, win, gci, gcl, orig, img, \
					           x, y, w, h, scale, mflag);
					break;
				case XK_Escape:
				case XK_q:
					done = 1;
					break;
				default:
					fprintf(stderr, "Unknown key '%s'. Exitting.\n", \
					        XKeysymToString(XevKeysym(dpy, ev)));
					done = -1;
					break;
				}
				break;
			default:
				break;
			}
		}
	}

	XFreeGC(dpy, gcl);
	XFreeGC(dpy, gci);
	if (grab == GrabSuccess)
		XUngrabPointer(dpy, CurrentTime);
	XDestroyWindow(dpy, win);
notwin:
	XFreeCursor(dpy, cursor);
	XDestroyImage(img);
notimg:
	XDestroyImage(orig);
close:
	XCloseDisplay(dpy);

	return done < 0 ? 1 : 0;
}
