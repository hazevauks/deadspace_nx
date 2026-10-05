/* port_config.h -- Dead Space's settings for the android32 runtime.
 *
 * Macros only: the runtime's C files, its assembly and the launcher all read
 * this (runtime/source/rt_settings.h). What each setting does is next to its
 * default in the runtime; runtime/docs/ lists them all. MIT.
 */
#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

/* ------------------------------------------------------------------ the game */
#define PORT_TITLE    "Dead Space"
#define PORT_NAME     "deadspace_nx"
#define PORT_PACKAGE  "com.eamobile.deadspace_full_azn"
#define PORT_BANNER   "deadspace_nx: Dead Space (EA's EAMCore engine, armeabi)"
#define PORT_ABI_DIR  "lib/armeabi/"
/* libDeadSpace.so maps 0x4e19e0 bytes (~5 MB): the runtime's 32 MB default
 * region holds it. */

/* The APK, by what is in it (any file name): it holds the engine and the
 * game's data; of several, the one at the version the port is made for. */
#define PORT_APK_DESC "Dead Space 1.2.0 (com.eamobile.deadspace_full_azn, armeabi)"
#define PORT_APK_ROLES                                                                     \
  {.what = "the game", .need = (const char *const[]){"lib/armeabi/libDeadSpace.so", NULL}, \
   .package = "com.eamobile.deadspace_full_azn", .version_code = 1200, .flags = RT_APK_PACKAGE_BONUS}
#define PORT_LAUNCHER_START_NOTE "(the first start prepares the game's data inside the APK)"

/* ------------------------------------------------------------------ sound */
/* The engine has no OpenSL ES: it mixes itself and writes PCM to a Java
 * AudioTrack, which ds_audio.c plays through audout. */

/* ------------------------------------------------------------------ frames */
/* A level loads inside one frame, and the frame loop cannot poll during it:
 * a watcher thread boosts the CPU for a frame that has run past 50 ms, until
 * it ends. */
#define RT_BOOST_WATCH_THREAD 1

/* ------------------------------------------------------------------ input */
#define RT_PAD_MAX_PLAYERS 1

#endif
