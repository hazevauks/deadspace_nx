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
 *    and the models ask for mipmaps on every texture
 *    (GL_LINEAR_MIPMAP_LINEAR). To the GL that is an incomplete texture.
 *    The one that shows is the hair of the unhelmeted head (the
 *    hallucinations of the second chapter): 512 x 512 RGBA of fine strands,
 *    alpha-tested, on a head a hundred pixels wide. Sampled from level 0
 *    alone it is static.
 *    So such a texture gets the mipmaps its filter asks for: the GL makes
 *    them when level 0 is given under a mipmap filter, or when the filter
 *    becomes one later, and again when part of level 0 changes. A
 *    compressed texture of one level, which the GL cannot make mipmaps for,
 *    is told that level 0 is all there is (GL_TEXTURE_MAX_LEVEL). MIT.
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
#define GL_TEXTURE_BINDING_2D 0x8069
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_TEXTURE_MAX_LEVEL 0x813D /* GL_APPLE_texture_max_level, which Mesa lists */
#define ALL_LEVELS 1000             /* its default */

static void (*r_tex_image)(unsigned, int, int, int, int, int, unsigned, unsigned, const void *);
static void (*r_tex_sub_image)(unsigned, int, int, int, int, int, unsigned, unsigned, const void *);
static void (*r_compressed_tex_image)(unsigned, int, unsigned, int, int, int, int, const void *);
static void (*r_tex_parameteri)(unsigned, unsigned, int);
static void (*r_tex_parameterx)(unsigned, unsigned, int32_t);
static void (*r_tex_parameterf)(unsigned, unsigned, float);
static void (*r_delete_textures)(int, const unsigned *);
static void (*r_get_integerv)(unsigned, int *);
static void (*r_get_tex_parameteriv)(unsigned, unsigned, int *);
static void (*r_generate_mipmap)(unsigned);

/* What is known of a texture, by its name: Mesa's names are small numbers. */
enum { T_UNKNOWN, T_ONE_LEVEL, T_MADE }; /* level 0 alone; its mipmaps made here */
#define MAX_NAMES 16384
static uint8_t g_tex[MAX_NAMES];

static int is_mipmap_filter(int filter) {
  return filter >= GL_NEAREST_MIPMAP_NEAREST && filter <= GL_LINEAR_MIPMAP_LINEAR;
}

/* The functions the engine may not have asked for by the time they are
 * needed: a lookup comes through port_gl_wrap, which fills the pointer. */
static int ready(void) {
  static int state;
  if (!state) {
    dcr_gl_lookup("glTexParameteri");
    dcr_gl_lookup("glGetIntegerv");
    dcr_gl_lookup("glGetTexParameteriv");
    dcr_gl_lookup("glGenerateMipmapOES");
    state = r_tex_parameteri && r_get_integerv && r_get_tex_parameteriv && r_generate_mipmap ? 1 : -1;
    debugPrintf("[gl] textures of one level under a mipmap filter: %s\n",
                state > 0 ? "their mipmaps are made here" : "left as they are (a GL function is missing)");
  }
  return state > 0;
}

static unsigned bound(void) {
  int name = 0;
  r_get_integerv(GL_TEXTURE_BINDING_2D, &name);
  return (unsigned)name;
}

static void make_mipmaps(unsigned name, int width, int height) {
  static int said;
  r_tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, ALL_LEVELS);
  r_generate_mipmap(GL_TEXTURE_2D);
  if (g_tex[name] != T_MADE && said < 160) {
    said++;
    if (width)
      debugPrintf("[gl] texture %u (%dx%d): one level under a mipmap filter, mipmaps made\n", name, width, height);
    else
      debugPrintf("[gl] texture %u: a mipmap filter on its one level, mipmaps made\n", name);
  }
  g_tex[name] = T_MADE;
  ds_trace_texture(name, -1, 0, 0, 0, 0);
}

static void w_tex_image(unsigned target, int level, int internal, int width, int height, int border,
                        unsigned format, unsigned type, const void *pixels) {
  r_tex_image(target, level, internal, width, height, border, format, type, pixels);
  if (target != GL_TEXTURE_2D || !ready())
    return;
  const unsigned name = bound();
  ds_trace_texture(name, level, width, height, (unsigned)internal, pixels ? 0 : 2);
  if (!name || name >= MAX_NAMES)
    return;
  if (level) { /* the engine has its own levels for this one */
    g_tex[name] = T_UNKNOWN;
    return;
  }
  int filter = 0;
  r_get_tex_parameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &filter);
  g_tex[name] = T_ONE_LEVEL;
  if (pixels && is_mipmap_filter(filter))
    make_mipmaps(name, width, height);
}

