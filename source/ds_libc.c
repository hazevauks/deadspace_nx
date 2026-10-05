/* ds_libc.c -- the few symbols libDeadSpace.so imports that the runtime's
 * shims do not have (tools/gen_imports.py binds them by their b_ names).
 * The library was built with an early NDK (Android 2.x): bionic's old
 * __atomic_* helpers, and symbols newer libraries carry themselves. MIT.
 */
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "bionic.h"
#include "dcr_path.h"
#include "util.h"

/* bionic's <sys/atomics.h>: cmpxchg answers 0 when it swapped; the others
 * answer the value before. */
int b___atomic_cmpxchg(int old, int new_value, volatile int *ptr) {
  return !__sync_bool_compare_and_swap(ptr, old, new_value);
}
int b___atomic_swap(int new_value, volatile int *ptr) {
  int old;
  do
    old = *ptr;
  while (!__sync_bool_compare_and_swap(ptr, old, new_value));
  return old;
}
int b___atomic_inc(volatile int *ptr) { return __sync_fetch_and_add(ptr, 1); }
int b___atomic_dec(volatile int *ptr) { return __sync_fetch_and_sub(ptr, 1); }

int b___isinf(double d) { return isinf(d) ? (d < 0 ? -1 : 1) : 0; }

/* Every stream is byte-oriented. */
int b_fwide(void *fp, int mode) {
  (void)fp, (void)mode;
  return -1;
}

/* No program break here: -1 (ENOMEM) sends an allocator on to mmap. */
void *b_sbrk(intptr_t increment) {
  static int said;
  if (!said++)
    debugPrintf("[libc] sbrk(%ld) refused (there is no program break)\n", (long)increment);
  return (void *)-1;
}

/* A thread's stack is always the runtime's own: the size is kept, the
 * caller's memory is not used. */
int b_pthread_attr_setstack(b_pthread_attr_t *a, void *base, size_t size) {
  debugPrintf("[libc] pthread_attr_setstack(%p, %u): the size only\n", base, (unsigned)size);
  a->stack_size = size;
  return 0;
}
int b_pthread_mutexattr_setpshared(b_pthread_mutexattr_t *a, int shared) {
  (void)a, (void)shared;
  return 0;
}

char *b_tzname[2] = {"UTC", "UTC"};
void *b___dso_handle = &b___dso_handle;

/* androidGetTmpRoot() and androidGetExternalRoot(): C++ functions the
 * library imports and no Android library exports (Android's linker bound
 * functions lazily, so the phone never noticed). In case a path of the
 * engine does call them: the app's cache and external folders. */
const char *b__Z17androidGetTmpRootv(void) {
  debugPrintf("[libc] androidGetTmpRoot()\n");
  return DCR_ANDROID_CACHE;
}
const char *b__Z22androidGetExternalRootv(void) {
  debugPrintf("[libc] androidGetExternalRoot()\n");
  return DCR_ANDROID_EXT_FILES;
}
