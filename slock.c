/* See LICENSE file for license details. */
#define _XOPEN_SOURCE 500
#if HAVE_SHADOW_H
#include <shadow.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/Xrender.h>
#include <X11/keysym.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <X11/Xft/Xft.h>

#include "arg.h"
#include "util.h"

char *argv0;

/* global count to prevent repeated error messages */
int count_error = 0;

enum {
	INIT,
	INPUT,
	FAILED,
	CAPS,
	NUMCOLS
};

struct lock {
	int screen;
	Window root, win;
	Pixmap pmap;
	unsigned long colors[NUMCOLS];
	Visual *visual;
	Colormap colormap;
};

struct xrandr {
	int active;
	int evbase;
	int errbase;
};

#include "config.h"

static void
die(const char *errstr, ...)
{
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(1);
}

#ifdef __linux__
#include <fcntl.h>
#include <linux/oom.h>

static void
dontkillme(void)
{
	FILE *f;
	const char oomfile[] = "/proc/self/oom_score_adj";

	if (!(f = fopen(oomfile, "w"))) {
		if (errno == ENOENT)
			return;
		die("slock: fopen %s: %s\n", oomfile, strerror(errno));
	}
	fprintf(f, "%d", OOM_SCORE_ADJ_MIN);
	if (fclose(f)) {
		if (errno == EACCES)
			die("slock: unable to disable OOM killer. "
			    "Make sure to suid or sgid slock.\n");
		else
			die("slock: fclose %s: %s\n", oomfile, strerror(errno));
	}
}
#endif


static void
writemessage(Display *dpy, Window win, int screen, struct lock *lock, int passlen)
{
	int len, width, height, s_width, s_height, i, j, k;
	int line_start, total_height;
	XineramaScreenInfo *xsi;
	XftDraw *draw;
	XftColor color;
	XftFont *font, *font_large;
	XGlyphInfo extents;
	char line_buf[256];
	int line_len;

	font = XftFontOpenName(dpy, screen, font_name);
	font_large = XftFontOpenName(dpy, screen, font_name_large);
	if (font == NULL || font_large == NULL) {
		if (count_error == 0) {
			fprintf(stderr, "slock: Unable to load font\n");
			fprintf(stderr, "slock: Try listing fonts with 'fc-list'\n");
			count_error++;
		}
		if (font) XftFontClose(dpy, font);
		if (font_large) XftFontClose(dpy, font_large);
		return;
	}

	draw = XftDrawCreate(dpy, win, lock->visual, lock->colormap);
	XftColorAllocName(dpy, lock->visual, lock->colormap, text_color, &color);

	/*  To prevent "Uninitialized" warnings. */
	xsi = NULL;

	/*
	 * Start formatting and drawing text
	 */

	len = strlen(message);

	/* Count number of lines */
	k = 0;
	for (i = 0; i < len; i++) {
		if (message[i] == '\n')
			k++;
	}

	if (XineramaIsActive(dpy)) {
		xsi = XineramaQueryScreens(dpy, &i);
		s_width = xsi[0].width;
		s_height = xsi[0].height;
	} else {
		s_width = DisplayWidth(dpy, screen);
		s_height = DisplayHeight(dpy, screen);
	}
	height = s_height/2 - (k*20)/2;

	/* Draw each line centered */
	line_start = 0;
	k = 0;
	total_height = height;
	for (i = 0; i <= len; i++) {
		if (i == len || message[i] == '\n') {
			/* Extract this line into buffer */
			line_len = i - line_start;
			if (line_len >= (int)sizeof(line_buf))
				line_len = sizeof(line_buf) - 1;

			for (j = 0; j < line_len; j++)
				line_buf[j] = message[line_start + j];
			line_buf[line_len] = '\0';

			/* Use larger font for first line (k==0) */
			XftFont *current_font = (k == 0) ? font_large : font;

			/* Calculate width of this line */
			XftTextExtentsUtf8(dpy, current_font, (FcChar8 *)line_buf, line_len, &extents);

			/* Center this line */
			width = (s_width - extents.width) / 2;

			/* Draw this line */
			XftDrawStringUtf8(draw, &color, current_font, width, total_height,
			                  (FcChar8 *)line_buf, line_len);

			total_height += current_font->ascent + current_font->descent;
			line_start = i + 1;
			k++;
		}
	}

	/* Draw password asterisks */
	if (passlen > 0) {
		char asterisks[256];
		int ast_len = passlen < 255 ? passlen : 255;
		for (i = 0; i < ast_len; i++)
			asterisks[i] = '*';
		asterisks[ast_len] = '\0';

		XftTextExtentsUtf8(dpy, font, (FcChar8 *)asterisks, ast_len, &extents);
		width = (s_width - extents.width) / 2;

		XftDrawStringUtf8(draw, &color, font, width, total_height + 10,
		                  (FcChar8 *)asterisks, ast_len);
	}

	XftColorFree(dpy, lock->visual, lock->colormap, &color);
	XftDrawDestroy(draw);
	XftFontClose(dpy, font);
	XftFontClose(dpy, font_large);

	/* xsi should not be NULL anyway if Xinerama is active, but to be safe */
	if (XineramaIsActive(dpy) && xsi != NULL)
			XFree(xsi);
}



