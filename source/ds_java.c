/* ds_java.c -- the Java side of Dead Space, as the engine sees it.
 *
 * The game's Java (com.ea.blast: MainActivity, its GLSurfaceView AndroidView
 * and renderer, the keyboard and touch handlers; com.eamobile's
 * DeadSpaceActivity with the Amazon licence check and the asset downloader)
 * does not run here: ds_game.c, ds_input.c and ds_audio.c do what it did.
 * What the ENGINE calls back through JNI is answered from the tables below.
 * That is little: fourteen classes, every one listed here. A method without
 * a handler is answered with its type's zero and logged once (jni_core.c):
 * that log is the to-do list.
 *
 * The phone the engine is told about is a Sony Ericsson Xperia Play (R800i),
 * a phone of the game's own time. (This build has none of the Xperia Play's
 * gamepad support: the controller is ds_input.c's and ds_engine.c's work.)
 * MIT.
 */
#include <string.h>
#include <switch.h>

#include "config.h"
#include "dcr_path.h"
#include "ds.h"
#include "jni.h"
#include "rt_window.h"
#include "util.h"

#define MA "com/ea/blast/MainActivity"
#define SD "com/ea/blast/SystemAndroidDelegate"
#define DD "com/ea/blast/DisplayAndroidDelegate"
#define AM "android/content/res/AssetManager"
#define AT "android/media/AudioTrack"
#define IO "com/ea/EAIO/EAIO"
#define QY "com/eamobile/Query"
#define S "Ljava/lang/String;"

#define H(fn) static jvalue fn(JObj *self, const jvalue *a, const JMethod *m)

JObj *g_activity;

/* --------------------------------------------------------- MainActivity */
H(h_GetInstance) { return jv_l(jni_retain(g_activity)); }
H(h_getAssets) { return jv_l(jni_retain(jni_singleton(AM))); }
H(h_finish) {
  debugPrintf("[java] Activity.finish()\n");
  ds_game_request_exit();
  return jv_none();
}

/* EAIO.Startup(AssetManager) and Shutdown() are natives the engine calls
 * THROUGH Java (during NativeOnCreate): handed on to the library. */
H(h_io_startup) {
  debugPrintf("[java] EAIO.Startup(AssetManager)\n");
  g_n.IoStartup(g_jni_env, jni_class(IO)->obj, a[0].l);
  return jv_none();
}
H(h_io_shutdown) {
  if (g_n.IoShutdown)
    g_n.IoShutdown(g_jni_env, jni_class(IO)->obj);
  return jv_none();
}

/* ------------------------------------------------- SystemAndroidDelegate */
/* What the phone is: every answer is a string, counts as digits. Nothing
 * here is sent anywhere (the game believes it has no network). */
#define STR(fn, text) \
  H(fn) { return jv_l(jni_str(text)); }
STR(h_one, "1")
STR(h_none, "0")
STR(h_false, "false")
STR(h_unknown, "-1")
STR(h_model, "R800i") /* Build.MODEL and Build.DEVICE: the Xperia Play */
STR(h_manufacturer, "Sony Ericsson")
STR(h_platform, "Android")
STR(h_release, "2.3.4")
STR(h_api, "10")
STR(h_abi, "armeabi-v7a armeabi")
STR(h_language, "en") /* Locale.getDefault() */
STR(h_locale, "en_US")

/* ------------------------------------------------ DisplayAndroidDelegate */
/* A screen that is landscape as it stands: the rendering size, unrotated. */
H(h_width) {
  int w, h;
  dcr_window_size(&w, &h);
  return jv_i(w);
}
H(h_height) {
  int w, h;
  dcr_window_size(&w, &h);
  return jv_i(h);
}
H(h_orientation) { return jv_i(g_ids.orientation_normal); }
/* DisplayMetrics.xdpi / ydpi: the Xperia Play's 4" 854x480 */
H(h_dpi) { return jv_f(245.0f); }

/* -------------------------------------------- GetAppDataDirectoryDelegate */
/* getFilesDir() and Environment.getExternalStorageDirectory(): Android's
 * own paths, which the runtime turns into the game folder's (dcr_path.c). */
STR(h_files_dir, DCR_ANDROID_FILES)
STR(h_external_dir, "/sdcard")

