/* ds_main.c -- Dead Space's part of the boot: the runtime's main()
 * (runtime/source/main.c) does the rest -- the log, config.ini, the NRO
 * self-update, the APK found by what it holds, its package checked -- and
 * calls these.
 *
 * The first launch (runtime/source/dcr_setup.c, from the plan below):
 * the APK rewritten with its assets/ stored (most are deflated, and the
 * engine reads them through streams it skips about in: stored, a read is
 * the APK's bytes at an offset, ds_assets.c); libDeadSpace.so out of
 * lib/armeabi/; classes.txt (the Java class names, which jni_core.c answers
 * FindClass with). The bar, in permille of the first launch:
 *     0- 100  (the APK found and checked: the runtime's main())
 *   100- 700  the APK's assets stored (by bytes written)
 *   700- 880  libDeadSpace.so unpacked
 *   880- 950  the Java class list
 *        1000 the game starts
 * MIT.
 */
#include "config.h"
#include "dcr_path.h"
#include "dcr_setup.h"
#include "ds.h"
#include "error.h"
#include "rt_boot.h"
#include "util.h"

static const char *const k_libs[] = {DS_LIB};

const RtSetupPlan port_setup_plan = {
    .libs = k_libs,
    .nlibs = 1,
    .libs_what = "Unpacking the game's engine",
    .apk_requirement = "This port needs Dead Space (com.eamobile.deadspace_full_azn)\n"
                       "for 32-bit ARM (armeabi): use the APK of your own copy.",
    .libs_p0 = 700,
    .libs_p1 = 880,
    .classes_p0 = 880,
    .classes_p1 = 950,
    .store_apk_prefix = "assets/",
    .store_p0 = 100,
    .store_p1 = 700,
};

/* From the APK to the game's first code: the setup above (again when the APK
 * changed), the assets indexed, then the engine loaded, relocated, resolved
 * against the shims and mapped as code. */
int port_load(const char *apk) {
  dcr_setup_from_apk(apk);
  if (ds_assets_init() < 0)
    fatal_error("Could not read the game's data from\n%s.\n\n%s", apk, port_apk_help());
  if (ds_load_engine() != 0)
    fatal_error("Could not load the game engine from %s/" DS_LIB ".\n\n"
                "It is unpacked from the APK (lib/armeabi/) on launch: delete\n" DS_LIB
                " and .setup there to unpack it again. See debug.log.",
                dcr_game_root());
  return 0;
}

/* System.loadLibrary("DeadSpace"): the library's constructors and
 * JNI_OnLoad; then DeadSpaceActivity and its GLSurfaceView. */
void port_run(void) {
  ds_java_init(); /* JNI_OnLoad needs the VM */
  ds_run_constructors();
  ds_game_run();
}

/* For the error screens. */
const char *port_apk_help(void) {
  return "Copy the APK of your own Dead Space (com.eamobile.deadspace_full_azn,\n"
         "armeabi) into /switch/" PORT_NAME ". Any file name ending in .apk\n"
         "works: the game's code and data are read from it.";
}