static const char *
gethash(void)
{
	const char *hash;
	struct passwd *pw;

	/* Check if the current user has a password entry */
	errno = 0;
	if (!(pw = getpwuid(getuid()))) {
		if (errno)
			die("slock: getpwuid: %s\n", strerror(errno));
		else
			die("slock: cannot retrieve password entry\n");
	}
	hash = pw->pw_passwd;

#if HAVE_SHADOW_H
	if (!strcmp(hash, "x")) {
		struct spwd *sp;
		if (!(sp = getspnam(pw->pw_name)))
			die("slock: getspnam: cannot retrieve shadow entry. "
			    "Make sure to suid or sgid slock.\n");
		hash = sp->sp_pwdp;
	}
#else
	if (!strcmp(hash, "*")) {
#ifdef __OpenBSD__
		if (!(pw = getpwuid_shadow(getuid())))
			die("slock: getpwnam_shadow: cannot retrieve shadow entry. "
			    "Make sure to suid or sgid slock.\n");
		hash = pw->pw_passwd;
#else
		die("slock: getpwuid: cannot retrieve shadow entry. "
		    "Make sure to suid or sgid slock.\n");
#endif /* __OpenBSD__ */
	}
#endif /* HAVE_SHADOW_H */

	return hash;
}

static void
readpw(Display *dpy, struct xrandr *rr, struct lock **locks, int nscreens,
       const char *hash)
{
	XRRScreenChangeNotifyEvent *rre;
	char buf[32], passwd[256], *inputhash;
	int caps, num, screen, running, failure, oldc;
	unsigned int len, color, indicators;
	KeySym ksym;
	XEvent ev;

	len = 0;
	caps = 0;
	running = 1;
	failure = 0;
	oldc = INIT;

	if (!XkbGetIndicatorState(dpy, XkbUseCoreKbd, &indicators))
		caps = indicators & 1;

	while (running && !XNextEvent(dpy, &ev)) {
		if (ev.type == KeyPress) {
			explicit_bzero(&buf, sizeof(buf));
			num = XLookupString(&ev.xkey, buf, sizeof(buf), &ksym, 0);
			if (IsKeypadKey(ksym)) {
				if (ksym == XK_KP_Enter)
					ksym = XK_Return;
				else if (ksym >= XK_KP_0 && ksym <= XK_KP_9)
					ksym = (ksym - XK_KP_0) + XK_0;
			}
			if (IsFunctionKey(ksym) ||
			    IsKeypadKey(ksym) ||
			    IsMiscFunctionKey(ksym) ||
			    IsPFKey(ksym) ||
			    IsPrivateKeypadKey(ksym))
				continue;
			switch (ksym) {
			case XK_Return:
				passwd[len] = '\0';
				errno = 0;
				if (!(inputhash = crypt(passwd, hash)))
					fprintf(stderr, "slock: crypt: %s\n", strerror(errno));
				else
					running = !!strcmp(inputhash, hash);
				if (running) {
					XBell(dpy, 100);
					failure = 1;
				}
				explicit_bzero(&passwd, sizeof(passwd));
				len = 0;
				break;
			case XK_Escape:
				explicit_bzero(&passwd, sizeof(passwd));
				len = 0;
				break;
			case XK_BackSpace:
				if (len)
					passwd[--len] = '\0';
				break;
			case XK_Caps_Lock:
				caps = !caps;
				break;
			default:
				if (num && !iscntrl((int)buf[0]) &&
				    (len + num < sizeof(passwd))) {
					memcpy(passwd + len, buf, num);
					len += num;
				}
				break;
			}
			color = len ? (caps ? CAPS : INPUT) : (failure || failonclear ? FAILED : INIT);
			if (running && oldc != color) {
				for (screen = 0; screen < nscreens; screen++) {
					XSetWindowBackground(dpy,
					                     locks[screen]->win,
					                     locks[screen]->colors[color]);
					XClearWindow(dpy, locks[screen]->win);
				}
				oldc = color;
			}
			if (running) {
				for (screen = 0; screen < nscreens; screen++) {
					XClearWindow(dpy, locks[screen]->win);
					writemessage(dpy, locks[screen]->win, screen, locks[screen], len);
				}
			}
		} else if (rr->active && ev.type == rr->evbase + RRScreenChangeNotify) {
			rre = (XRRScreenChangeNotifyEvent*)&ev;
			for (screen = 0; screen < nscreens; screen++) {
				if (locks[screen]->win == rre->window) {
					if (rre->rotation == RR_Rotate_90 ||
					    rre->rotation == RR_Rotate_270)
						XResizeWindow(dpy, locks[screen]->win,
						              rre->height, rre->width);
					else
						XResizeWindow(dpy, locks[screen]->win,
						              rre->width, rre->height);
					XClearWindow(dpy, locks[screen]->win);
					break;
				}
			}
		} else {
			for (screen = 0; screen < nscreens; screen++)
				XRaiseWindow(dpy, locks[screen]->win);
		}
	}
}

