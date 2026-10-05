/* dcr_config.c -- Dead Space's settings: config.ini's options, on the
 * runtime's INI engine (runtime/source/rt_cfg.c).
 *
 * Written whole on the first start, the options a newer build adds appended
 * at the end ("# Added by build ..."), [config] version = 1 last. Never
 * rename or reorder an option once players have it: their config.ini files
 * must read the same. Booleans take true/false, yes/no, on/off, 1/0. Read
 * once at start-up: changes apply the next time the game starts. MIT.
 */
#include <switch.h>

#include "dcr_config.h"
#include "rt_cfg.h"
#include "util.h"

static DcrConfig g_cfg = {
    .swap_ab = 0,
    .touch = 1,
    .res_w = 1280,
    .res_h = 720,
    .boost = 1,
    .look = 1.0f,
    .gyro_aim_only = 1,
    .gyro_sensitivity = 1.0f,
};

const DcrConfig *dcr_config(void) { return &g_cfg; }

static const CfgOpt k_opts[] = {
    CFG_ROW_SWAP_AB("Swap A and B. false: the buttons work by where they are, as on the\n"
                    "# game's own pad: B (bottom) interacts and confirms, A (right) is kinesis\n"
                    "# and back, Y (left) reloads, X (top) is the quick turn and stasis.",
                    &g_cfg.swap_ab),
    {"touch", "enabled", "true", "The touch screen works as on the phone (handheld mode).", CFG_BOOL,
     NULL, &g_cfg.touch},
    CFG_ROW_RESOLUTION("auto",
                       "Rendering resolution: 720, 1080 or auto (1080 if docked when the game\n"
                       "# starts)."),
    CFG_ROW_BOOST("CPU at 1785 MHz while the game starts (until its first picture).", &g_cfg.boost),
    CFG_ROW_GL_SELFTEST(&g_cfg.gl_selftest),
    CFG_ROW_BOOT_LOG("Show the start-up log on screen at every launch. Off: the log appears only\n"
                     "# while something is being set up (first launch, a new APK or NRO).",
                     &g_cfg.boot_log),
    CFG_ROW_LOG_JNI("Write every Java method the game calls to debug.log (for bug reports).",
                    &g_cfg.log_jni),
    {"debug", "log_input", "false",
     "Write every key and touch the game receives to debug.log (for bug reports).", CFG_BOOL, NULL,
     &g_cfg.log_input},
    {"controls", "look_sensitivity", "1.0",
     "How fast the right stick turns the camera: 0.25 (slow) to 4 (fast).", CFG_FLOAT, NULL,
     &g_cfg.look, 0.25f, 4.0f},
    {"motion", "enabled", "false",
     "Motion aiming: turning the controller (or the console, in handheld mode) turns\n"
     "# the camera. A click of the left stick switches it while playing.",
     CFG_BOOL, NULL, &g_cfg.gyro},
    {"motion", "only_while_aiming", "true",
     "true: the motion counts only while the aim button is held.", CFG_BOOL, NULL,
     &g_cfg.gyro_aim_only},
    {"motion", "sensitivity", "1.0", "How far the camera turns for the controller's turn: 0.25 to 4.",
     CFG_FLOAT, NULL, &g_cfg.gyro_sensitivity, 0.25f, 4.0f},
    {"motion", "invert_horizontal", "false", "Turn the other way, left and right.", CFG_BOOL, NULL,
     &g_cfg.gyro_invert_x},
    {"motion", "invert_vertical", "false", "Turn the other way, up and down.", CFG_BOOL, NULL,
     &g_cfg.gyro_invert_y},
    /* [config] version = 1: the engine's row, last (CfgTable.version) */
};

static void apply(void) {
  const RtConfig *rt = rt_config(); /* the resolution: rt_cfg.c sets the window to it */
  g_cfg.res_w = rt->res_w;
  g_cfg.res_h = rt->res_h;
  const int docked = appletGetOperationMode() == AppletOperationMode_Console;
  debugPrintf("[config] %dx%d (%s, %s); A/B %s, touch %s, CPU boost %s\n", g_cfg.res_w, g_cfg.res_h,
              rt_config_get("display", "resolution"), docked ? "docked" : "handheld",
              g_cfg.swap_ab ? "swapped" : "by position", g_cfg.touch ? "on" : "off",
              g_cfg.boost ? "on" : "off");
}

static const CfgTable k_table = {
    .opts = k_opts,
    .nopts = CFG_COUNT(k_opts),
    .version = 1,
    .apply = apply,
};

void dcr_config_load(void) { rt_config_load(&k_table); }

void dcr_config_set_gyro(int on) {
  g_cfg.gyro = on;
  rt_config_set("motion", "enabled", on ? "true" : "false");
  rt_config_save();
}
