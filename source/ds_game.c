/* ds_game.c -- plays the part of the game's Java: DeadSpaceActivity (a
 * com.ea.blast.MainActivity), its GLSurfaceView (AndroidView) and renderer.
 *
 * What the Java does, and what this file does for it:
 *
 *   MainActivity.onCreate     NativeOnCreate(): the engine starts (it asks
 *                             Java for the phone, the screen, and its files:
 *                             ds_java.c, ds_assets.c)
 *   DeadSpaceActivity         AndroidEAAudioCore.Startup() (ds_audio.c); the
 *                             Amazon licence check and the asset downloader,
 *                             neither of which exists here
 *   AndroidView               an EGL context: OpenGL ES 1, RGB, depth (here:
 *                             libnx's default window, through the shared
 *                             EGL layer, gl_mesa.c)
 *   AndroidRenderer           on the GL thread: NativeOnSurfaceChanged(width,
 *                             height), then NativeOnDrawFrame() for every
 *                             frame; the first one starts the engine
 *   onWindowFocusChanged      NativeOnWindowFocusChanged(focus): after the
 *                             first frame; here for HOME and sleep too (see
 *                             "lifecycle" below)
 *   Activity.onPause, onStop  NativeOnPause(), NativeOnStop(): here when the
 *                             game closes
 *
 * On Android the UI thread (input, lifecycle) and the GL thread (frames) run
 * side by side; here one thread does both, in the order a frame sees them:
 * input, frame, present. MIT.
 */
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "config.h"
#include "dcr_boost.h"
#include "dcr_config.h"
#include "ds.h"
#include "error.h"
#include "gl_layer.h"
#include "jni.h"
#include "rt_applet.h"
#include "rt_window.h"
#include "util.h"
#include "watchdog.h"

#define ENV g_jni_env
#define SELF g_activity

/* The shared EGL layer (gl_mesa.c / gl_null.c), as plain C: both define
 * these with the same register-level signatures. */
typedef int32_t fEGLint;
void *b_eglGetDisplay(void *native);
unsigned b_eglInitialize(void *d, fEGLint *maj, fEGLint *min);
unsigned b_eglChooseConfig(void *d, const fEGLint *attrs, void **cfgs, fEGLint cap, fEGLint *num);
void *b_eglCreateWindowSurface(void *d, void *cfg, void *win, const fEGLint *attrs);
void *b_eglCreateContext(void *d, void *cfg, void *share, const fEGLint *attrs);
unsigned b_eglMakeCurrent(void *d, void *draw, void *read, void *ctx);
unsigned b_eglSwapInterval(void *d, fEGLint interval);
unsigned b_eglSwapBuffers(void *d, void *s);
unsigned b_eglDestroySurface(void *d, void *s);
unsigned b_eglDestroyContext(void *d, void *c);
unsigned b_eglTerminate(void *d);
fEGLint b_eglGetError(void);

#define EGL_NONE 0x3038
#define EGL_RED_SIZE 0x3024
#define EGL_GREEN_SIZE 0x3023
#define EGL_BLUE_SIZE 0x3022
#define EGL_DEPTH_SIZE 0x3025
#define EGL_STENCIL_SIZE 0x3026
#define EGL_SURFACE_TYPE 0x3033
#define EGL_WINDOW_BIT 0x0004
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_OPENGL_ES_BIT 0x0001
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_OPENGL_ES_API 0x30A0

static volatile int g_exit;
static int g_engine_up; /* the surface is made: the lifecycle natives may be called */
static int g_w, g_h;
static void *g_dpy, *g_surf, *g_ctx;

/* For the watchdog: frames presented. */
uint64_t dcr_boot_frames(void) { return dcr_gl_frames(); }

void ds_game_request_exit(void) { g_exit = 1; }

