/* ds_cursor.c -- what the port draws over the game: the menus' pointer, a
 * mark that says motion aiming was switched on or off, and its own settings
 * (ds_menu.c).
 *
 * The game's menus are made for a finger and have no selection to move with
 * a D-pad, so a controller gets a pointer: the stick moves it, a button
 * touches the screen under it (ds_input.c). Everything here is drawn over
 * the finished frame, right before it is presented, with the runtime's
 * overlay (gl_blit.c), which leaves the engine's GL state as it was. MIT.
 */
#include <math.h>
#include <stdint.h>

#include "ds.h"
#include "gl_blit.h"
#include "gl_layer.h"
#include "util.h"

#define SIZE 32        /* each picture, in pixels at 720p */
#define MARK_FRAMES 90 /* how long the motion mark stays */

enum { PIC_CROSS, PIC_DISC, PIC_RING, PICS };

static volatile int g_show, g_mark_frames, g_mark_on;
static int g_x, g_y, g_px, g_width;
static int g_ready; /* 0 not tried, 1 the overlay works, -1 it does not */
static GLuint g_tex[PICS];

static void (*gl_get_integerv)(GLenum, GLint *);
static void (*gl_gen_textures)(GLsizei, GLuint *);
static void (*gl_bind_texture)(GLenum, GLuint);
static void (*gl_tex_parameteri)(GLenum, GLenum, GLint);
static void (*gl_tex_image)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);

static int overlay_ready(void) {
  if (g_ready)
    return g_ready == 1;
  gl_get_integerv = (void (*)(GLenum, GLint *))dcr_gl_lookup("glGetIntegerv");
  gl_gen_textures = (void (*)(GLsizei, GLuint *))dcr_gl_lookup("glGenTextures");
  gl_bind_texture = (void (*)(GLenum, GLuint))dcr_gl_lookup("glBindTexture");
  gl_tex_parameteri = (void (*)(GLenum, GLenum, GLint))dcr_gl_lookup("glTexParameteri");
  gl_tex_image = (void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *))
      dcr_gl_lookup("glTexImage2D");
  g_ready = gl_get_integerv && gl_gen_textures && gl_bind_texture && gl_tex_parameteri && gl_tex_image &&
                    dcr_blit_setup("the port's overlay") == 1
                ? 1
                : -1;
  if (g_ready < 0)
    debugPrintf("[cursor] the overlay is not there: no pointer, no settings screen\n");
  return g_ready == 1;
}

/* The engine's own texture binding is put back. */
unsigned ds_overlay_texture(const uint8_t *rgba, int width, int height, int nearest) {
  if (!overlay_ready())
    return 0;
  GLint bound = 0;
  GLuint tex = 0;
  gl_get_integerv(GL_TEXTURE_BINDING_2D, &bound);
  gl_gen_textures(1, &tex);
  gl_bind_texture(GL_TEXTURE_2D, tex);
  gl_tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
  gl_tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
  gl_tex_image(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  gl_bind_texture(GL_TEXTURE_2D, (GLuint)bound);
  return tex;
}

/* White with a dark rim, so that it shows on any background:
 *   the pointer      a thin cross, its middle the spot that is touched
 *   motion aiming    a disc for on, a ring for off */
static void paint(int pic, uint8_t *img) {
  const float c = (SIZE - 1) / 2.0f;
  for (int y = 0; y < SIZE; y++)
    for (int x = 0; x < SIZE; x++) {
      const float dx = fabsf((float)x - c), dy = fabsf((float)y - c), d = sqrtf(dx * dx + dy * dy);
      int white, rim;
      if (pic == PIC_CROSS) {
        white = (dy < 1.0f && dx < 13.0f) || (dx < 1.0f && dy < 13.0f);
        rim = (dy < 2.5f && dx < 14.5f) || (dx < 2.5f && dy < 14.5f);
      } else {
        white = d < 10.0f && (pic == PIC_DISC || d > 7.0f);
        rim = d < 12.0f && (pic == PIC_DISC || d > 5.0f);
      }
      uint8_t *p = &img[(y * SIZE + x) * 4];
      p[0] = p[1] = p[2] = white ? 255 : 0;
      p[3] = white ? 255 : rim ? 150 : 0;
    }
}

/* The present hook: on the thread whose GL context it is. */
static void draw(void) {
  static uint8_t img[SIZE * SIZE * 4];
  ds_gl_overlay(1);
  if ((g_show || g_mark_frames > 0) && overlay_ready()) {
    if (!g_tex[0])
      for (int i = 0; i < PICS; i++) {
        paint(i, img);
        g_tex[i] = ds_overlay_texture(img, SIZE, SIZE, 0);
      }
    if (g_show)
      dcr_blit_alpha(g_tex[PIC_CROSS], g_x - g_px / 2, g_y - g_px / 2, g_px, g_px, 1.0f);
    if (g_mark_frames > 0) { /* at the top, in the middle; fading over its last third */
      const float alpha = g_mark_frames > MARK_FRAMES / 3 ? 1.0f : (float)g_mark_frames / (MARK_FRAMES / 3);
      dcr_blit_alpha(g_tex[g_mark_on ? PIC_DISC : PIC_RING], (g_width - g_px) / 2, g_px, g_px, g_px, alpha);
      g_mark_frames--;
    }
  }
  ds_menu_draw();
  ds_gl_overlay(0);
}

void ds_cursor_init(int width, int height) {
  g_width = width;
  g_px = SIZE * height / 720;
  dcr_present_hook = draw;
}

void ds_cursor_set(int show, int x, int y) {
  g_x = x;
  g_y = y;
  g_show = show;
}

void ds_cursor_mark(int on) {
  g_mark_on = on;
  g_mark_frames = MARK_FRAMES;
}
