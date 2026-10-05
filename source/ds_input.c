/* ds_input.c -- the controller and the touch screen.
 *
 * The game is played by touch: a finger dragged on the left of the screen
 * moves, one dragged on the right looks, and the rest is taps and swipes on
 * the HUD. Its Java hands touches to the engine as
 * TouchSurfaceAndroid.NativeOnPointerEvent(event, module, pointer id, x, y),
 * in pixels, and keys as KeyboardAndroid.NativeOnKeyDown / Up(module,
 * keyCode, alt), of which the game knows Back alone.
 *
 * A controller, then:
 *   left stick   a finger held on the screen, dragged from a fixed point by
 *                the stick's deflection (as the Vita port does,
 *                v-atamanenko/deadspace-vita, whose numbers these are)
 *   right stick  a finger on the right of the screen that keeps moving at
 *                the stick's rate; at the edge of its patch it is lifted and
 *                put down again in the middle
 *   buttons      the game's own actions, called directly (ds_engine.c);
 *                outside a level B is Android's Back
 *
 * The buttons go by where they are: B (bottom) interacts, A (right) is the
 * weapon's other mode, Y (left) reloads, X (top) is the quick turn, or
 * stasis while aiming. MIT.
 */
#include <math.h>
#include <switch.h>

#include "dcr_config.h"
#include "ds.h"
#include "rt_pad.h"
#include "util.h"

#define ENV g_jni_env
#define SELF g_activity

#define AK_BACK 4 /* android.view.KeyEvent.KEYCODE_BACK */

/* release: the action is called again, with -1, when the button is let go */
static const struct {
  u64 button;
  int action, release;
} k_buttons[] = {
    {HidNpadButton_L | HidNpadButton_ZL, DS_ACT_AIM, 1},
    {HidNpadButton_R | HidNpadButton_ZR, DS_ACT_FIRE, 0},
    {HidNpadButton_B, DS_ACT_INTERACT, 0},
    {HidNpadButton_A, DS_ACT_ALT_FIRE, 0},
    {HidNpadButton_Y, DS_ACT_RELOAD, 0},
    {HidNpadButton_X, DS_ACT_TURN_STASIS, 0},
    {HidNpadButton_StickR, DS_ACT_JUMP, 0},
    {HidNpadButton_Left, DS_ACT_WEAPON_PREV, 0},
    {HidNpadButton_Right, DS_ACT_WEAPON_NEXT, 0},
    {HidNpadButton_Up, DS_ACT_MELEE, 0},
    {HidNpadButton_Down, DS_ACT_LOCATOR, 0},
    {HidNpadButton_Plus, DS_ACT_PAUSE, 0},
    {HidNpadButton_Minus, DS_ACT_RIG, 0},
};
#define NBUTTONS (sizeof k_buttons / sizeof k_buttons[0])

/* The sticks' fingers, on a 960x544 screen (scaled to the rendering size). */
#define DEADZONE 0.15f
#define L_X 192.0f
#define L_Y 75.0f
#define L_RADIUS 75.0f
#define R_X 720.0f
#define R_Y 272.0f
#define R_REACH 160.0f /* how far from its middle the right finger goes */
#define R_SPEED 420.0f /* a second, at full deflection and look_sensitivity 1 */
#define L_POINTER 1
#define R_POINTER 2

static PadState g_pads[2]; /* player 1's controller, and the Joy-Cons on the console */
static u64 g_held;         /* k_buttons rows down, as bits */
static int g_back_down;
static int g_l_down, g_r_down;
static float g_l_x, g_l_y, g_r_x, g_r_y; /* where each stick's finger is, in pixels */
static float g_r_dx, g_r_dy;             /* the right one's place in its patch */
static u64 g_last_tick;

/* The screen's fingers down last frame. The engine's pointer ids skip the
 * sticks'. */
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
  ds_engine_init();
  debugPrintf("[input] one player, the touch screen %s, look sensitivity %.2f\n",
              dcr_config()->touch ? "on" : "off", (double)dcr_config()->look);
}

static void pointer(jint event, int id, float x, float y) {
  if (dcr_config()->log_input && event != g_ids.pointer_move)
    debugPrintf("[input] pointer %s %d at %.0f,%.0f\n", event == g_ids.pointer_down ? "down" : "up", id,
                (double)x, (double)y);
  g_n.OnPointerEvent(ENV, NULL, event, g_ids.touch_screen, id, x, y);
}

static void back_key(int down) {
  if (down == g_back_down)
    return;
  g_back_down = down;
  (down ? g_n.OnKeyDown : g_n.OnKeyUp)(ENV, SELF, g_ids.keyboard, AK_BACK, 0);
}

