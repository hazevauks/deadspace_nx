/* ds_menu.c -- the port's own settings, on the screen.
 *
 * + and - pressed together open it, over whatever the game shows; a level
 * that is being played is paused first. Up and down choose a line, left and
 * right change it, B (or + and - again) closes it and writes the choices to
 * config.ini, where the same options can be edited by hand.
 *
 * The game's own fonts cannot be used from outside it, so the screen has a
 * small one of its own: capitals, digits and a few signs, five pixels by
 * seven, drawn from a texture made at the first use. Everything is drawn
 * with the runtime's overlay (gl_blit.c) from the frame's present hook
 * (ds_cursor.c). MIT.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "dcr_config.h"
#include "ds.h"
#include "gl_blit.h"
#include "util.h"

/* ------------------------------------------------------------------ font */
/* One glyph a line: seven rows, the five low bits of each its pixels, the
 * leftmost the highest. The first is the blank; a solid cell follows the
 * last (the panel is drawn with it). */
static const char k_chars[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.:-+/<>";
static const uint8_t k_glyphs[][7] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* space */
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, /* A */
    {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}, /* B */
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, /* C */
    {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}, /* D */
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, /* E */
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}, /* F */
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, /* G */
    {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, /* H */
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, /* I */
    {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}, /* J */
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, /* K */
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}, /* L */
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, /* M */
    {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}, /* N */
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, /* O */
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}, /* P */
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, /* Q */
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}, /* R */
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, /* S */
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, /* T */
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, /* U */
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}, /* V */
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}, /* W */
    {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}, /* X */
    {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}, /* Y */
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}, /* Z */
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, /* 0 */
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, /* 1 */
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, /* 2 */
    {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}, /* 3 */
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, /* 4 */
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, /* 5 */
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, /* 6 */
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, /* 7 */
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, /* 8 */
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}, /* 9 */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}, /* . */
    {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}, /* : */
    {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}, /* - */
    {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}, /* + */
    {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}, /* / */
    {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}, /* < */
    {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}, /* > */
};
#define NGLYPHS ((int)(sizeof k_glyphs / sizeof k_glyphs[0]))
#define SOLID NGLYPHS /* the cell after the last glyph */

/* The texture: cells of 8x8 pixels, sixteen to a row. */
#define CELL 8
#define COLS 16
#define TEX_W 128
#define TEX_H 32
#define ADVANCE 6 /* a glyph's five pixels and one of space */

static unsigned g_font; /* the texture; 0 until the first draw */

static int make_font(void) {
  static uint8_t img[TEX_W * TEX_H * 4];
  memset(img, 0, sizeof img);
  for (int i = 0; i <= NGLYPHS; i++) {
    const int ox = i % COLS * CELL, oy = i / COLS * CELL;
    for (int y = 0; y < CELL; y++)
      for (int x = 0; x < CELL; x++) {
        const int on = i == SOLID || (y < 7 && x < 5 && (k_glyphs[i][y] >> (4 - x) & 1));
        if (on)
          memset(&img[((oy + y) * TEX_W + ox + x) * 4], 255, 4);
      }
  }
  g_font = ds_overlay_texture(img, TEX_W, TEX_H, 1);
  return g_font != 0;
}

/* ------------------------------------------------------------ the batch */
/* Triangles of the font texture, drawn in one colour at a time. */
#define MAX_VERTS 3072
static GLfloat g_pos[MAX_VERTS * 2], g_uv[MAX_VERTS * 2];
static int g_nverts;

static void quad(float x, float y, float w, float h, int cell, float inset) {
  if (g_nverts + 6 > MAX_VERTS)
    return;
  const float u0 = ((float)(cell % COLS * CELL) + inset) / TEX_W;
  const float v0 = ((float)(cell / COLS * CELL) + inset) / TEX_H;
  const float u1 = ((float)(cell % COLS * CELL + CELL) - inset) / TEX_W;
  const float v1 = ((float)(cell / COLS * CELL + CELL) - inset) / TEX_H;
  const GLfloat p[12] = {x, y, x + w, y, x, y + h, x + w, y, x + w, y + h, x, y + h};
  const GLfloat t[12] = {u0, v0, u1, v0, u0, v1, u1, v0, u1, v1, u0, v1};
  memcpy(&g_pos[g_nverts * 2], p, sizeof p);
  memcpy(&g_uv[g_nverts * 2], t, sizeof t);
  g_nverts += 6;
}

static void box(float x, float y, float w, float h) { quad(x, y, w, h, SOLID, 2.0f); }

/* A line of text, its top left at x, y; `px` is the size of one font pixel. */
static void text(float x, float y, float px, const char *s) {
  for (; *s; s++, x += ADVANCE * px) {
    const char *at = strchr(k_chars, *s);
    if (at && at != k_chars)
      quad(x, y, CELL * px, CELL * px, (int)(at - k_chars), 0.0f);
  }
}

static float text_width(float px, const char *s) { return (float)strlen(s) * ADVANCE * px; }

static void flush(float r, float g, float b, float a) {
  const GLfloat rgba[4] = {r, g, b, a};
  if (g_nverts)
    dcr_blit_mesh(g_font, g_pos, g_uv, g_nverts, rgba);
  g_nverts = 0;
}

