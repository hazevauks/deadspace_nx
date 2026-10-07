/* ds_gl.c -- where Mesa on the Switch is not the GLES 1 of the game's phones.
 *
 * 1. OES_matrix_palette. libDeadSpace.so imports the whole GLES 1 API, GPU
 *    skinning included, which Mesa does not implement and does not list
 *    among its extensions. An engine that honours the extension string never
 *    calls these; each says so once if it does, instead of the fault an
 *    unresolved import would be.
 *
 * 2. The window's alpha. The game's Java asks for an RGB 565 screen
 *    (AndroidView's ConfigChooser: 5, 6, 5, 0): a framebuffer without alpha,
 *    whose "destination alpha" reads 1 whatever was drawn. The Switch's
 *    window is RGBA 8888 (the one format Mesa's EGL offers here), and
 *    everything blended into it leaves its own alpha behind. The game's
 *    hallucinations draw a screen of static with a blend that depends on
 *    destination alpha: on a phone an even tint, here static wherever
 *    something translucent had been drawn (the unhelmeted head's hair, in
 *    the second chapter).
 *    Destination alpha is seen through the blend factors alone, so those are
 *    what change: while the window is the target, GL_DST_ALPHA is 1 and
 *    GL_ONE_MINUS_DST_ALPHA is 0, which is what Mesa itself does for a
 *    surface without alpha. A framebuffer object keeps what the engine
 *    asked for. Nothing is done per frame, and no clear becomes a masked
 *    one, as holding glColorMask's alpha off would have it. MIT.
 */
#include <stdint.h>
#include <string.h>

#include "ds.h"
#include "gl_layer.h"
#include "imports.h"
#include "util.h"

#define MISSING(name)                                                          \
  static void name(void) {                                                     \
    static int said;                                                           \
    if (!said++)                                                               \
      debugPrintf("[gl] " #name " called: OES_matrix_palette is not there\n"); \
  }
MISSING(glCurrentPaletteMatrixOES)
MISSING(glLoadPaletteFromModelViewMatrixOES)
MISSING(glMatrixIndexPointerOES)
MISSING(glWeightPointerOES)

const DynLibFunction port_imports[] = {
    {"glCurrentPaletteMatrixOES", (uintptr_t)glCurrentPaletteMatrixOES},
    {"glLoadPaletteFromModelViewMatrixOES", (uintptr_t)glLoadPaletteFromModelViewMatrixOES},
    {"glMatrixIndexPointerOES", (uintptr_t)glMatrixIndexPointerOES},
    {"glWeightPointerOES", (uintptr_t)glWeightPointerOES},
};
const int port_imports_count = sizeof port_imports / sizeof port_imports[0];

/* ------------------------------------------------------- the window's alpha */
#define GL_ZERO 0
#define GL_ONE 1
#define GL_DST_ALPHA 0x0304
#define GL_ONE_MINUS_DST_ALPHA 0x0305
#define GL_SRC_ALPHA_SATURATE 0x0308
#define GL_FRAMEBUFFER 0x8D40

static void (*r_blend_func)(unsigned, unsigned);
static void (*r_blend_func_separate)(unsigned, unsigned, unsigned, unsigned);
static void (*r_bind_framebuffer)(unsigned, unsigned);

/* What the engine asked for last (source and destination: colour, then
 * alpha), and where it draws. */
static unsigned g_blend[4] = {GL_ONE, GL_ZERO, GL_ONE, GL_ZERO};
static int g_separate;
static unsigned g_fbo;
static int g_overlay; /* the port's own drawing: passed through, not kept */

/* A factor as a framebuffer without alpha has it. */
static unsigned opaque(unsigned factor) {
  return factor == GL_DST_ALPHA                                                  ? GL_ONE
         : factor == GL_ONE_MINUS_DST_ALPHA || factor == GL_SRC_ALPHA_SATURATE ? GL_ZERO
                                                                                 : factor;
}

static void apply_blend(void) {
  unsigned f[4];
  int changed = 0;
  for (int i = 0; i < 4; i++) {
    f[i] = g_fbo ? g_blend[i] : opaque(g_blend[i]);
    changed |= f[i] != g_blend[i];
  }
  static int said;
  if (changed && !said++)
    debugPrintf("[gl] a blend by the window's alpha (0x%x, 0x%x): taken as 1, as a phone's screen has none\n",
                g_blend[0], g_blend[1]);
  if (g_separate && r_blend_func_separate)
    r_blend_func_separate(f[0], f[1], f[2], f[3]);
  else if (r_blend_func)
    r_blend_func(f[0], f[1]);
}

static void w_blend_func(unsigned src, unsigned dst) {
  if (g_overlay) {
    r_blend_func(src, dst);
    return;
  }
  g_blend[0] = g_blend[2] = src, g_blend[1] = g_blend[3] = dst;
  g_separate = 0;
  apply_blend();
}

static void w_blend_func_separate(unsigned src, unsigned dst, unsigned src_alpha, unsigned dst_alpha) {
  if (g_overlay) {
    r_blend_func_separate(src, dst, src_alpha, dst_alpha);
    return;
  }
  g_blend[0] = src, g_blend[1] = dst, g_blend[2] = src_alpha, g_blend[3] = dst_alpha;
  g_separate = 1;
  apply_blend();
}

static void w_bind_framebuffer(unsigned target, unsigned fbo) {
  r_bind_framebuffer(target, fbo);
  if (target == GL_FRAMEBUFFER && !fbo != !g_fbo) { /* to or from the window */
    g_fbo = fbo;
    apply_blend();
  }
}

/* The runtime's callback: every lookup of these goes through the wrappers
 * above, the engine's imports and the overlay's own (ds_gl_overlay). */
uintptr_t port_gl_wrap(const char *name, uintptr_t real) {
  static const struct {
    const char *name;
    void *slot, *wrapper;
  } k_wraps[] = {
      {"glBlendFunc", &r_blend_func, w_blend_func},
      {"glBlendFuncSeparateOES", &r_blend_func_separate, w_blend_func_separate},
      {"glBindFramebufferOES", &r_bind_framebuffer, w_bind_framebuffer},
  };
  if (!real)
    return 0;
  for (unsigned i = 0; i < sizeof k_wraps / sizeof k_wraps[0]; i++)
    if (!strcmp(name, k_wraps[i].name)) {
      memcpy(k_wraps[i].slot, &real, sizeof real);
      return (uintptr_t)k_wraps[i].wrapper;
    }
  return 0;
}

/* Around what the port draws over the frame (ds_cursor.c): the overlay sets
 * its own blend and puts the engine's back as it read it from the GL. */
void ds_gl_overlay(int on) { g_overlay = on; }
