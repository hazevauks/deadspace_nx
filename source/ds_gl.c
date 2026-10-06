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
 *    window is RGBA 8888, and everything blended into it leaves its own
 *    alpha behind. The game's hallucinations draw a screen of static with a
 *    blend that depends on destination alpha: on a phone an even tint, here
 *    static wherever something translucent had been drawn (the unhelmeted
 *    head's hair, in the second chapter).
 *    So the window is made to behave as the phone's: its alpha is set to 1
 *    at the start of every frame and never written after (glColorMask's
 *    alpha is held off while the window is the target; a framebuffer object
 *    keeps what the engine asked for). MIT.
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
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_SCISSOR_TEST 0x0C11
#define GL_ALPHA_BITS 0x0D55
#define GL_FRAMEBUFFER 0x8D40

static void (*r_color_mask)(uint8_t, uint8_t, uint8_t, uint8_t);
static void (*r_clear_color)(float, float, float, float);
static void (*r_clear_colorx)(int32_t, int32_t, int32_t, int32_t);
static void (*r_bind_framebuffer)(unsigned, unsigned);

/* What the engine asked for last, and where it draws. */
static uint8_t g_mask[4] = {1, 1, 1, 1};
static float g_clear[4];
static unsigned g_fbo;
static int g_opaque = -1; /* 1: the window has alpha to keep at 1; -1: not looked at yet */

static void apply_mask(void) {
  r_color_mask(g_mask[0], g_mask[1], g_mask[2], g_opaque > 0 && !g_fbo ? 0 : g_mask[3]);
}

static void w_color_mask(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  g_mask[0] = r, g_mask[1] = g, g_mask[2] = b, g_mask[3] = a;
  apply_mask();
}

static void w_clear_color(float r, float g, float b, float a) {
  g_clear[0] = r, g_clear[1] = g, g_clear[2] = b, g_clear[3] = a;
  r_clear_color(r, g, b, a);
}

static void w_clear_colorx(int32_t r, int32_t g, int32_t b, int32_t a) {
  g_clear[0] = (float)r / 65536.0f, g_clear[1] = (float)g / 65536.0f;
  g_clear[2] = (float)b / 65536.0f, g_clear[3] = (float)a / 65536.0f;
  r_clear_colorx(r, g, b, a);
}

static void w_bind_framebuffer(unsigned target, unsigned fbo) {
  r_bind_framebuffer(target, fbo);
  if (target == GL_FRAMEBUFFER && r_color_mask) {
    g_fbo = fbo;
    apply_mask();
  }
}

/* The runtime's callback: the engine's imports of these go through the
 * wrappers above. */
uintptr_t port_gl_wrap(const char *name, uintptr_t real) {
  static const struct {
    const char *name;
    void *slot, *wrapper;
  } k_wraps[] = {
      {"glColorMask", &r_color_mask, w_color_mask},
      {"glClearColor", &r_clear_color, w_clear_color},
      {"glClearColorx", &r_clear_colorx, w_clear_colorx},
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

/* Before each of the engine's frames, with its context current: the new
 * back buffer's alpha set to 1, and nothing else touched (the engine keeps
 * its own copy of the GL state: the clear colour and the scissor test are
 * put back as it left them). */
void ds_gl_frame_begin(void) {
  static void (*clear)(unsigned), (*enable)(unsigned), (*disable)(unsigned);
  static uint8_t (*is_enabled)(unsigned);
  if (g_opaque < 0) {
    void (*get_integerv)(unsigned, int *) = (void (*)(unsigned, int *))dcr_gl_lookup("glGetIntegerv");
    clear = (void (*)(unsigned))dcr_gl_lookup("glClear");
    enable = (void (*)(unsigned))dcr_gl_lookup("glEnable");
    disable = (void (*)(unsigned))dcr_gl_lookup("glDisable");
    is_enabled = (uint8_t (*)(unsigned))dcr_gl_lookup("glIsEnabled");
    int bits = 0;
    if (get_integerv)
      get_integerv(GL_ALPHA_BITS, &bits);
    g_opaque = bits > 0 && r_color_mask && r_clear_color && clear && enable && disable && is_enabled;
    debugPrintf("[gl] the window has %d alpha bits%s\n", bits,
                g_opaque ? ": kept at 1, as a phone's screen has none" : "");
    if (g_opaque)
      apply_mask();
  }
  if (g_opaque <= 0 || g_fbo)
    return;
  const int scissor = is_enabled(GL_SCISSOR_TEST);
  if (scissor)
    disable(GL_SCISSOR_TEST);
  r_color_mask(0, 0, 0, 1);
  r_clear_color(0, 0, 0, 1);
  clear(GL_COLOR_BUFFER_BIT);
  r_clear_color(g_clear[0], g_clear[1], g_clear[2], g_clear[3]);
  apply_mask();
  if (scissor)
    enable(GL_SCISSOR_TEST);
}