/* ----------------------------------------------------------------- Query */
/* ActivityManager.MemoryInfo.availMem, in bytes */
H(h_avail_mem) { return jv_j(512ll << 20); }
STR(h_version, "1.2.0")

const JMethodDef jni_method_defs[] = {
    {MA, "GetInstance", "()L" MA ";", h_GetInstance},
    {MA, "getAssets", "()L" AM ";", h_getAssets},
    {MA, "finish", "()V", h_finish},
    {IO, "Startup", "(L" AM ";)V", h_io_startup},
    {IO, "Shutdown", "()V", h_io_shutdown},
    /* the game's files (ds_assets.c) */
    {AM, "open", "(" S ")Ljava/io/InputStream;", ds_h_asset_open},
    {AM, "openFd", "(" S ")Landroid/content/res/AssetFileDescriptor;", ds_h_asset_open_fd},
    {AM, "list", "(" S ")[" S, ds_h_asset_list},
    {"android/content/res/AssetFileDescriptor", "getLength", "()J", ds_h_asset_fd_length},
    {"java/io/InputStream", "read", "([BII)I", ds_h_stream_read},
    {"java/io/InputStream", "skip", "(J)J", ds_h_stream_skip},
    {"java/io/InputStream", "close", "()V", ds_h_stream_close},
    /* the phone */
    {SD, "<init>", NULL, jni_h_void},
    {SD, "GetAccelerometerCount", "()" S, h_one},
    {SD, "GetTouchScreenCount", "()" S, h_one},
    {SD, "GetTouchPadCount", "()" S, h_one},
    {SD, "GetPhysicalKeyboardCount", "()" S, h_one},
    {SD, "GetVirtualKeyboardCount", "()" S, h_one},
    {SD, "GetDisplayCount", "()" S, h_one},
    {SD, "GetCameraCount", "()" S, h_none},
    {SD, "GetCompassCount", "()" S, h_none},
    {SD, "GetGyroscopeCount", "()" S, h_none},
    {SD, "GetMicrophoneCount", "()" S, h_none},
    {SD, "GetTrackBallCount", "()" S, h_none},
    {SD, "GetVibratorCount", "()" S, h_none},
    {SD, "IsBatteryStateAvailable", "()" S, h_false},
    {SD, "GetLocationAvailable", "()" S, h_false},
    {SD, "GetTotalRAM", "()" S, h_unknown}, /* the Java's own answer */
    {SD, "GetDeviceUniqueId", "()" S, h_unknown},
    {SD, "GetDeviceModel", "()" S, h_model},
    {SD, "GetDeviceName", "()" S, h_model},
    {SD, "GetManufacturer", "()" S, h_manufacturer},
    {SD, "GetPlatformRawName", "()" S, h_platform},
    {SD, "GetPlatformStdName", "()" S, h_platform},
    {SD, "GetPlatformVersion", "()" S, h_release},
    {SD, "GetApiLevel", "()" S, h_api},
    {SD, "GetProcessorArchitecture", "()" S, h_abi},
    {SD, "GetLanguage", "()" S, h_language},
    {SD, "GetLocale", "()" S, h_locale},
    {SD, "GetChipset", "()" S, jni_h_empty_string},
    {SD, "GetPhoneNumber", "()" S, jni_h_empty_string},
    {SD, "GetDeviceSubscriberID", "()" S, jni_h_empty_string},
    {SD, "IntentView", "(" S ")Z", jni_h_false}, /* a web page: there is no browser */
    /* the screen */
    {DD, "<init>", NULL, jni_h_void},
    {DD, "GetDefaultWidth", "()I", h_width},
    {DD, "GetDefaultHeight", "()I", h_height},
    {DD, "GetStdOrientation", "()I", h_orientation},
    {DD, "SetStdOrientation", "(I)V", jni_h_void},
    {DD, "GetDpiX", "()F", h_dpi},
    {DD, "GetDpiY", "()F", h_dpi},
    {"com/ea/blast/TouchSurfaceAndroid", "IsTouchScreenMultiTouch", "()Z", jni_h_true},
    {"com/ea/blast/GetAppDataDirectoryDelegate", "<init>", NULL, jni_h_void},
    {"com/ea/blast/GetAppDataDirectoryDelegate", "GetAppDataDirectory", "()" S, h_files_dir},
    {"com/ea/blast/GetAppDataDirectoryDelegate", "GetExternalStorageDirectory", "()" S, h_external_dir},
    /* sensors, the wake lock: nothing to do (no motion is sent yet) */
    {"com/ea/blast/AccelerometerAndroidDelegate", NULL, NULL, jni_h_void},
    {"com/ea/blast/DeviceOrientationHandlerAndroidDelegate", NULL, NULL, jni_h_void},
    {"com/ea/blast/PowerManagerAndroid", NULL, NULL, jni_h_void},
    /* com.eamobile.Query: every frame the engine asks isContentReady() and
     * starts the game only once it is true (on the phone: after the licence
     * check and the asset download); saveImage is a screenshot to the gallery */
    {QY, "isContentReady", "()Z", jni_h_true},
    {QY, "getTotalMemory", "()J", h_avail_mem},
    {QY, "getVersion", "()" S, h_version},
    {QY, "saveImage", NULL, jni_h_void},
    /* the engine's sound output (ds_audio.c) */
    {AT, "write", "([SII)I", ds_h_track_write},
    {AT, "write", "([BII)I", ds_h_track_write},
    {AT, "play", "()V", jni_h_void},
    {AT, "pause", "()V", jni_h_void},
    {AT, "stop", "()V", jni_h_void},
    {AT, "flush", "()V", jni_h_void},
    {AT, "release", "()V", jni_h_void},
    {NULL, NULL, NULL, NULL},
};