/* --------------------------------------------------------- lifecycle */
/* The runtime's applet lifecycle (rt_applet.c) calls these from the frame
 * loop's rt_applet_poll().
 *
 * Activity.onPause / onResume is Android's heavy way of taking a game off
 * the screen: the GLSurfaceView loses its GL context, and the engine loads
 * everything again. HOME and sleep on the Switch never lose the context, so
 * they are the light way, onWindowFocusChanged: held keys and touches let
 * go, the engine told (it pauses the game), the sound held. */
void port_focus_lost(void) {
  if (!g_engine_up)
    return;
  ds_input_reset();
  g_n.OnWindowFocusChanged(ENV, SELF, 0);
  ds_audio_pause(1);
}

void port_focus_gained(void) {
  if (!g_engine_up)
    return;
  ds_audio_pause(0);
  g_n.OnWindowFocusChanged(ENV, SELF, 1);
}

/* HOME and sleep freeze the whole process; the runtime's clocks find each
 * freeze (whether or not focus messages came). What Android does around it:
 * the focus lost, then back. */
void port_process_frozen(unsigned count) {
  debugPrintf("[game] the process was held (HOME menu or sleep; freeze %u)\n", count);
  port_focus_lost();
  port_focus_gained();
}

/* ---------------------------------------------------------------- EGL */
static void egl_up(void) {
  dcr_window_size(&g_w, &g_h);
  g_dpy = b_eglGetDisplay(NULL);
  fEGLint maj = 0, min = 0, n = 0;
  if (!g_dpy || !b_eglInitialize(g_dpy, &maj, &min))
    fatal_error("The graphics driver did not start (eglInitialize 0x%x).", (unsigned)b_eglGetError());
  unsigned (*bind_api)(unsigned) = (unsigned (*)(unsigned))dcr_gl_lookup("eglBindAPI");
  if (bind_api)
    bind_api(EGL_OPENGL_ES_API);
  /* AndroidView's chooser asks for ES 1 with depth; the stencil is for the
   * engine's GL_STENCIL_TEST, should it use one */
  static const fEGLint cfg_attrs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES_BIT, EGL_SURFACE_TYPE,
                                      EGL_WINDOW_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                                      EGL_BLUE_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
                                      EGL_NONE};
  void *cfg = NULL;
  if (!b_eglChooseConfig(g_dpy, cfg_attrs, &cfg, 1, &n) || n < 1)
    fatal_error("No OpenGL ES 1 window configuration (0x%x).", (unsigned)b_eglGetError());
  g_surf = b_eglCreateWindowSurface(g_dpy, cfg, nwindowGetDefault(), NULL);
  static const fEGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 1, EGL_NONE};
  g_ctx = b_eglCreateContext(g_dpy, cfg, NULL, ctx_attrs);
  if (!g_surf || !g_ctx || !b_eglMakeCurrent(g_dpy, g_surf, g_surf, g_ctx))
    fatal_error("Could not create the OpenGL ES 1 context (surface %p, context %p, 0x%x).", g_surf,
                g_ctx, (unsigned)b_eglGetError());
  b_eglSwapInterval(g_dpy, 1);
  debugPrintf("[game] EGL %d.%d: OpenGL ES 1 on the window, %dx%d\n", (int)maj, (int)min, g_w, g_h);
  const char *(*get_string)(unsigned) = (const char *(*)(unsigned))dcr_gl_lookup("glGetString");
  if (get_string) {
    debugPrintf("[game] GL %s, %s\n", get_string(0x1F02 /* GL_VERSION */), get_string(0x1F01 /* GL_RENDERER */));
    debugPrintf("[game] GL extensions: %s\n", get_string(0x1F03 /* GL_EXTENSIONS */));
  }
}

static void egl_down(void) {
  if (!g_dpy)
    return;
  b_eglMakeCurrent(g_dpy, NULL, NULL, NULL);
  if (g_ctx)
    b_eglDestroyContext(g_dpy, g_ctx);
  if (g_surf)
    b_eglDestroySurface(g_dpy, g_surf);
  b_eglTerminate(g_dpy);
  g_dpy = g_surf = g_ctx = NULL;
}

