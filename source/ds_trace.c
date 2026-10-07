/* ds_trace.c -- a frame's report, for a picture that is wrong.
 *
 * With the right stick held down, a click of the left one saves the next
 * frame as capture-NNN.bmp in the game's folder and lists that frame's
 * draws in debug.log: for each, the texture of either unit (its size,
 * format, levels and filters), the tests, the blend, and where the thing is
 * from the camera. The two together say which draw is the wrong one, and
 * in what state the GL was for it. MIT.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ds.h"
#include "gl_layer.h"
#include "util.h"

#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_ACTIVE_TEXTURE 0x84E0
#define GL_TEXTURE_BINDING_2D 0x8069
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_ENV 0x2300
#define GL_TEXTURE_ENV_MODE 0x2200
#define GL_TEXTURE_MATRIX 0x0BA8
#define GL_MODELVIEW_MATRIX 0x0BA6
#define GL_CURRENT_COLOR 0x0B00
#define GL_ALPHA_TEST 0x0BC0
#define GL_ALPHA_TEST_FUNC 0x0BC1
#define GL_ALPHA_TEST_REF 0x0BC2
#define GL_BLEND 0x0BE2
#define GL_BLEND_DST 0x0BE0
#define GL_BLEND_SRC 0x0BE1
#define GL_DEPTH_TEST 0x0B71
#define GL_DEPTH_WRITEMASK 0x0B72
#define GL_DEPTH_FUNC 0x0B74
#define GL_LIGHTING 0x0B50
#define GL_CULL_FACE 0x0B44
#define GL_FOG 0x0B60
#define GL_STENCIL_TEST 0x0B90
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_COLOR_ARRAY 0x8076
#define GL_FRAMEBUFFER_BINDING 0x8CA6

#define MAX_NAMES 4096
#define MAX_DRAWS 2000

/* What each texture was given, by its name. */
static struct {
  uint16_t width, height, format;
  uint8_t top;   /* the highest level given */
  uint8_t flags; /* 1 compressed, 2 level 0 given without pixels, 4 mipmaps made by the GL */
} g_tex[MAX_NAMES];

static void (*r_draw_elements)(unsigned, int, unsigned, const void *);
static void (*r_draw_arrays)(unsigned, int, int);
static void (*gl_get_integerv)(unsigned, int *);
static void (*gl_get_floatv)(unsigned, float *);
static uint8_t (*gl_is_enabled)(unsigned);
static void (*gl_active_texture)(unsigned);
static void (*gl_get_tex_parameteriv)(unsigned, unsigned, int *);
static void (*gl_get_tex_enviv)(unsigned, unsigned, int *);

static int g_state; /* 2: asked for; 1: the frame being drawn is the one reported */
static int g_draws;

void ds_trace_texture(unsigned name, int level, int width, int height, unsigned format, int flags) {
  if (!name || name >= MAX_NAMES)
    return;
  if (level == 0) {
    g_tex[name].width = (uint16_t)width;
    g_tex[name].height = (uint16_t)height;
    g_tex[name].format = (uint16_t)format;
    g_tex[name].top = 0;
    g_tex[name].flags = (uint8_t)flags;
  } else if (level > 0) {
    if (level > g_tex[name].top)
      g_tex[name].top = (uint8_t)level;
  } else { /* its mipmaps were made */
    g_tex[name].flags |= 4;
  }
}

static void unit(int index, char *out, size_t cap) {
  int name = 0, min = 0, mag = 0, env = 0;
  float m[16] = {0};
  gl_active_texture(GL_TEXTURE0 + (unsigned)index);
  if (!gl_is_enabled(GL_TEXTURE_2D)) {
    snprintf(out, cap, "-");
    return;
  }
  gl_get_integerv(GL_TEXTURE_BINDING_2D, &name);
  gl_get_tex_parameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &min);
  gl_get_tex_parameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &mag);
  gl_get_tex_enviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &env);
  gl_get_floatv(GL_TEXTURE_MATRIX, m);
  if (name > 0 && name < MAX_NAMES)
    snprintf(out, cap, "#%d %ux%u %x top%u fl%u f%x/%x e%x tm %.3g %.3g %.3g %.3g", name, g_tex[name].width,
             g_tex[name].height, g_tex[name].format, g_tex[name].top, g_tex[name].flags, (unsigned)min,
             (unsigned)mag, (unsigned)env, (double)m[0], (double)m[5], (double)m[12], (double)m[13]);
  else
    snprintf(out, cap, "#%d ? f%x/%x e%x", name, (unsigned)min, (unsigned)mag, (unsigned)env);
}

