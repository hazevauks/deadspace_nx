/* ds_input.c -- the controller and the touch screen.
 *
 * The game is played by touch: a finger dragged on the left of the screen
 * moves, one dragged on the right looks, a tap fires, and the rest is taps
 * and swipes on the HUD. Its Java hands touches to the engine as
 * TouchSurfaceAndroid.NativeOnPointerEvent(event, module, pointer id, x, y),
 * in pixels, and keys as KeyboardAndroid.NativeOnKeyDown / Up(module,
 * keyCode, alt), of which the game knows Back alone.
 *
 * A controller, while a level is being played:
 *   left stick   a finger held on the screen, dragged from a fixed point by
 *                the stick's deflection (as the Vita port does,
 *                v-atamanenko/deadspace-vita, whose numbers these are)
 *   right stick  the camera, turned as a dragged finger would turn it but
 *                without one: a finger that ends its drag is a tap, and a
 *                tap fires (ds_engine_look)
 *   buttons      the game's own actions, called directly (ds_engine.c)
 * and in the menus, which have nothing a D-pad could move through:
 *   sticks, D-pad  a pointer on the screen (ds_cursor.c)
 *   A              a finger on the screen where the pointer is
 *   B              back
 * as the console's own menus have them. In a level the buttons go by where
 * they are: B (bottom) interacts, A (right) is kinesis, Y (left) reloads, X
 * (top) is the quick turn, or stasis while aiming. The left stick's click
 * switches motion aiming (ds_gyro.c) on and off; + and - together open the
 * port's own settings (ds_menu.c), which then have the controller. MIT.
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

/* release: the action is called again, with -1, when the button is let go.
 * + and - are not here: they act when let go (poll_pad), as pressed together
 * they open the port's settings instead. */
static const struct {
  u64 button;
  int action, release;
} k_buttons[] = {
    {HidNpadButton_L | HidNpadButton_ZL, DS_ACT_AIM, 1},
    {HidNpadButton_ZR, DS_ACT_FIRE, 0},
    {HidNpadButton_R, DS_ACT_ALT_FIRE, 0},
    {HidNpadButton_B, DS_ACT_ACCEPT, 0},
    {HidNpadButton_A, DS_ACT_KINESIS, 0},
    {HidNpadButton_Y, DS_ACT_RELOAD, 0},
    {HidNpadButton_X, DS_ACT_TURN_STASIS, 0},
    {HidNpadButton_StickR, DS_ACT_JUMP, 0},
    {HidNpadButton_Left, DS_ACT_WEAPON_PREV, 0},
    {HidNpadButton_Right, DS_ACT_WEAPON_NEXT, 0},
    {HidNpadButton_Up, DS_ACT_MELEE, 0},
    {HidNpadButton_Down, DS_ACT_LOCATOR, 0},
};
#define NBUTTONS (sizeof k_buttons / sizeof k_buttons[0])

#define DEADZONE 0.15f
/* The left stick's finger, on a 960x544 screen (scaled to the rendering size). */
#define L_X 192.0f
#define L_Y 75.0f
#define L_RADIUS 75.0f
#define L_POINTER 1
/* The right stick at full deflection, as a finger's pixels a second (at
 * look_sensitivity 1). */
#define LOOK_X 900.0f
#define LOOK_Y 600.0f
/* The pointer: its finger, its speed across a 1280x720 screen, and how long
 * it stays on the screen once the controller is left alone. */
#define CURSOR_POINTER 2
#define CURSOR_SPEED 900.0f
#define CURSOR_STEP 24.0f
#define CURSOR_IDLE_S 6.0f

static PadState g_pads[2]; /* player 1's controller, and the Joy-Cons on the console */
static u64 g_buttons;      /* the pad's buttons last frame */
static int g_was_menu = -1;
static int g_back_down;
static int g_l_down;
static float g_l_x, g_l_y; /* where the left stick's finger is, in pixels */
static float g_cx, g_cy;   /* the pointer, in pixels; negative: not placed yet */
static int g_c_down;
static float g_c_idle = CURSOR_IDLE_S;
static int g_handheld; /* the console itself is the controller */
static u64 g_style;    /* player 1's controller */
static int g_combo;    /* + and - were down together: neither acts when let go */
static u64 g_last_tick;

/* The screen's fingers down last frame. The engine's pointer ids skip the
 * controller's two. */
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
  ds_gyro_init();
  g_cx = g_cy = -1;
  debugPrintf("[input] one player, the touch screen %s, look sensitivity %.2f, motion aiming %s\n",
              dcr_config()->touch ? "on" : "off", (double)dcr_config()->look, dcr_config()->gyro ? "on" : "off");
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

