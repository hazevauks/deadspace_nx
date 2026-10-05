/* ds_gl.c -- the GLES 1 entry points the engine imports that Mesa lacks.
 *
 * libDeadSpace.so imports the whole GLES 1 API, OES_matrix_palette (skinning
 * on the GPU) included, which Mesa does not implement and does not list
 * among its extensions. An engine that honours the extension string never
 * calls these; each says so once if it does, instead of the fault an
 * unresolved import would be. MIT.
 */
#include "imports.h"
#include "util.h"

#define MISSING(name)                                                          \
  static void name(void) {                                                     \
    static int said;                                                           \
    if (!said++)                                                               \
      debugPrintf("[gl] " #name " called: OES_matrix_palette is not there\n"); \
  }
MISSING(glCurrentPaletteMatrixOES)
MISSING(glLoadPaletteFromModelViewMatrixOES)
MISSING(glMatrixIndexPointerOES)
MISSING(glWeightPointerOES)

const DynLibFunction port_imports[] = {
    {"glCurrentPaletteMatrixOES", (uintptr_t)glCurrentPaletteMatrixOES},
    {"glLoadPaletteFromModelViewMatrixOES", (uintptr_t)glLoadPaletteFromModelViewMatrixOES},
    {"glMatrixIndexPointerOES", (uintptr_t)glMatrixIndexPointerOES},
    {"glWeightPointerOES", (uintptr_t)glWeightPointerOES},
};
const int port_imports_count = sizeof port_imports / sizeof port_imports[0];