static void report(char kind, unsigned mode, int count) {
  if (g_draws++ >= MAX_DRAWS)
    return;
  char t0[112], t1[112];
  int active = GL_TEXTURE0, afunc = 0, src = 0, dst = 0, zfunc = 0, zwrite = 0, fbo = 0;
  float aref = 0, mv[16] = {0}, color[4] = {0};
  gl_get_integerv(GL_ACTIVE_TEXTURE, &active);
  unit(0, t0, sizeof t0);
  unit(1, t1, sizeof t1);
  gl_active_texture((unsigned)active);
  gl_get_integerv(GL_ALPHA_TEST_FUNC, &afunc);
  gl_get_floatv(GL_ALPHA_TEST_REF, &aref);
  gl_get_integerv(GL_BLEND_SRC, &src);
  gl_get_integerv(GL_BLEND_DST, &dst);
  gl_get_integerv(GL_DEPTH_FUNC, &zfunc);
  gl_get_integerv(GL_DEPTH_WRITEMASK, &zwrite);
  gl_get_integerv(GL_FRAMEBUFFER_BINDING, &fbo);
  gl_get_floatv(GL_MODELVIEW_MATRIX, mv);
  gl_get_floatv(GL_CURRENT_COLOR, color);
  debugPrintf("[trace] %d %c%u n%d fb%d | %s | %s | a%d %x %.2f b%d %x/%x z%d %x w%d | li%d cull%d fog%d st%d po%d ca%d | "
              "at %.2f %.2f %.2f | c %.2f %.2f %.2f %.2f\n",
              g_draws, kind, mode, count, fbo, t0, t1, gl_is_enabled(GL_ALPHA_TEST), (unsigned)afunc, (double)aref,
              gl_is_enabled(GL_BLEND), (unsigned)src, (unsigned)dst, gl_is_enabled(GL_DEPTH_TEST), (unsigned)zfunc,
              zwrite, gl_is_enabled(GL_LIGHTING), gl_is_enabled(GL_CULL_FACE), gl_is_enabled(GL_FOG),
              gl_is_enabled(GL_STENCIL_TEST), gl_is_enabled(GL_POLYGON_OFFSET_FILL), gl_is_enabled(GL_COLOR_ARRAY),
              (double)mv[12], (double)mv[13], (double)mv[14], (double)color[0], (double)color[1], (double)color[2],
              (double)color[3]);
}

static void w_draw_elements(unsigned mode, int count, unsigned type, const void *indices) {
  if (g_state == 1)
    report('E', mode, count);
  r_draw_elements(mode, count, type, indices);
}

static void w_draw_arrays(unsigned mode, int first, int count) {
  if (g_state == 1)
    report('A', mode, count);
  r_draw_arrays(mode, first, count);
}

/* For port_gl_wrap (ds_gl.c): the two draws go through the wrappers above. */
uintptr_t ds_trace_wrap(const char *name, uintptr_t real) {
  if (!strcmp(name, "glDrawElements")) {
    r_draw_elements = (void (*)(unsigned, int, unsigned, const void *))real;
    return (uintptr_t)w_draw_elements;
  }
  if (!strcmp(name, "glDrawArrays")) {
    r_draw_arrays = (void (*)(unsigned, int, int))real;
    return (uintptr_t)w_draw_arrays;
  }
  return 0;
}

void ds_trace_request(void) {
  if (!g_state)
    g_state = 2;
}

/* Before each of the engine's frames. */
void ds_trace_frame(void) {
  if (g_state == 1) {
    debugPrintf("[trace] end: %d draws\n", g_draws);
    g_state = 0;
  } else if (g_state == 2) {
    gl_get_integerv = (void (*)(unsigned, int *))dcr_gl_lookup("glGetIntegerv");
    gl_get_floatv = (void (*)(unsigned, float *))dcr_gl_lookup("glGetFloatv");
    gl_is_enabled = (uint8_t (*)(unsigned))dcr_gl_lookup("glIsEnabled");
    gl_active_texture = (void (*)(unsigned))dcr_gl_lookup("glActiveTexture");
    gl_get_tex_parameteriv = (void (*)(unsigned, unsigned, int *))dcr_gl_lookup("glGetTexParameteriv");
    gl_get_tex_enviv = (void (*)(unsigned, unsigned, int *))dcr_gl_lookup("glGetTexEnviv");
    if (!gl_get_integerv || !gl_get_floatv || !gl_is_enabled || !gl_active_texture || !gl_get_tex_parameteriv ||
        !gl_get_tex_enviv) {
      debugPrintf("[trace] not possible: a GL function is missing\n");
      g_state = 0;
      return;
    }
    g_state = 1;
    g_draws = 0;
    dcr_gl_request_capture();
    debugPrintf("[trace] a frame's draws, and its picture (capture-NNN.bmp). Each: number, E/A and mode, count, "
                "framebuffer | unit 0 | unit 1 (#name size format top-level flags filters env texture-matrix) | "
                "alpha test, blend, depth | lighting... | modelview translation | colour\n");
  }
}
