/* ds_input.c -- the controller and the touch screen, as an Xperia Play's
 * gamepad and screen (the phone the engine is told it runs on, ds_java.c).
 *
 * On Android:
 *   keys   MainActivity.onKeyDown / onKeyUp -> PhysicalKeyboardAndroid ->
 *          NativeOnKeyDown / Up(module, keyCode, alt). The Xperia Play's
 *          pad is a keyboard: D-pad keys, Cross = DPAD_CENTER, Circle =
 *          BACK with Alt held, Square = BUTTON_X, Triangle = BUTTON_Y, L1,
 *          R1, Start, Select
 *   touch  TouchSurfaceAndroid.onTouchEvent -> NativeOnPointerEvent(event,
 *          module, pointer id, x, y), in pixels; the module is the touch
 *          screen's, or the touch pad's for the Xperia Play's two pads
 *
 * The game has no stick input of its own: it is played by dragging on the
 * left half of the screen (moving) and on the right half or the right touch
 * pad (looking). So, as the Vita port does (v-atamanenko/deadspace-vita,
 * whose numbers these are):
 *   left stick   a finger held on the screen, dragged from a fixed point by
 *                the stick's deflection
 *   right stick  each frame a new drag on the right touch pad, as long as
 *                the deflection: the camera turns at the stick's rate
 *
 * The buttons go by where they are, as on the game's own pad: B (bottom) is
 * Cross, A (right) Circle, Y (left) Square, X (top) Triangle. ZL / ZR do
 * what L / R do (aim, fire). MIT.
 */
#include <math.h>
#include <string.h>
#include <switch.h>

#include "dcr_config.h"
#include "ds.h"
#include "rt_pad.h"
#include "util.h"

#define ENV g_jni_env
#define SELF g_activity

/* android.view.KeyEvent */
enum {
  AK_BACK = 4,
  AK_DPAD_UP = 19,
  AK_DPAD_DOWN = 20,
  AK_DPAD_LEFT = 21,
  AK_DPAD_RIGHT = 22,
  AK_DPAD_CENTER = 23,
  AK_BUTTON_X = 99,
  AK_BUTTON_Y = 100,
  AK_BUTTON_L1 = 102,
  AK_BUTTON_R1 = 103,
  AK_BUTTON_START = 108,
  AK_BUTTON_SELECT = 109,
};

static const struct {
  u64 button;
  int code;
} k_keys[] = {
    {HidNpadButton_B, AK_DPAD_CENTER},        {HidNpadButton_A, AK_BACK},
    {HidNpadButton_Y, AK_BUTTON_X},           {HidNpadButton_X, AK_BUTTON_Y},
    {HidNpadButton_L | HidNpadButton_ZL, AK_BUTTON_L1},
    {HidNpadButton_R | HidNpadButton_ZR, AK_BUTTON_R1},
    {HidNpadButton_Plus, AK_BUTTON_START},    {HidNpadButton_Minus, AK_BUTTON_SELECT},
    {HidNpadButton_Up, AK_DPAD_UP},           {HidNpadButton_Down, AK_DPAD_DOWN},
    {HidNpadButton_Left, AK_DPAD_LEFT},       {HidNpadButton_Right, AK_DPAD_RIGHT},
};
#define NKEYS (sizeof k_keys / sizeof k_keys[0])

/* The sticks' fingers. The left one's point on the screen is given for the
 * Vita's 960x544 and scaled to the rendering size; the right one's is on
 * the Xperia Play's touch pad (966x360, y up). */
#define DEADZONE 0.15f
#define L_X 192.0f
#define L_Y 75.0f
#define L_RADIUS 75.0f
#define R_X 786.0f
#define R_Y 180.0f
#define R_RADIUS_X 155.0f
#define R_RADIUS_Y 105.0f
#define L_POINTER 1
#define R_POINTER 2

static PadState g_pads[2]; /* player 1's controller, and the Joy-Cons on the console */
static u64 g_held;         /* k_keys rows down, as bits */
static int g_l_down, g_r_down;
static float g_l_x, g_l_y, g_r_x, g_r_y; /* where each stick's finger is */

