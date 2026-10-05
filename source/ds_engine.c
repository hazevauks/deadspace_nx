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

void ds_engine_action(void *hud, int action, int param) {
  if (hud)
    ((void (*)(void *, int, int))AT(OFF_HUD_ACTION))(hud, action, param);
}
