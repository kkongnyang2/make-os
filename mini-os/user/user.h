// user/user.h — minimal user‑space interface for the mini‑xv6 kernel
//
// Keep this file lean: only the system‑call stubs (and a couple of
// libc‑style helpers) that our early test programs need.  You can add
// more prototypes as new functionality is enabled in the kernel.
//
// NOTE: the prototypes must match the wrappers that are generated in
//       user/usys.S.  If you add or reorder entries here, keep the two
//       files in sync.

#ifndef _USER_H_
#define _USER_H_

#include "kernel/types.h"   // uint, int, etc.

//───────────────────────────────────────────────────────────────────────
//  system‑call stubs (user‑space wrappers)
//───────────────────────────────────────────────────────────────────────
//  The kernel side numbers and trampoline live in kernel/syscall.c and
//  user/usys.S.  We expose only the subset we need for now.
//-----------------------------------------------------------------------

int write(int fd, const void *buf, int n);        // fd = 1 ⇒ stdout(UART)
void exit(int status) __attribute__((noreturn));  // terminate process

// (Optional) stubs you might enable soon — uncomment when ready
// int  fork(void);
// int  exec(const char *path, char *const argv[]);
// int  wait(int *status);
// int  read(int fd, void *buf, int n);
// int  open(const char *path, int oflags);
// int  close(int fd);

//───────────────────────────────────────────────────────────────────────
//  tiny libc helpers (in user/string.c or kernel/string.c, dual‑use)
//───────────────────────────────────────────────────────────────────────

void *memset(void *dst, int c, uint n);
int   strlen(const char *s);

#endif // _USER_H_