static struct lock *
lockscreen(Display *dpy, struct xrandr *rr, int screen)
{
	char curs[] = {0, 0, 0, 0, 0, 0, 0, 0};
	int i, ptgrab, kbgrab;
	struct lock *lock;
	XColor color, dummy;
	XSetWindowAttributes wa;
	Cursor invisible;
	XVisualInfo vinfo;

	if (dpy == NULL || screen < 0 || !(lock = malloc(sizeof(struct lock))))
		return NULL;

	lock->screen = screen;
	lock->root = RootWindow(dpy, lock->screen);

	/* Try to get ARGB visual for transparency */
	if (XMatchVisualInfo(dpy, lock->screen, 32, TrueColor, &vinfo)) {
		lock->visual = vinfo.visual;
		lock->colormap = XCreateColormap(dpy, lock->root, lock->visual, AllocNone);
	} else {
		lock->visual = DefaultVisual(dpy, lock->screen);
		lock->colormap = DefaultColormap(dpy, lock->screen);
	}

	for (i = 0; i < NUMCOLS; i++) {
		if (vinfo.depth == 32) {
			/* Parse ARGB color from hex string */
			unsigned long argb;
			sscanf(colorname[i] + 1, "%lx", &argb);
			lock->colors[i] = argb;
		} else {
			XAllocNamedColor(dpy, lock->colormap,
			                 colorname[i], &color, &dummy);
			lock->colors[i] = color.pixel;
		}
	}

	/* init */
	wa.override_redirect = 1;
	wa.background_pixel = lock->colors[INIT];
	wa.border_pixel = 0;
	wa.colormap = lock->colormap;
	lock->win = XCreateWindow(dpy, lock->root, 0, 0,
	                          DisplayWidth(dpy, lock->screen),
	                          DisplayHeight(dpy, lock->screen),
	                          0, vinfo.depth,
	                          CopyFromParent,
	                          lock->visual,
	                          CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWColormap, &wa);
	lock->pmap = XCreateBitmapFromData(dpy, lock->win, curs, 8, 8);
	invisible = XCreatePixmapCursor(dpy, lock->pmap, lock->pmap,
	                                &color, &color, 0, 0);
	XDefineCursor(dpy, lock->win, invisible);

	/* Try to grab mouse pointer *and* keyboard for 600ms, else fail the lock */
	for (i = 0, ptgrab = kbgrab = -1; i < 6; i++) {
		if (ptgrab != GrabSuccess) {
			ptgrab = XGrabPointer(dpy, lock->root, False,
			                      ButtonPressMask | ButtonReleaseMask |
			                      PointerMotionMask, GrabModeAsync,
			                      GrabModeAsync, None, invisible, CurrentTime);
		}
		if (kbgrab != GrabSuccess) {
			kbgrab = XGrabKeyboard(dpy, lock->root, True,
			                       GrabModeAsync, GrabModeAsync, CurrentTime);
		}

		/* input is grabbed: we can lock the screen */
		if (ptgrab == GrabSuccess && kbgrab == GrabSuccess) {
			XMapRaised(dpy, lock->win);
			if (rr->active)
				XRRSelectInput(dpy, lock->win, RRScreenChangeNotifyMask);

			XSelectInput(dpy, lock->root, SubstructureNotifyMask);
			return lock;
		}

		/* retry on AlreadyGrabbed but fail on other errors */
		if ((ptgrab != AlreadyGrabbed && ptgrab != GrabSuccess) ||
		    (kbgrab != AlreadyGrabbed && kbgrab != GrabSuccess))
			break;

		usleep(100000);
	}

	/* we couldn't grab all input: fail out */
	if (ptgrab != GrabSuccess)
		fprintf(stderr, "slock: unable to grab mouse pointer for screen %d\n",
		        screen);
	if (kbgrab != GrabSuccess)
		fprintf(stderr, "slock: unable to grab keyboard for screen %d\n",
		        screen);
	return NULL;
}

