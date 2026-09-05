/* Replays wm2's nextEvent() top-of-loop EXACTLY:
      195:  if (QLength(dpy) > 0) { XNextEvent(...); return; }
      201:  XFlush(dpy);
      205:  poll(fd, ..., -1);
   and asks whether an event can end up in Xlib's queue with the socket
   drained -- i.e. poll() blocking on an event it already holds. */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>

static int fd_readable(Display *d) {
    struct pollfd p = { ConnectionNumber(d), POLLIN, 0 };
    return poll(&p, 1, 0) > 0 && (p.revents & POLLIN);
}

int main(void) {
    Display *A = XOpenDisplay(NULL);
    Display *B = XOpenDisplay(NULL);
    if (!A || !B) return 2;

    XSelectInput(A, DefaultRootWindow(A), PropertyChangeMask);
    XSync(A, False);

    Atom a = XInternAtom(B, "_PROBE2", False);
    XChangeProperty(B, DefaultRootWindow(B), a, XA_STRING, 8,
                    PropModeReplace, (unsigned char *)"y", 1);
    XFlush(B);
    usleep(200000);

    printf("wm2 line 195  -> QLength=%d  (0 means the branch is NOT taken)\n", QLength(A));
    printf("              -> fd readable=%d\n", fd_readable(A));

    XFlush(A);                                  /* wm2 line 201 */

    int q = QLength(A), rd = fd_readable(A);
    printf("wm2 line 201  -> XFlush() called\n");
    printf("wm2 line 205  -> QLength=%d, fd readable=%d\n", q, rd);

    if (q > 0 && !rd) {
        printf("\n*** WEDGE CONFIRMED ***\n");
        printf("XFlush() moved the event into Xlib's queue and drained the socket.\n");
        printf("wm2 does not re-test QLength after line 201, so poll() at line 205\n");
        printf("blocks indefinitely on an event the process is already holding.\n");
        return 1;
    }
    printf("\nno wedge in this interleaving (q=%d rd=%d)\n", q, rd);
    return 0;
}