/* ------------------------------------------------------------- the lines */
enum { ROW_LOOK, ROW_GYRO, ROW_GYRO_SPEED, ROW_GYRO_AIM, ROW_GYRO_X, ROW_GYRO_Y, ROWS };
static const char *const k_labels[ROWS] = {
    "CAMERA SENSITIVITY",       "MOTION AIMING",            "MOTION SENSITIVITY",
    "MOTION ONLY WHILE AIMING", "INVERT MOTION LEFT/RIGHT", "INVERT MOTION UP/DOWN",
};
#define STEP 0.05f
#define LOWEST 0.25f
#define HIGHEST 4.0f

static int g_open, g_row, g_w, g_h;
static u64 g_held;
static float g_hold_s, g_repeat_s;

static float *row_number(int row) {
  DcrConfig *c = dcr_config_edit();
  return row == ROW_LOOK ? &c->look : row == ROW_GYRO_SPEED ? &c->gyro_sensitivity : NULL;
}

static int *row_switch(int row) {
  DcrConfig *c = dcr_config_edit();
  return row == ROW_GYRO       ? &c->gyro
         : row == ROW_GYRO_AIM ? &c->gyro_aim_only
         : row == ROW_GYRO_X   ? &c->gyro_invert_x
         : row == ROW_GYRO_Y   ? &c->gyro_invert_y
                               : NULL;
}

static void change(int row, int dir) {
  float *n = row_number(row);
  int *s = row_switch(row);
  if (n) {
    float v = roundf((*n + (float)dir * STEP) / STEP) * STEP;
    *n = v < LOWEST ? LOWEST : v > HIGHEST ? HIGHEST : v;
  } else if (s) {
    *s = !*s;
  }
}

int ds_menu_is_open(void) { return g_open; }

void ds_menu_toggle(int width, int height) {
  g_open = !g_open;
  g_w = width, g_h = height;
  g_held = ~0ull; /* what is held now is not a new press */
  g_hold_s = g_repeat_s = 0;
  if (!g_open)
    dcr_config_save_controls();
  debugPrintf("[menu] the port's settings %s\n", g_open ? "opened" : "closed and saved");
}

void ds_menu_input(u64 held, float dt) {
  const u64 down = held & ~g_held;
  g_held = held;
  if (down & HidNpadButton_B) {
    ds_menu_toggle(g_w, g_h);
    return;
  }
  if (down & HidNpadButton_Up)
    g_row = (g_row + ROWS - 1) % ROWS;
  if (down & HidNpadButton_Down)
    g_row = (g_row + 1) % ROWS;
  const int dir = (held & HidNpadButton_Right ? 1 : 0) - (held & HidNpadButton_Left ? 1 : 0);
  if (!dir) {
    g_hold_s = g_repeat_s = 0;
  } else if (down & (HidNpadButton_Left | HidNpadButton_Right)) {
    change(g_row, dir);
  } else if (row_number(g_row)) { /* held: after a moment, twenty steps a second */
    g_hold_s += dt;
    g_repeat_s += dt;
    if (g_hold_s > 0.4f && g_repeat_s > 0.05f) {
      g_repeat_s = 0;
      change(g_row, dir);
    }
  }
  if ((down & HidNpadButton_A) && row_switch(g_row))
    change(g_row, 1);
}

void ds_menu_draw(void) {
  if (!g_open || (!g_font && !make_font()))
    return;
  const float px = (float)(g_h * 3 / 720); /* a whole number: the font stays sharp */
  const float k = px / 3.0f;
  const float pw = 800 * k, ph = 470 * k, x0 = ((float)g_w - pw) / 2, y0 = ((float)g_h - ph) / 2;
  const float row_h = 46 * k, rows_y = y0 + 96 * k, left = x0 + 34 * k, right = x0 + pw - 34 * k;

  box(x0, y0, pw, ph);
  flush(0.02f, 0.05f, 0.07f, 0.92f);
  box(x0, y0, pw, 3 * k);
  box(x0, y0 + 66 * k, pw, 2 * k);
  box(x0 + 8 * k, rows_y + (float)g_row * row_h - 10 * k, pw - 16 * k, row_h - 4 * k);
  flush(0.27f, 0.78f, 0.90f, 0.35f);

  text(left, y0 + 26 * k, px, "DEAD SPACE: SABOTAGE - PORT SETTINGS");
  for (int i = 0; i < ROWS; i++) {
    char value[24];
    const float *n = row_number(i);
    const int *s = row_switch(i);
    if (n)
      snprintf(value, sizeof value, "< %.2f >", (double)*n);
    else
      snprintf(value, sizeof value, "< %s >", s && *s ? "ON" : "OFF");
    const float y = rows_y + (float)i * row_h;
    text(left, y, px, k_labels[i]);
    text(right - text_width(px, value), y, px, value);
  }
  text(left, y0 + ph - 78 * k, px, "UP/DOWN: CHOOSE    LEFT/RIGHT: CHANGE");
  text(left, y0 + ph - 42 * k, px, "B OR + AND -: CLOSE AND SAVE");
  flush(1.0f, 1.0f, 1.0f, 1.0f);
}
