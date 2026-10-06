/* ds_engine.c -- the game's own functions, reached by address.
 *
 * This build of the game (the Amazon one) is played by touch only: its key
 * handler (Application::OnKeyDown) knows Back and Menu and nothing else. But
 * the function a gamepad's buttons went to in the Xperia Play build is still
 * in it, whole: Hud::doSpecialAction(action, param), which aims, fires,
 * reloads, changes weapon... (DsAction, ds.h). The port's buttons call it
 * directly, on the game's thread, between two frames -- where the engine's
 * own message queue would have delivered a key.
 *
 * ds_engine_look turns the camera the same way: what the touch screen's look
 * pad would send the player, without a finger.
 *
 * None of these functions is exported, so they are found by their place in
 * libDeadSpace.so 1.2.0 (names from its .symtab, tools/armdis.pl), and only
 * after the instructions there are checked to be that build's. With another
 * build the buttons do nothing (ds_engine_init says so in the log). MIT.
 */
#include <stdint.h>

#include "ds.h"
#include "gl_layer.h"
#include "util.h"

#define AT(off) ((uintptr_t)g_mod_game.load_virtbase + (off))

#define OFF_APP_INSTANCE 0x207ee8 /* Application::getInstance() */
#define OFF_OBJECT_HUD 0x21cb2c   /* GameObject::getHud() */
#define OFF_HUD_ACTION 0x7541c    /* Hud::doSpecialAction(int, int) */
/* Application: its GameWorld; GameWorld: its player, and its state (0 and 1:
 * not being played), as Application::OnKeyDown reads them */
#define APP_WORLD 204
#define WORLD_PLAYER 88
#define WORLD_STATE 0x16a0
/* Hud: its state, its world; that world's player, as Hud::doSpecialAction
 * reads them */
#define HUD_STATE 664
#define HUD_INPUT_STATE 676 /* 2: an object of the level has the input (Hud::objectGetInput) */
/* GameObjectPlayable: the weapon in hand (an index, and the five weapons' slots),
 * as getCurrentWeapon() reads them */
#define PLAYER_WEAPON_INDEX 744
#define PLAYER_WEAPONS 724
#define HUD_WORLD 4
#define WORLD_HUD_PLAYER 212
#define OFF_VECTOR2_VTABLE 0x4794b8 /* vtable for Vector2Event */

/* What is at a few addresses in the build these are for. */
static const struct {
  uint32_t off, word;
} k_marks[] = {
    {OFF_HUD_ACTION, 0xe92d4ff0}, {OFF_HUD_ACTION + 4, 0xed2d8b06}, {0x76324, DS_ACT_AIM},
    {OFF_APP_INSTANCE, 0xeaffffcf}, {OFF_OBJECT_HUD, 0xeafd7f17},
};

static int g_known;

int ds_engine_init(void) {
  g_known = 1;
  for (unsigned i = 0; i < sizeof k_marks / sizeof k_marks[0]; i++)
    if (*(const uint32_t *)AT(k_marks[i].off) != k_marks[i].word)
      g_known = 0;
  debugPrintf("[engine] %s\n", g_known ? "libDeadSpace.so 1.2.0: the game's actions are reached directly"
                                       : "an unknown build of libDeadSpace.so: the buttons have no actions");
  return g_known;
}

/* The HUD while a level is being played, else NULL (menus, loading). The
 * first frames are left alone: getInstance() would create the game before
 * the engine does. */
void *ds_engine_hud(void) {
  if (!g_known || dcr_gl_frames() < 30)
    return NULL;
  const uint8_t *app = ((const uint8_t *(*)(void))AT(OFF_APP_INSTANCE))();
  const uint8_t *world = app ? *(const uint8_t *const *)(app + APP_WORLD) : NULL;
  const void *player = world ? *(const void *const *)(world + WORLD_PLAYER) : NULL;
  if (!player || *(const uint32_t *)(world + WORLD_STATE) < 2)
    return NULL;
  return ((void *(*)(const void *))AT(OFF_OBJECT_HUD))(player);
}

int ds_engine_hud_state(const void *hud) { return hud ? *(const int *)((const uint8_t *)hud + HUD_STATE) : -1; }

/* A power node lock asking its question, a bench, a store, a cinematic: the
 * level is loaded and not paused, but what is on the screen is theirs. */
int ds_engine_input_taken(const void *hud) {
  return hud && *(const int *)((const uint8_t *)hud + HUD_INPUT_STATE) == 2;
}

static void **hud_player(void *hud) {
  return *(void ***)(*(uint8_t **)((uint8_t *)hud + HUD_WORLD) + WORLD_HUD_PLAYER);
}

/* Aiming with no weapon in hand is refused here, as the game's own aim
 * button is (it is not on the screen then): doSpecialAction notes that the
 * button is held before it looks for a weapon, and the end of the next
 * animation (a swing of the plasma saw, the one tool the player has before
 * the first weapon) raises a weapon that is not there -- a fault at
 * GameObjectPlayable::setAiming+0xd4. */
void ds_engine_action(void *hud, int action, int param) {
  if (!hud)
    return;
  if (action == DS_ACT_AIM && param != -1) {
    const uint8_t *player = (const uint8_t *)hud_player(hud);
    const uint32_t index = player ? *(const uint32_t *)(player + PLAYER_WEAPON_INDEX) : 5;
    if (index > 4 || !*(const void *const *)(player + PLAYER_WEAPONS + index * 4))
      return;
  }
  ((void (*)(void *, int, int))AT(OFF_HUD_ACTION))(hud, action, param);
}

/* What the touch screen's "look" pad (an InputForwarderTouchDPad of
 * InputSchemeDPadsRel) sends the player for a finger that moved: a
 * Vector2Event, type 0x3ee, with the pad's number and the pixels moved.
 * GameObjectPlayable::onEvent turns them into adjustYaw / adjustPitch, by
 * the game's own sensitivity setting. Pad 3 is the camera; pad 5 takes its
 * place in one state of the player, and each is ignored in the other's. */
void ds_engine_look(void *hud, float dx, float dy) {
  struct {
    const void *vtable;
    int type, pad;
    float x, y;
    int flag;
  } ev = {(const void *)(AT(OFF_VECTOR2_VTABLE) + 8), 0x3ee, 3, dx, dy, 0};
  if (!hud)
    return;
  void **player = hud_player(hud);
  if (!player)
    return;
  int (*on_event)(void *, const void *) = ((int (**)(void *, const void *))*player)[2];
  on_event(player, &ev);
  ev.pad = 5;
  on_event(player, &ev);
}
