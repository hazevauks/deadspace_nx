/* ds_cursor.c -- a pointer drawn over the game's menus.
 *
 * The game's menus are made for a finger and have no selection to move with
 * a D-pad, so a controller gets a pointer: the stick moves it, a button
 * touches the screen under it (ds_input.c). It is drawn over the finished
 * frame, right before it is presented, with the runtime's overlay
 * (gl_blit.c), which leaves the engine's GL state as it was. MIT.
 */
#include <stdint.h>

#include "ds.h"
#include "gl_blit.h"
#include "gl_layer.h"
#include "util.h"

#define SIZE 32 /* the picture: a white disc with a dark rim */

static volatile int g_show;
static int g_x, g_y, g_px;
static int g_ready; /* 0 not tried, 1 made, -1 failed */
static GLuint g_tex;

/* The picture as a texture, with the engine's own binding put back. */
static void make_texture(void) {
  static uint8_t img[SIZE * SIZE * 4];
  void (*get_integerv)(GLenum, GLint *) = (void (*)(GLenum, GLint *))dcr_gl_lookup("glGetIntegerv");
  void (*gen_textures)(GLsizei, GLuint *) = (void (*)(GLsizei, GLuint *))dcr_gl_lookup("glGenTextures");
  void (*bind_texture)(GLenum, GLuint) = (void (*)(GLenum, GLuint))dcr_gl_lookup("glBindTexture");
  void (*tex_parameteri)(GLenum, GLenum, GLint) =
      (void (*)(GLenum, GLenum, GLint))dcr_gl_lookup("glTexParameteri");
  void (*tex_image)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *) =
      (void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *))dcr_gl_lookup(
          "glTexImage2D");
  g_ready = -1;
  if (!get_integerv || !gen_textures || !bind_texture || !tex_parameteri || !tex_image ||
      dcr_blit_setup("the pointer") != 1) {
    debugPrintf("[cursor] the overlay is not there: no pointer in the menus\n");
    return;
  }
  for (int y = 0; y < SIZE; y++)
    for (int x = 0; x < SIZE; x++) {
      const float dx = (float)x - (SIZE - 1) / 2.0f, dy = (float)y - (SIZE - 1) / 2.0f;
      const float d2 = dx * dx + dy * dy;
      uint8_t *p = &img[(y * SIZE + x) * 4];
      const uint8_t shade = d2 < 9.0f * 9.0f ? 255 : 20; /* the disc, then its rim */
      p[0] = p[1] = p[2] = shade;
      p[3] = d2 < 13.0f * 13.0f ? 230 : 0;
    }
  GLint bound = 0;
  get_integerv(GL_TEXTURE_BINDING_2D, &bound);
  gen_textures(1, &g_tex);
  bind_texture(GL_TEXTURE_2D, g_tex);
  tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  tex_image(GL_TEXTURE_2D, 0, GL_RGBA, SIZE, SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
  bind_texture(GL_TEXTURE_2D, (GLuint)bound);
  g_ready = 1;
}

/* The present hook: on the thread whose GL context it is. */
static void draw(void) {
  if (!g_show)
    return;
  if (!g_ready)
    make_texture();
  if (g_ready == 1)
    dcr_blit_alpha(g_tex, g_x - g_px / 2, g_y - g_px / 2, g_px, g_px, 1.0f);
}

void ds_cursor_init(int height) {
  g_px = SIZE * height / 720;
  dcr_present_hook = draw;
}

void ds_cursor_set(int show, int x, int y) {
  g_x = x;
  g_y = y;
  g_show = show;
}
