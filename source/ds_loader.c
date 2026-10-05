/* ds_loader.c -- loading Dead Space's one module, libDeadSpace.so.
 *
 * EA's EAMCore ("Blast") framework with the game, EASTL and the rest linked
 * in: ARMv5TE code, soft-float, GLES 1.1. Its DT_NEEDED are system libraries
 * only (libc, libstdc++, libm, liblog, libGLESv1_CM), all served by the
 * shims: 398 imports, 190 gl* through the GL layer (the whole GLES 1 API:
 * what the engine really calls is far less), the rest from the import table
 * (runtime/tools/gen_imports.py). Nothing in it writes code at run time, so
 * the module is mapped the plain way: staged, relocated, resolved, then
 * sealed as code (RX text, RW data) before anything in it runs. Its init
 * array has 208 constructors.
 *
 * Its natives are all exported by name (Java_com_ea_...). The library also
 * keeps its .symtab (20 000 names), which tells what any address in a crash
 * log is. MIT.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "config.h"
#include "dcr_path.h"
#include "ds.h"
#include "error.h"
#include "imports.h"
#include "so_util.h"
#include "util.h"

so_module g_mod_game;
DsNatives g_n;
DsIds g_ids;

/* ---------------------------------------------------------------- natives */
#define BLAST "Java_com_ea_blast_"

static const struct {
  const char *sym;
  size_t off;
  int required;
} k_natives[] = {
#define NAT(field, sym, req) {sym, offsetof(DsNatives, field), req}
    NAT(OnCreate, BLAST "MainActivity_NativeOnCreate", 1),
    NAT(OnPause, BLAST "MainActivity_NativeOnPause", 1),
    NAT(OnStop, BLAST "MainActivity_NativeOnStop", 0),
    NAT(OnWindowFocusChanged, BLAST "MainActivity_NativeOnWindowFocusChanged", 1),
    NAT(OnSurfaceChanged, BLAST "AndroidRenderer_NativeOnSurfaceChanged", 1),
    NAT(OnDrawFrame, BLAST "AndroidRenderer_NativeOnDrawFrame", 1),
    NAT(OnKeyDown, BLAST "KeyboardAndroid_NativeOnKeyDown", 1),
    NAT(OnKeyUp, BLAST "KeyboardAndroid_NativeOnKeyUp", 1),
    NAT(OnVisibilityChanged, BLAST "KeyboardAndroid_NativeOnVisibilityChanged", 0),
    NAT(OnPointerEvent, BLAST "TouchSurfaceAndroid_NativeOnPointerEvent", 1),
    NAT(OnAcceleration, BLAST "AccelerometerAndroidDelegate_NativeOnAcceleration", 0),
    NAT(IoStartup, "Java_com_ea_EAIO_EAIO_Startup", 1),
    NAT(IoShutdown, "Java_com_ea_EAIO_EAIO_Shutdown", 0),
    NAT(AudioInit, "Java_com_ea_EAAudioCore_AndroidEAAudioCore_Init", 0),
    NAT(AudioRelease, "Java_com_ea_EAAudioCore_AndroidEAAudioCore_Release", 0),
#undef NAT
};

static int bind_natives(void) {
  int missing = 0;
  for (unsigned i = 0; i < sizeof k_natives / sizeof k_natives[0]; i++) {
    const uintptr_t a = so_try_find_addr_rx(&g_mod_game, k_natives[i].sym);
    memcpy((uint8_t *)&g_n + k_natives[i].off, &a, sizeof a); /* a function pointer's slot */
    if (!a) {
      debugPrintf("[boot] %s native %s%s\n", k_natives[i].required ? "MISSING" : "no", k_natives[i].sym,
                  k_natives[i].required ? "" : " (optional)");
      missing += k_natives[i].required;
    }
  }
  return missing;
}

/* The Java's static initializers ask the engine for these numbers
 * (ModuleCatalog.<clinit> and the like): each native returns a constant. */
static jint constant(const char *sym, jint fallback) {
  jint (*fn)(void *env, void *cls) = (jint (*)(void *, void *))so_try_find_addr_rx(&g_mod_game, sym);
  if (!fn) {
    debugPrintf("[boot] no %s: %d assumed\n", sym, (int)fallback);
    return fallback;
  }
  return fn(g_jni_env, NULL);
}

