/* Same stimulus, both loop shapes. OLD = wm2 today. NEW = the proposed fix.
   poll() timeout is 1500ms instead of -1 so "blocks forever" is observable
   rather than fatal. */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>

static Display *A, *B;
static Atom prop;

static void arm(const char *tag) {          /* make one PropertyNotify pending */
    XChangeProperty(B, DefaultRootWindow(B), prop, XA_STRING, 8,
                    PropModeReplace, (unsigned char *)tag, 1);
    XFlush(B);
    usleep(200000);
}

static int poll_x(int ms) {
    struct pollfd p = { ConnectionNumber(A), POLLIN, 0 };
    return poll(&p, 1, ms);
}

int main(void) {
    A = XOpenDisplay(NULL); B = XOpenDisplay(NULL);
    if (!A || !B) return 2;
    XSelectInput(A, DefaultRootWindow(A), PropertyChangeMask);
    XSync(A, False);
    prop = XInternAtom(B, "_PROBE3", False);

    /* ---- OLD shape: wm2 Events.cpp:195-205 ---- */
    arm("a");
    printf("OLD (wm2 today):\n");
    if (QLength(A) > 0) { printf("  line195 took branch -> event delivered\n"); }
    else {
        XFlush(A);                                  /* line 201 */
        printf("  line195 saw QLength=0, fell through to XFlush\n");
        int r = poll_x(1500);                       /* line 205 */
        printf("  line205 poll() -> %s (QLength is now %d)\n",
               r == 0 ? "TIMED OUT: would block forever with timeout=-1"
                      : "woke", QLength(A));
    }

    /* drain so the two shapes see the same clean starting state */
    while (XPending(A)) { XEvent e; XNextEvent(A, &e); }

    /* ---- NEW shape: the proposed fix ---- */
    arm("b");
    printf("\nNEW (proposed fix):\n");
    int pending = XPending(A);                      /* replaces line 195+201 */
    if (pending > 0) {
        XEvent e; XNextEvent(A, &e);
        printf("  XPending()=%d -> event delivered immediately (type=%d)\n",
               pending, e.type);
        printf("  poll() never reached; no wedge possible\n");
        return 0;
    }
    printf("  XPending()=0 -> would poll (correct: nothing is pending)\n");
    return 0;
}
