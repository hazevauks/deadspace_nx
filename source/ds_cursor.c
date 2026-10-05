/* ds_cursor.c -- what the port draws over the game: the menus' pointer, and
 * a mark that says the motion aiming was switched on or off.
 *
 * The game's menus are made for a finger and have no selection to move with
 * a D-pad, so a controller gets a pointer: the stick moves it, a button
 * touches the screen under it (ds_input.c). Both pictures are drawn over the
 * finished frame, right before it is presented, with the runtime's overlay
 * (gl_blit.c), which leaves the engine's GL state as it was. MIT.
 */
#include <math.h>
#include <stdint.h>

#include "ds.h"
#include "gl_blit.h"
#include "gl_layer.h"
#include "util.h"

#define SIZE 32       /* each picture, in pixels at 720p */
#define MARK_FRAMES 90 /* how long the motion mark stays */

enum { PIC_CROSS, PIC_DISC, PIC_RING, PICS };

static volatile int g_show, g_mark_frames, g_mark_on;
static int g_x, g_y, g_px, g_width;
static int g_ready; /* 0 not tried, 1 made, -1 failed */
static GLuint g_tex[PICS];

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

/* The pictures as textures, with the engine's own binding put back. */
static void make_textures(void) {
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
  GLint bound = 0;
  get_integerv(GL_TEXTURE_BINDING_2D, &bound);
  gen_textures(PICS, g_tex);
  for (int i = 0; i < PICS; i++) {
    paint(i, img);
    bind_texture(GL_TEXTURE_2D, g_tex[i]);
    tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    tex_parameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    tex_image(GL_TEXTURE_2D, 0, GL_RGBA, SIZE, SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
  }
  bind_texture(GL_TEXTURE_2D, (GLuint)bound);
  g_ready = 1;
}

/* The present hook: on the thread whose GL context it is. */
static void draw(void) {
  if (!g_show && g_mark_frames <= 0)
    return;
  if (!g_ready)
    make_textures();
  if (g_ready != 1)
    return;
  if (g_show)
    dcr_blit_alpha(g_tex[PIC_CROSS], g_x - g_px / 2, g_y - g_px / 2, g_px, g_px, 1.0f);
  if (g_mark_frames > 0) { /* at the top, in the middle; fading over its last third */
    const float alpha = g_mark_frames > MARK_FRAMES / 3 ? 1.0f : (float)g_mark_frames / (MARK_FRAMES / 3);
    dcr_blit_alpha(g_tex[g_mark_on ? PIC_DISC : PIC_RING], (g_width - g_px) / 2, g_px, g_px, g_px, alpha);
    g_mark_frames--;
  }
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