/* ---------------------------------------------------------------- report */
static void report(void) {
  static u64 last_tick;
  static unsigned long last_frames;
  const u64 tick = armGetSystemTick();
  const unsigned long frames = (unsigned long)dcr_gl_frames();
  const double fps =
      last_tick ? (double)(frames - last_frames) * 1e9 / (double)armTicksToNs(tick - last_tick) : 0.0;
  last_tick = tick;
  last_frames = frames;
  debugPrintf("[game] %lu frames (%.1f fps), %u audio blocks, %d Java objects\n", frames, fps,
              (unsigned)ds_audio_blocks(), jni_live_objects());
  dcr_boost_report();
}

/* ----------------------------------------------------------------- run */
int ds_game_run(void) {
  egl_up();
  ds_audio_init();
  dcr_watchdog_start();
  rt_watchdog_add_counter("audio blocks", ds_audio_blocks);

  /* ---- onCreate, the surface, the first frame ----
   * The engine's system starts inside the FIRST NativeOnDrawFrame
   * (EA::Blast::Loop: SystemAndroid::Init), and until then every lifecycle
   * native but NativeOnSurfaceChanged returns at once, so the focus is given
   * after that frame. The game itself starts in a later frame, once Java
   * answers Query.isContentReady() with true (ds_java.c). */
  debugPrintf("[game] MainActivity.NativeOnCreate\n");
  g_n.OnCreate(ENV, SELF);
  ds_audio_start();
  debugPrintf("[game] AndroidRenderer.NativeOnSurfaceChanged(%d, %d)\n", g_w, g_h);
  g_n.OnSurfaceChanged(ENV, SELF, g_w, g_h);
  debugPrintf("[game] the first NativeOnDrawFrame: the engine's system starts\n");
  g_n.OnDrawFrame(ENV, SELF);
  debugPrintf("[game] MainActivity.NativeOnWindowFocusChanged(true)\n");
  g_n.OnWindowFocusChanged(ENV, SELF, 1);
  ds_input_init();
  g_engine_up = 1;
  debugPrintf("[game] the engine is up\n");
  log_flush_ring();

  /* ---- AndroidRenderer.onDrawFrame, and the UI thread's input, in turn ---- */
  u64 last_report = armGetSystemTick();
  int first = 1;
  unsigned long quiet_at = 0;
  while (!g_exit && !rt_exit_requested() && appletMainLoop()) {
    rt_applet_poll(); /* focus, freezes: port_focus_lost/gained, port_process_frozen */
    if (!rt_focused()) {
      svcSleepThread(50000000ll);
      continue;
    }
    ds_input_poll(g_w, g_h);
    g_n.OnDrawFrame(ENV, SELF);
    b_eglSwapBuffers(g_dpy, g_surf);

    const u64 now = armGetSystemTick();
    const unsigned long frames = (unsigned long)dcr_gl_frames();
    if (first) {
      first = 0;
      dcr_boost_launch_end();
      debugPrintf("[game] first frame presented\n");
      quiet_at = frames + 180;
    }
    /* From ~3 s after the first picture the log goes to a RAM ring (util.c),
     * written out every 10 s and by the watchdog. */
    if (quiet_at && frames >= quiet_at) {
      quiet_at = 0;
      log_set_quiet(1);
    }
    if (armTicksToNs(now - last_report) >= 10000000000ull) {
      last_report = now;
      report();
      log_flush_ring();
    }
  }

  /* ---- onPause, onStop ---- */
  debugPrintf("[game] leaving (%s)\n", g_exit ? "the game closed itself" : "closed from the system");
  log_set_quiet(0);
  rt_applet_stop();
  if (rt_focused()) {
    g_n.OnPause(ENV, SELF);
    if (g_n.OnStop)
      g_n.OnStop(ENV, SELF);
  }
  ds_audio_shutdown();
  egl_down();
  debugPrintf("[game] closed\n");
  log_flush_ring();
  return 0;
}