static void poll_buttons(u64 buttons) {
  void *hud = ds_engine_hud();
  u64 now = 0;
  for (unsigned i = 0; i < NBUTTONS; i++)
    if (buttons & k_buttons[i].button)
      now |= 1ull << i;
  const u64 changed = now ^ g_held;
  for (unsigned i = 0; i < NBUTTONS && changed; i++) {
    if (!(changed & (1ull << i)))
      continue;
    const int down = (now >> i) & 1;
    if (dcr_config()->log_input)
      debugPrintf("[input] action 0x%x %s%s\n", k_buttons[i].action, down ? "down" : "up",
                  hud ? "" : " (no level is being played)");
    if (down)
      ds_engine_action(hud, k_buttons[i].action, 0);
    else if (k_buttons[i].release)
      ds_engine_action(hud, k_buttons[i].action, -1);
  }
  g_held = now;
  back_key(!hud && (buttons & HidNpadButton_B) != 0);
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

static void poll_sticks(float lx, float ly, float rx, float ry, int width, int height, float dt) {
  const float sx = (float)width / 960.0f, sy = (float)height / 544.0f;
  deadzone(&lx, &ly);
  deadzone(&rx, &ry);

  if (lx != 0 || ly != 0) {
    if (!g_l_down)
      pointer(g_ids.pointer_down, L_POINTER, L_X * sx, L_Y * sy);
    g_l_down = 1;
    g_l_x = (L_X + L_RADIUS * lx) * sx;
    g_l_y = (L_Y - L_RADIUS * ly) * sy; /* the screen's y runs down */
    pointer(g_ids.pointer_move, L_POINTER, g_l_x, g_l_y);
  } else if (g_l_down) {
    pointer(g_ids.pointer_up, L_POINTER, g_l_x, g_l_y);
    g_l_down = 0;
  }

  if (rx != 0 || ry != 0) {
    if (!g_r_down) {
      g_r_dx = g_r_dy = 0;
      pointer(g_ids.pointer_down, R_POINTER, R_X * sx, R_Y * sy);
      g_r_down = 1;
    }
    const float step = R_SPEED * dcr_config()->look * dt;
    g_r_dx += rx * step;
    g_r_dy -= ry * step;
    g_r_x = (R_X + g_r_dx) * sx;
    g_r_y = (R_Y + g_r_dy) * sy;
    pointer(g_ids.pointer_move, R_POINTER, g_r_x, g_r_y);
    if (fabsf(g_r_dx) > R_REACH || fabsf(g_r_dy) > R_REACH) { /* down again next frame */
      pointer(g_ids.pointer_up, R_POINTER, g_r_x, g_r_y);
      g_r_down = 0;
    }
  } else if (g_r_down) {
    pointer(g_ids.pointer_up, R_POINTER, g_r_x, g_r_y);
    g_r_down = 0;
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
  const u64 tick = armGetSystemTick();
  float dt = g_last_tick ? (float)armTicksToNs(tick - g_last_tick) * 1e-9f : 1.0f / 60.0f;
  if (dt > 0.05f)
    dt = 0.05f; /* a long frame (a level loading) does not swing the camera */
  g_last_tick = tick;
  poll_buttons(buttons);
  poll_sticks(st[0], st[1], st[2], st[3], width, height, dt);
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
      pointer(g_ids.pointer_up, k_touch_pointer[i], g_touch[i].x, g_touch[i].y);
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
      pointer(g_ids.pointer_down, k_touch_pointer[slot], x, y);
    } else if (g_touch[slot].x != x || g_touch[slot].y != y) {
      pointer(g_ids.pointer_move, k_touch_pointer[slot], x, y);
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
  void *hud = ds_engine_hud();
  for (unsigned i = 0; i < NBUTTONS; i++)
    if ((g_held & (1ull << i)) && k_buttons[i].release)
      ds_engine_action(hud, k_buttons[i].action, -1);
  g_held = 0;
  back_key(0);
  if (g_r_down)
    pointer(g_ids.pointer_up, R_POINTER, g_r_x, g_r_y);
  if (g_l_down)
    pointer(g_ids.pointer_up, L_POINTER, g_l_x, g_l_y);
  g_r_down = g_l_down = 0;
  for (int i = 0; i < MAX_TOUCH; i++)
    if (g_touch[i].used) {
      pointer(g_ids.pointer_up, k_touch_pointer[i], g_touch[i].x, g_touch[i].y);
      g_touch[i].used = 0;
    }
}
