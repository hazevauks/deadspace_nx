/* ds.h -- what Dead Space's port files share: the engine's module, its
 * natives (com.ea.blast's and friends), and each file's entry points. MIT.
 */
#ifndef DS_H
#define DS_H

#include <stdint.h>

#include "jni.h"
#include "so_util.h"

/* ------------------------------------------------------------- the engine */
extern so_module g_mod_game;

/* The natives the port calls, as the Java declares them (classes.dex): all
 * are exported by name. The second argument is the receiver (the class
 * object for a static one). Floats travel in core registers (softfp), as
 * the engine was built. */
typedef struct {
  /* com.ea.blast.MainActivity */
  void (*OnCreate)(void *env, void *self);
  void (*OnPause)(void *env, void *self);
  void (*OnStop)(void *env, void *self);
  void (*OnWindowFocusChanged)(void *env, void *self, jboolean focus);
  /* com.ea.blast.AndroidRenderer */
  void (*OnSurfaceChanged)(void *env, void *self, jint width, jint height);
  void (*OnDrawFrame)(void *env, void *self);
  /* com.ea.blast.KeyboardAndroid */
  void (*OnKeyDown)(void *env, void *self, jint module, jint key, jint alt);
  void (*OnKeyUp)(void *env, void *self, jint module, jint key, jint alt);
  /* com.ea.blast.TouchSurfaceAndroid, AccelerometerAndroidDelegate */
  void (*OnPointerEvent)(void *env, void *cls, jint event, jint module, jint pointer, jfloat x, jfloat y);
  void (*OnAcceleration)(void *env, void *self, jfloat x, jfloat y, jfloat z);
  /* com.ea.EAIO.EAIO, com.ea.EAAudioCore.AndroidEAAudioCore */
  void (*IoStartup)(void *env, void *cls, void *asset_manager);
  void (*IoShutdown)(void *env, void *cls);
  void (*AudioInit)(void *env, void *cls, void *track, jint buffer_bytes, jint channels, jint rate);
  void (*AudioRelease)(void *env, void *cls);
} DsNatives;
extern DsNatives g_n;

/* The engine's own numbers for its input modules, pointer events and screen
 * orientation (ModuleCatalog.NativeGetModuleTypeId*, TouchSurfaceAndroid
 * .NativeGetIdRawPointer*, DisplayAndroidDelegate.NativeGetOrientationNormal),
 * asked once. */
typedef struct {
  jint keyboard, touch_screen, touch_pad;
  jint pointer_down, pointer_move, pointer_up;
  jint orientation_normal;
} DsIds;
extern DsIds g_ids;

/* ds_loader.c */
int ds_load_engine(void);       /* 0, or negative (logged) */
void ds_run_constructors(void); /* System.loadLibrary: the init array, JNI_OnLoad */

/* ds_engine.c: the game's own functions, by address (one build of it) */
/* Hud::doSpecialAction's actions. Its param is 0, but for DS_ACT_AIM: 0 the
 * button pressed, -1 let go. */
enum DsAction {
  DS_ACT_PAUSE = 0x352fb91, /* pause; the RIG where there is nothing to pause */
  DS_ACT_RIG,               /* the inventory */
  DS_ACT_WEAPON_PREV,
  DS_ACT_WEAPON_NEXT,
  DS_ACT_LOCATOR,
  DS_ACT_MELEE,
  DS_ACT_RELOAD,
  DS_ACT_AIM,
  DS_ACT_FIRE,        /* aiming: fire; else melee */
  DS_ACT_TURN_STASIS, /* aiming: stasis; else the quick turn */
  DS_ACT_INTERACT,    /* what is in front: a door, an item, kinesis (again: throw) */
  DS_ACT_JUMP = 0x352fb9f, /* zero gravity */
  DS_ACT_ALT_FIRE,         /* aiming: the weapon's other mode */
};
int ds_engine_init(void);  /* 1: the library is the build the addresses are for */
void *ds_engine_hud(void); /* the HUD while a level is played, else NULL */
void ds_engine_action(void *hud, int action, int param);

/* ds_java.c */
extern JObj *g_activity; /* com.ea.blast.MainActivity.instance */
void ds_java_init(void);

/* ds_assets.c: android.content.res.AssetManager on the APK's assets/ */
int ds_assets_init(void); /* entries indexed, or negative */
JNI_H_DECL(ds_h_asset_open);
JNI_H_DECL(ds_h_asset_open_fd);
JNI_H_DECL(ds_h_asset_list);
JNI_H_DECL(ds_h_asset_fd_length);
JNI_H_DECL(ds_h_stream_read);
JNI_H_DECL(ds_h_stream_skip);
JNI_H_DECL(ds_h_stream_close);

/* ds_game.c */
int ds_game_run(void);
void ds_game_request_exit(void); /* Activity.finish() */

/* ds_audio.c: android.media.AudioTrack, the engine's PCM through audout */
int ds_audio_init(void);
void ds_audio_start(void); /* AndroidEAAudioCore.Startup() */
void ds_audio_pause(int paused);
void ds_audio_shutdown(void);
uint32_t ds_audio_blocks(void);
JNI_H_DECL(ds_h_track_write);

/* ds_input.c */
void ds_input_init(void);
void ds_input_poll(int width, int height);
void ds_input_reset(void); /* focus lost: held keys and touches let go */

#endif /* DS_H */