const JFieldDef jni_field_defs[] = {
    {NULL, NULL, NULL, 0, NULL},
};

const char *const jni_class_supers[][2] = {
    {"com/eamobile/deadspace_full_azn/DeadSpaceActivity", MA},
    {MA, "android/app/Activity"},
    {"android/app/Activity", "android/content/Context"},
    {NULL, NULL},
};

/* Classes the engine probes for and must not find (when classes.txt is
 * absent): none known. */
const char *const jni_missing_classes[] = {
    NULL,
};

/* -------------------------------------------------------------- monitors */
/* JNIEnv's MonitorEnter and MonitorExit, which the runtime answers without
 * locking anything. The engine needs them real: both of its
 * AssetManagerJNI::Read (EA::IO's and rw::core::filesys's, its only uses of
 * a monitor) read every file through one shared byte[] of 64 KB, held by
 * its monitor from InputStream.read() to GetByteArrayRegion(). Without the
 * lock two loading threads read each other's bytes: a model that loads as
 * nothing, a size of a gigabyte, a fault somewhere in the loader at
 * start-up, on some starts and not on others.
 * One lock for each object locked, found by its address; they are few and
 * are never let go. */
#define JNI_MONITOR_ENTER 217 /* JNINativeInterface's slots */
#define JNI_MONITOR_EXIT 218
#define MAX_MONITORS 16

static struct {
  const void *obj;
  RMutex lock;
} g_monitors[MAX_MONITORS];
static int g_nmonitors;
static Mutex g_monitors_lock;

static RMutex *monitor_of(const void *obj) {
  mutexLock(&g_monitors_lock);
  int i = 0;
  while (i < g_nmonitors && g_monitors[i].obj != obj)
    i++;
  if (i == MAX_MONITORS) {
    i--; /* more objects than locks: the last one is shared */
  } else if (i == g_nmonitors) {
    g_monitors[i].obj = obj;
    rmutexInit(&g_monitors[i].lock);
    g_nmonitors++;
    debugPrintf("[java] a monitor for %p\n", obj);
  }
  mutexUnlock(&g_monitors_lock);
  return &g_monitors[i].lock;
}

static int monitor_enter(void *env, void *obj) {
  rmutexLock(monitor_of(obj));
  return 0;
}

static int monitor_exit(void *env, void *obj) {
  rmutexUnlock(monitor_of(obj));
  return 0;
}

void ds_java_init(void) {
  jni_init();
  void **env = *(void ***)g_jni_env; /* the function table */
  env[JNI_MONITOR_ENTER] = (void *)monitor_enter;
  env[JNI_MONITOR_EXIT] = (void *)monitor_exit;
  g_activity = jni_singleton("com/eamobile/deadspace_full_azn/DeadSpaceActivity");
  debugPrintf("[java] DeadSpaceActivity %p\n", (void *)g_activity);
}