/* The screen's fingers down last frame. The engine's pointer ids skip the
 * left stick's. */
#define MAX_TOUCH 3
static const int k_touch_pointer[MAX_TOUCH] = {0, 3, 4};
static struct {
  int used;
  u32 id;
  float x, y;
} g_touch[MAX_TOUCH];

void ds_input_init(void) {
  rt_pad_setup(1, 1);
  rt_pad_slot(&g_pads[0], 0);
  rt_pad_slot(&g_pads[1], RT_PAD_HANDHELD);
  hidInitializeTouchScreen();
  /* PhysicalKeyboardAndroid: the keyboard (the Xperia Play's pad) slid out */
  if (g_n.OnVisibilityChanged)
    g_n.OnVisibilityChanged(ENV, SELF, g_ids.keyboard, 1);
  debugPrintf("[input] one player, the touch screen %s\n", dcr_config()->touch ? "on" : "off");
}

static void key(int code, int down) {
  if (dcr_config()->log_input)
    debugPrintf("[input] key %s %d\n", down ? "down" : "up", code);
  (down ? g_n.OnKeyDown : g_n.OnKeyUp)(ENV, SELF, g_ids.keyboard, code, 1);
}

static void pointer(jint event, jint module, int id, float x, float y) {
  if (dcr_config()->log_input && event != g_ids.pointer_move)
    debugPrintf("[input] pointer %s %d (module %d) at %.0f,%.0f\n", event == g_ids.pointer_down ? "down" : "up",
                id, (int)module, (double)x, (double)y);
  g_n.OnPointerEvent(ENV, NULL, event, module, id, x, y);
}

/* A stick past its dead zone, from there to full travel as 0..1. */
static void deadzone(float *x, float *y) {
  const float len = sqrtf(*x * *x + *y * *y);
  if (len < DEADZONE) {
    *x = *y = 0;
    return;
  }
  const float k = (len > 1.0f ? 1.0f : (len - DEADZONE) / (1.0f - DEADZONE)) / len;
  *x *= k;
  *y *= k;
}

static void poll_sticks(float lx, float ly, float rx, float ry, int width, int height) {
  const float sx = (float)width / 960.0f, sy = (float)height / 544.0f;
  deadzone(&lx, &ly);
  deadzone(&rx, &ry);

  /* the right stick's drag of last frame ends */
  if (g_r_down) {
    pointer(g_ids.pointer_up, g_ids.touch_pad, R_POINTER, g_r_x, g_r_y);
    g_r_down = 0;
  }
  if (rx != 0 || ry != 0) {
    g_r_x = R_X + R_RADIUS_X * rx;
    g_r_y = R_Y + R_RADIUS_Y * ry;
    pointer(g_ids.pointer_down, g_ids.touch_pad, R_POINTER, R_X, R_Y);
    pointer(g_ids.pointer_move, g_ids.touch_pad, R_POINTER, g_r_x, g_r_y);
    g_r_down = 1;
  }

  if (lx != 0 || ly != 0) {
    if (!g_l_down)
      pointer(g_ids.pointer_down, g_ids.touch_screen, L_POINTER, L_X * sx, L_Y * sy);
    g_l_down = 1;
    g_l_x = (L_X + L_RADIUS * lx) * sx;
    g_l_y = (L_Y - L_RADIUS * ly) * sy; /* the screen's y runs down */
    pointer(g_ids.pointer_move, g_ids.touch_screen, L_POINTER, g_l_x, g_l_y);
  } else if (g_l_down) {
    pointer(g_ids.pointer_up, g_ids.touch_screen, L_POINTER, g_l_x, g_l_y);
    g_l_down = 0;
  }
}