static void
usage(void)
{
	die("usage: slock [-v] [-f] [-m message] [cmd [arg ...]]\n");
}

int
main(int argc, char **argv) {
	struct xrandr rr;
	struct lock **locks;
	struct passwd *pwd;
	struct group *grp;
	uid_t duid;
	gid_t dgid;
	const char *hash;
	Display *dpy;
	int i, s, nlocks, nscreens;
	int count_fonts;
	char **font_names;

	ARGBEGIN {
	case 'v':
		fprintf(stderr, "slock-"VERSION"\n");
		return 0;
	case 'm':
		message = EARGF(usage());
		break;
	case 'f':
		if (!(dpy = XOpenDisplay(NULL)))
			die("slock: cannot open display\n");
		font_names = XListFonts(dpy, "*", 10000 /* list 10000 fonts*/, &count_fonts);
		for (i=0; i<count_fonts; i++) {
			fprintf(stderr, "%s\n", *(font_names+i));
		}
		return 0;
	default:
		usage();
	} ARGEND

	/* validate drop-user and -group */
	errno = 0;
	if (!(pwd = getpwnam(user)))
		die("slock: getpwnam %s: %s\n", user,
		    errno ? strerror(errno) : "user entry not found");
	duid = pwd->pw_uid;
	errno = 0;
	if (!(grp = getgrnam(group)))
		die("slock: getgrnam %s: %s\n", group,
		    errno ? strerror(errno) : "group entry not found");
	dgid = grp->gr_gid;

#ifdef __linux__
	dontkillme();
#endif

	hash = gethash();
	errno = 0;
	if (!crypt("", hash))
		die("slock: crypt: %s\n", strerror(errno));

	if (!(dpy = XOpenDisplay(NULL)))
		die("slock: cannot open display\n");

	/* drop privileges */
	if (setgroups(0, NULL) < 0)
		die("slock: setgroups: %s\n", strerror(errno));
	if (setgid(dgid) < 0)
		die("slock: setgid: %s\n", strerror(errno));
	if (setuid(duid) < 0)
		die("slock: setuid: %s\n", strerror(errno));

	/* check for Xrandr support */
	rr.active = XRRQueryExtension(dpy, &rr.evbase, &rr.errbase);

	/* get number of screens in display "dpy" and blank them */
	nscreens = ScreenCount(dpy);
	if (!(locks = calloc(nscreens, sizeof(struct lock *))))
		die("slock: out of memory\n");
	for (nlocks = 0, s = 0; s < nscreens; s++) {
		if ((locks[s] = lockscreen(dpy, &rr, s)) != NULL) {
			writemessage(dpy, locks[s]->win, s, locks[s], 0);
			nlocks++;
		} else {
			break;
		}
	}
	XSync(dpy, 0);

	/* did we manage to lock everything? */
	if (nlocks != nscreens)
		return 1;

	/* run post-lock command */
	if (argc > 0) {
		switch (fork()) {
		case -1:
			die("slock: fork failed: %s\n", strerror(errno));
		case 0:
			if (close(ConnectionNumber(dpy)) < 0)
				die("slock: close: %s\n", strerror(errno));
			execvp(argv[0], argv);
			fprintf(stderr, "slock: execvp %s: %s\n", argv[0], strerror(errno));
			_exit(1);
		}
	}

	/* everything is now blank. Wait for the correct password */
	readpw(dpy, &rr, locks, nscreens, hash);

	return 0;
}