static void w_tex_sub_image(unsigned target, int level, int x, int y, int width, int height, unsigned format,
                            unsigned type, const void *pixels) {
  r_tex_sub_image(target, level, x, y, width, height, format, type, pixels);
  if (target != GL_TEXTURE_2D || level || !ready())
    return;
  const unsigned name = bound();
  if (!name || name >= MAX_NAMES || g_tex[name] == T_UNKNOWN)
    return;
  int filter = 0;
  r_get_tex_parameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &filter);
  if (is_mipmap_filter(filter)) /* made before, or level 0 was given empty */
    make_mipmaps(name, 0, 0);
}

/* The GL cannot make a compressed texture's mipmaps: with level 0 alone it
 * is sampled from level 0. */
static void w_compressed_tex_image(unsigned target, int level, unsigned internal, int width, int height,
                                   int border, int size, const void *data) {
  r_compressed_tex_image(target, level, internal, width, height, border, size, data);
  if (target != GL_TEXTURE_2D || !ready())
    return;
  const unsigned name = bound();
  ds_trace_texture(name, level, width, height, internal, 1);
  if (level > 1)
    return;
  if (name && name < MAX_NAMES)
    g_tex[name] = T_UNKNOWN;
  r_tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, level ? ALL_LEVELS : 0);
}

/* A mipmap filter set on a texture that was given one level under another. */
static void filter_set(unsigned target, unsigned pname, int value) {
  if (target != GL_TEXTURE_2D || pname != GL_TEXTURE_MIN_FILTER || !is_mipmap_filter(value) || !ready())
    return;
  const unsigned name = bound();
  if (name && name < MAX_NAMES && g_tex[name] == T_ONE_LEVEL)
    make_mipmaps(name, 0, 0);
}

static void w_tex_parameteri(unsigned target, unsigned pname, int value) {
  r_tex_parameteri(target, pname, value);
  filter_set(target, pname, value);
}

static void w_tex_parameterx(unsigned target, unsigned pname, int32_t value) {
  r_tex_parameterx(target, pname, value);
  filter_set(target, pname, value); /* an enum is passed as it is */
}

static void w_tex_parameterf(unsigned target, unsigned pname, float value) {
  r_tex_parameterf(target, pname, value);
  filter_set(target, pname, (int)value);
}

static void w_delete_textures(int n, const unsigned *names) {
  for (int i = 0; names && i < n; i++)
    if (names[i] < MAX_NAMES)
      g_tex[names[i]] = T_UNKNOWN;
  r_delete_textures(n, names);
}

/* The runtime's callback, for every GL function looked up (the engine's
 * imports, the overlay's, ready()'s): those with a wrapper go through it,
 * the others are only taken note of. */
uintptr_t port_gl_wrap(const char *name, uintptr_t real) {
  static const struct {
    const char *name;
    void *slot, *wrapper;
  } k_wraps[] = {
      {"glTexImage2D", &r_tex_image, w_tex_image},
      {"glTexSubImage2D", &r_tex_sub_image, w_tex_sub_image},
      {"glCompressedTexImage2D", &r_compressed_tex_image, w_compressed_tex_image},
      {"glTexParameteri", &r_tex_parameteri, w_tex_parameteri},
      {"glTexParameterx", &r_tex_parameterx, w_tex_parameterx},
      {"glTexParameterf", &r_tex_parameterf, w_tex_parameterf},
      {"glDeleteTextures", &r_delete_textures, w_delete_textures},
      {"glGetIntegerv", &r_get_integerv, NULL},
      {"glGetTexParameteriv", &r_get_tex_parameteriv, NULL},
      {"glGenerateMipmapOES", &r_generate_mipmap, NULL},
  };
  if (!real)
    return 0;
  const uintptr_t traced = ds_trace_wrap(name, real); /* the draws: ds_trace.c */
  if (traced)
    return traced;
  for (unsigned i = 0; i < sizeof k_wraps / sizeof k_wraps[0]; i++)
    if (!strcmp(name, k_wraps[i].name)) {
      memcpy(k_wraps[i].slot, &real, sizeof real);
      return (uintptr_t)k_wraps[i].wrapper;
    }
  return 0;
}