static void read_ids(void) {
  g_ids.keyboard = constant(BLAST "ModuleCatalog_NativeGetModuleTypeIdPhysicalKeyboard", 600);
  g_ids.touch_screen = constant(BLAST "ModuleCatalog_NativeGetModuleTypeIdTouchScreen", 1000);
  g_ids.touch_pad = constant(BLAST "ModuleCatalog_NativeGetModuleTypeIdTouchPad", 1100);
  g_ids.pointer_down = constant(BLAST "TouchSurfaceAndroid_NativeGetIdRawPointerDown", 0x6000c);
  g_ids.pointer_move = constant(BLAST "TouchSurfaceAndroid_NativeGetIdRawPointerMove", 0x4000c);
  g_ids.pointer_up = constant(BLAST "TouchSurfaceAndroid_NativeGetIdRawPointerUp", 0x8000c);
  g_ids.orientation_normal = constant(BLAST "DisplayAndroidDelegate_NativeGetOrientationNormal", 0);
  debugPrintf("[boot] modules: keyboard %d, touch screen %d, touch pad %d; pointer down 0x%x, "
              "move 0x%x, up 0x%x; orientation %d\n",
              (int)g_ids.keyboard, (int)g_ids.touch_screen, (int)g_ids.touch_pad,
              (unsigned)g_ids.pointer_down, (unsigned)g_ids.pointer_move, (unsigned)g_ids.pointer_up,
              (int)g_ids.orientation_normal);
}

/* -------------------------------------------------------------- loading */
int ds_load_engine(void) {
  char path[512];
  snprintf(path, sizeof path, "%s/%s", dcr_game_root(), DS_LIB);
  int rc = so_load(&g_mod_game, path, NULL, PORT_SO_REGION_BYTES);
  if (rc < 0) {
    const char *why = rc == -1 ? "cannot open it, or it is not a 32-bit ARM ELF"
                    : rc == -2 ? "out of memory"
                    : rc == -3 ? "larger than PORT_SO_REGION_BYTES"
                    : rc == -4 ? "too many program headers" : "?";
    debugPrintf("[boot] so_load(%s) failed rc=%d: %s\n", path, rc, why);
    return -1;
  }
  so_relocate(&g_mod_game);
  int missing = so_resolve(&g_mod_game, dcr_imports, dcr_imports_count, 1);
  debugPrintf("[boot] %s %u KB  staged %p -> %p  (%d unresolved imports)\n", g_mod_game.base_name,
              (unsigned)(g_mod_game.load_size >> 10), g_mod_game.load_base, g_mod_game.load_virtbase,
              missing);
  /* libgcc's __sync_* on ARM Linux call the kernel's user helpers through
   * literal pools: any such literal is pointed at the runtime's kuser.S. */
  so_fix_kuser_helpers(&g_mod_game);
  so_finalize(&g_mod_game);
  so_flush_caches(&g_mod_game);
  return 0;
}

/* Android runs a library's constructors inside System.loadLibrary, which
 * DeadSpaceActivity's static initializer calls; JNI_OnLoad follows, where
 * the engine keeps the VM. */
void ds_run_constructors(void) {
  const u64 t0 = armGetSystemTick();
  so_execute_init_array(&g_mod_game);
  debugPrintf("[boot] %s constructors done in %llu ms\n", DS_LIB,
              (unsigned long long)(armTicksToNs(armGetSystemTick() - t0) / 1000000ull));
  typedef jint (*fn_onload)(void *vm, void *reserved);
  fn_onload onload = (fn_onload)so_try_find_addr_rx(&g_mod_game, "JNI_OnLoad");
  if (onload)
    debugPrintf("[boot] JNI_OnLoad -> 0x%lx\n", (unsigned long)onload(g_jni_vm, NULL));
  if (bind_natives())
    fatal_error(DS_LIB " is not the Dead Space engine this port knows\n"
                "(natives are missing: see debug.log). The port is made for\n" PORT_APK_DESC ".");
  read_ids();
}