static void action(void *hud, int id, int param) {
  if (dcr_config()->log_input)
    debugPrintf("[input] action 0x%x (%d)\n", id, param);
  ds_engine_action(hud, id, param);
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

static void left_finger_up(void) {
  if (g_l_down)
    pointer(g_ids.pointer_up, L_POINTER, g_l_x, g_l_y);
  g_l_down = 0;
}

static void cursor_up(void) {
  if (g_c_down)
    pointer(g_ids.pointer_up, CURSOR_POINTER, g_cx, g_cy);
  g_c_down = 0;
}

/* ------------------------------------------------------- a level, played */
static void poll_game(void *hud, u64 down, u64 up, const float st[4], int width, int height, float dt) {
  for (unsigned i = 0; i < NBUTTONS; i++) {
    if (down & k_buttons[i].button)
      action(hud, k_buttons[i].action, 0);
    else if ((up & k_buttons[i].button) && k_buttons[i].release && !(g_buttons & k_buttons[i].button))
      action(hud, k_buttons[i].action, -1);
  }

  const float sx = (float)width / 960.0f, sy = (float)height / 544.0f;
  if (st[0] != 0 || st[1] != 0) {
    if (!g_l_down)
      pointer(g_ids.pointer_down, L_POINTER, L_X * sx, L_Y * sy);
    g_l_down = 1;
    g_l_x = (L_X + L_RADIUS * st[0]) * sx;
    g_l_y = (L_Y - L_RADIUS * st[1]) * sy; /* the screen's y runs down */
    pointer(g_ids.pointer_move, L_POINTER, g_l_x, g_l_y);
  } else {
    left_finger_up();
  }
  float dx = st[2] * LOOK_X * dcr_config()->look * dt, dy = -st[3] * LOOK_Y * dcr_config()->look * dt;
  float gx = 0, gy = 0;
  if (dcr_config()->gyro && (!dcr_config()->gyro_aim_only || (g_buttons & (HidNpadButton_L | HidNpadButton_ZL))) &&
      ds_gyro_read(g_handheld, g_style, dt, &gx, &gy))
    dx += gx, dy += gy;
  if (dx != 0 || dy != 0)
    ds_engine_look(hud, dx, dy);
}

/* ------------------------------------------------------------- the menus */
static void poll_menu(void *hud, u64 buttons, u64 down, u64 up, const float st[4], int width, int height,
                      float dt) {
  const float scale = (float)width / 1280.0f;
  if (g_cx < 0)
    g_cx = (float)width / 2, g_cy = (float)height / 2;
  float mx = st[0] + st[2], my = st[1] + st[3];
  if (down & HidNpadButton_Left) mx -= CURSOR_STEP / (CURSOR_SPEED * dt);
  if (down & HidNpadButton_Right) mx += CURSOR_STEP / (CURSOR_SPEED * dt);
  if (down & HidNpadButton_Up) my += CURSOR_STEP / (CURSOR_SPEED * dt);
  if (down & HidNpadButton_Down) my -= CURSOR_STEP / (CURSOR_SPEED * dt);
  const int moved = mx != 0 || my != 0;
  if (moved) {
    g_cx += mx * CURSOR_SPEED * scale * dt;
    g_cy -= my * CURSOR_SPEED * scale * dt;
    g_cx = g_cx < 0 ? 0 : g_cx > (float)(width - 1) ? (float)(width - 1) : g_cx;
    g_cy = g_cy < 0 ? 0 : g_cy > (float)(height - 1) ? (float)(height - 1) : g_cy;
  }
  if (moved || (buttons & HidNpadButton_A))
    g_c_idle = 0;
  else
    g_c_idle += dt;

  /* A: a finger where the pointer is, for as long as it is held (a list is
   * dragged by moving the pointer with it down) */
  if (down & HidNpadButton_A) {
    pointer(g_ids.pointer_down, CURSOR_POINTER, g_cx, g_cy);
    g_c_down = 1;
  } else if (g_c_down && moved) {
    pointer(g_ids.pointer_move, CURSOR_POINTER, g_cx, g_cy);
  }
  if (up & HidNpadButton_A)
    cursor_up();

  /* B: the menu's own way back -- the pause menu's and the RIG's through the
   * HUD, the others' through Android's Back key */
  if (hud && (down & HidNpadButton_B))
    action(hud, DS_ACT_CANCEL, 0);
  back_key(!hud && (buttons & HidNpadButton_B) != 0);
}

static void poll_pad(int width, int height) {
  u64 buttons = 0;
  float st[4] = {0, 0, 0, 0};
  for (int i = 0; i < 2; i++) {
    padUpdate(&g_pads[i]);
    if (!padIsConnected(&g_pads[i]))
      continue;
    if (i == 0)
      g_style = padGetStyleSet(&g_pads[i]);
    g_handheld = i == 1;
    float s[4];
    buttons |= rt_pad_read(&g_pads[i], s);
    for (int k = 0; k < 4; k += 2)
      if (s[k] * s[k] + s[k + 1] * s[k + 1] > st[k] * st[k] + st[k + 1] * st[k + 1])
        st[k] = s[k], st[k + 1] = s[k + 1];
  }
  deadzone(&st[0], &st[1]);
  deadzone(&st[2], &st[3]);
  const u64 tick = armGetSystemTick();
  float dt = g_last_tick ? (float)armTicksToNs(tick - g_last_tick) * 1e-9f : 1.0f / 60.0f;
  if (dt > 0.05f)
    dt = 0.05f; /* a long frame (a level loading) does not swing the camera */
  if (dt < 0.001f)
    dt = 0.001f;
  g_last_tick = tick;

  void *hud = ds_engine_hud();
  /* the pointer: no level, a level paused, or one whose input something has
   * taken (a power node lock asking its question, a bench, a store) */
  const int menu = ds_engine_hud_state(hud) != 0 || ds_engine_input_taken(hud);
  if (!menu && dcr_config()->swap_ab) { /* in a level only: the menus keep A and B */
    const u64 ab = buttons & (HidNpadButton_A | HidNpadButton_B);
    if (ab == HidNpadButton_A || ab == HidNpadButton_B)
      buttons ^= HidNpadButton_A | HidNpadButton_B;
  }
  if (menu != g_was_menu) { /* what the other side held is let go */
    if (g_was_menu == 0 && (g_buttons & (HidNpadButton_L | HidNpadButton_ZL)))
      action(hud, DS_ACT_AIM, -1);
    left_finger_up();
    cursor_up();
    back_key(0);
    g_buttons = buttons; /* a button held across the change does nothing new */
    g_was_menu = menu;
    if (dcr_config()->log_input)
      debugPrintf("[input] %s\n", menu ? "a menu: the pointer" : "a level: the game's controls");
  }
  const u64 down = buttons & ~g_buttons, up = g_buttons & ~buttons;
  const u64 both = HidNpadButton_Plus | HidNpadButton_Minus;
  g_buttons = buttons;

  /* + and - together: the port's settings, over a level that is paused first */
  if ((buttons & both) == both && (down & both)) {
    g_combo = 1;
    if (!ds_menu_is_open()) {
      if (!menu) {
        action(hud, DS_ACT_AIM, -1);
        action(hud, DS_ACT_PAUSE, 0);
      }
      left_finger_up();
      cursor_up();
      back_key(0);
    }
    ds_menu_toggle(width, height);
  }
  if (ds_menu_is_open()) { /* the controller is the settings' alone */
    u64 held = buttons;
    if (st[1] > 0.5f) held |= HidNpadButton_Up;
    if (st[1] < -0.5f) held |= HidNpadButton_Down;
    if (st[0] < -0.5f) held |= HidNpadButton_Left;
    if (st[0] > 0.5f) held |= HidNpadButton_Right;
    ds_menu_input(held, dt);
    ds_cursor_set(0, 0, 0);
    g_was_menu = -1; /* what is held when it closes does nothing new */
    return;
  }
  /* + pauses and - opens the RIG when let go, unless they were the two that
   * opened the settings */
  if (up & both) {
    if (!g_combo && hud && (up & HidNpadButton_Plus))
      action(hud, DS_ACT_PAUSE, 0);
    else if (!g_combo && hud && !menu && (up & HidNpadButton_Minus))
      action(hud, DS_ACT_RIG, 0);
    if (!(buttons & both))
      g_combo = 0;
  }
  if ((down & HidNpadButton_StickL) && (buttons & HidNpadButton_StickR)) {
    ds_trace_request(); /* with the right stick held: a frame's report (ds_trace.c) */
    ds_cursor_mark(1);
  } else if (down & HidNpadButton_StickL) { /* motion aiming on / off, kept for the next start */
    DcrConfig *cfg = dcr_config_edit();
    cfg->gyro = !cfg->gyro;
    ds_cursor_mark(cfg->gyro);
    dcr_config_save_controls();
    debugPrintf("[input] motion aiming %s\n", cfg->gyro ? "on" : "off");
  }
  if (menu)
    poll_menu(hud, buttons, down, up, st, width, height, dt);
  else
    poll_game(hud, down, up, st, width, height, dt);
  ds_cursor_set(menu && g_c_idle < CURSOR_IDLE_S, (int)g_cx, (int)g_cy);
}

/* The touch screen reports in 1280x720 whatever the rendering size: the
 * engine takes pixels of its surface. */
static void poll_touch(int width, int height) {
  HidTouchScreenState ts = {0};
  if (!hidGetTouchScreenStates(&ts, 1))
    return;
  if (ts.count > 0)
    g_c_idle = CURSOR_IDLE_S; /* a real finger: the pointer goes */
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
  static int cursor_ready;
  if (!cursor_ready) {
    cursor_ready = 1;
    ds_cursor_init(width, height);
  }
  poll_pad(width, height);
  if (dcr_config()->touch && !ds_menu_is_open())
    poll_touch(width, height);
}

/* Focus lost: Android sends held keys and touches as cancelled. */
void ds_input_reset(void) {
  if (g_was_menu == 0 && (g_buttons & (HidNpadButton_L | HidNpadButton_ZL)))
    action(ds_engine_hud(), DS_ACT_AIM, -1);
  g_buttons = 0;
  back_key(0);
  left_finger_up();
  cursor_up();
  for (int i = 0; i < MAX_TOUCH; i++)
    if (g_touch[i].used) {
      pointer(g_ids.pointer_up, k_touch_pointer[i], g_touch[i].x, g_touch[i].y);
      g_touch[i].used = 0;
    }
}