static void poll_pad(int width, int height) {
  u64 buttons = 0;
  float st[4] = {0, 0, 0, 0};
  for (int i = 0; i < 2; i++) {
    padUpdate(&g_pads[i]);
    if (!padIsConnected(&g_pads[i]))
      continue;
    float s[4];
    buttons |= rt_pad_read(&g_pads[i], s);
    for (int k = 0; k < 4; k += 2)
      if (s[k] * s[k] + s[k + 1] * s[k + 1] > st[k] * st[k] + st[k + 1] * st[k + 1])
        st[k] = s[k], st[k + 1] = s[k + 1];
  }
  if (dcr_config()->swap_ab) {
    const u64 ab = buttons & (HidNpadButton_A | HidNpadButton_B);
    if (ab == HidNpadButton_A || ab == HidNpadButton_B)
      buttons ^= HidNpadButton_A | HidNpadButton_B;
  }
  u64 now = 0;
  for (unsigned i = 0; i < NKEYS; i++)
    if (buttons & k_keys[i].button)
      now |= 1ull << i;
  const u64 changed = now ^ g_held;
  for (unsigned i = 0; i < NKEYS && changed; i++)
    if (changed & (1ull << i))
      key(k_keys[i].code, (now >> i) & 1);
  g_held = now;
  poll_sticks(st[0], st[1], st[2], st[3], width, height);
}

/* The touch screen reports in 1280x720 whatever the rendering size: the
 * engine takes pixels of its surface. */
static void poll_touch(int width, int height) {
  HidTouchScreenState ts = {0};
  if (!hidGetTouchScreenStates(&ts, 1))
    return;
  /* lifted: down last frame, gone now */
  for (int i = 0; i < MAX_TOUCH; i++) {
    int still = 0;
    for (int j = 0; j < ts.count && !still; j++)
      still = g_touch[i].used && ts.touches[j].finger_id == g_touch[i].id;
    if (g_touch[i].used && !still) {
      pointer(g_ids.pointer_up, g_ids.touch_screen, k_touch_pointer[i], g_touch[i].x, g_touch[i].y);
      g_touch[i].used = 0;
    }
  }
  /* new or moved */
  for (int j = 0; j < ts.count; j++) {
    const float x = (float)ts.touches[j].x * (float)width / 1280.0f;
    const float y = (float)ts.touches[j].y * (float)height / 720.0f;
    int slot = -1, spare = -1;
    for (int i = 0; i < MAX_TOUCH; i++) {
      if (g_touch[i].used && g_touch[i].id == ts.touches[j].finger_id)
        slot = i;
      else if (!g_touch[i].used && spare < 0)
        spare = i;
    }
    if (slot < 0) {
      if (spare < 0)
        continue; /* more fingers than the game is given */
      slot = spare;
      g_touch[slot].used = 1;
      g_touch[slot].id = ts.touches[j].finger_id;
      pointer(g_ids.pointer_down, g_ids.touch_screen, k_touch_pointer[slot], x, y);
    } else if (g_touch[slot].x != x || g_touch[slot].y != y) {
      pointer(g_ids.pointer_move, g_ids.touch_screen, k_touch_pointer[slot], x, y);
    }
    g_touch[slot].x = x;
    g_touch[slot].y = y;
  }
}

void ds_input_poll(int width, int height) {
  poll_pad(width, height);
  if (dcr_config()->touch)
    poll_touch(width, height);
}

/* Focus lost: Android sends held keys and touches as cancelled. */
void ds_input_reset(void) {
  for (unsigned i = 0; i < NKEYS; i++)
    if (g_held & (1ull << i))
      key(k_keys[i].code, 0);
  g_held = 0;
  if (g_r_down)
    pointer(g_ids.pointer_up, g_ids.touch_pad, R_POINTER, g_r_x, g_r_y);
  if (g_l_down)
    pointer(g_ids.pointer_up, g_ids.touch_screen, L_POINTER, g_l_x, g_l_y);
  g_r_down = g_l_down = 0;
  for (int i = 0; i < MAX_TOUCH; i++)
    if (g_touch[i].used) {
      pointer(g_ids.pointer_up, g_ids.touch_screen, k_touch_pointer[i], g_touch[i].x, g_touch[i].y);
      g_touch[i].used = 0;
    }
}
