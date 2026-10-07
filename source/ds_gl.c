/* ds_gl.c -- where Mesa on the Switch is not the GLES 1 of the game's phones.
 *
 * 1. OES_matrix_palette. libDeadSpace.so imports the whole GLES 1 API, GPU
 *    skinning included, which Mesa does not implement and does not list
 *    among its extensions. An engine that honours the extension string never
 *    calls these; each says so once if it does, instead of the fault an
 *    unresolved import would be.
 *
 * 2. Textures of a single level. The engine keeps a mipmap chain for some
 *    compressed formats only; every other image (RGBA 8888 among them) is
 *    uploaded as level 0 alone, with whatever filter its model asks for,
 *    and the models ask for mipmaps on every texture. To the GL that is an
 *    incomplete texture, which by the book is not applied at all. Mesa goes
 *    by the book; the game's phones, evidently, sampled the level there
 *    is. So the
 *    hair of the unhelmeted head (the hallucinations of the second chapter:
 *    512 x 512 RGBA, alpha-tested) was drawn without its texture: every
 *    card of it whole, in the grey its lighting gives it.
 *    Here a texture is told how many levels it has: GL_TEXTURE_MAX_LEVEL is
 *    0 when level 0 is uploaded, and unbounded again when level 1 follows.
 *    MIT.
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

/* ------------------------------------------- textures of a single level */
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_MAX_LEVEL 0x813D /* GL_APPLE_texture_max_level, which Mesa lists */
#define ALL_LEVELS 1000             /* its default */

static void (*r_tex_image)(unsigned, int, int, int, int, int, unsigned, unsigned, const void *);
static void (*r_compressed_tex_image)(unsigned, int, unsigned, int, int, int, int, const void *);
static void (*r_tex_parameteri)(unsigned, unsigned, int);

/* After level 0 or 1 of the bound texture was given. */
static void levels(unsigned target, int level) {
  static int said;
  if (target != GL_TEXTURE_2D || level > 1)
    return;
  if (!r_tex_parameteri) /* not looked up yet: the lookup comes through port_gl_wrap */
    dcr_gl_lookup("glTexParameteri");
  if (!r_tex_parameteri)
    return;
  if (!said++)
    debugPrintf("[gl] textures are sampled from the levels they were given (GL_TEXTURE_MAX_LEVEL)\n");
  r_tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, level ? ALL_LEVELS : 0);
}

static void w_tex_image(unsigned target, int level, int internal, int width, int height, int border,
                        unsigned format, unsigned type, const void *pixels) {
  r_tex_image(target, level, internal, width, height, border, format, type, pixels);
  levels(target, level);
}

static void w_compressed_tex_image(unsigned target, int level, unsigned internal, int width, int height,
                                   int border, int size, const void *data) {
  r_compressed_tex_image(target, level, internal, width, height, border, size, data);
  levels(target, level);
}

/* The runtime's callback, for every GL function looked up (the engine's
 * imports, the overlay's): the two uploads go through the wrappers above;
 * glTexParameteri is only taken note of. */
uintptr_t port_gl_wrap(const char *name, uintptr_t real) {
  static const struct {
    const char *name;
    void *slot, *wrapper;
  } k_wraps[] = {
      {"glTexImage2D", &r_tex_image, w_tex_image},
      {"glCompressedTexImage2D", &r_compressed_tex_image, w_compressed_tex_image},
      {"glTexParameteri", &r_tex_parameteri, NULL},
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
