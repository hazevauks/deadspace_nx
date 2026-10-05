/* ds_assets.c -- android.content.res.AssetManager over the APK's assets/.
 *
 * The engine reads every file of the game through Java (EA::IO's and
 * rw::core::filesys's AssetManagerJNI, for "appbundle:/" paths):
 *
 *   AssetManager.open(name)    an InputStream; FileNotFoundException if absent
 *   AssetManager.openFd(name)  an AssetFileDescriptor, for getLength(); only
 *                              a stored entry has one (else the engine finds
 *                              the length by skipping to the end)
 *   AssetManager.list(dir)     the names in a folder
 *   InputStream.read(byte[], off, len), skip(n), close()
 *
 * Here the APK's central directory is indexed once (1731 entries under
 * assets/), and a stream is a position in an entry: the setup rewrote the
 * APK with its assets stored (ds_main.c), so a read is the APK's bytes at an
 * offset, through the runtime's block cache (dcr_apkcache.c). An entry that
 * is still deflated (the rewrite did not happen: no room on the card) is
 * inflated whole when opened. MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#ifndef MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#endif
#include <miniz/miniz.h>

#include "dcr_apkcache.h"
#include "dcr_path.h"
#include "ds.h"
#include "rt_apkfind.h"
#include "util.h"

#define PREFIX "assets/"
#define PREFIX_LEN 7

typedef struct {
  char *name; /* after "assets/" */
  uint32_t lho, size, comp_size;
  uint32_t data; /* where its bytes begin in the APK; 0 until first opened */
  uint8_t stored;
} Asset;

static Asset *g_assets;
static int g_nassets, g_cap;

/* An open InputStream: its entry's bytes in the APK, or inflated in RAM. */
typedef struct {
  uint32_t base, size, pos;
  uint8_t *mem;
} Stream;

/* ------------------------------------------------------------- the APK */
static Mutex g_file_lock;
static FILE *g_file;

/* 0 when all n bytes were read. */
static int apk_read(uint64_t off, void *buf, size_t n) {
  if (dcr_apkcache_read(off, buf, n) == (ssize_t)n)
    return 0;
  mutexLock(&g_file_lock);
  if (!g_file)
    g_file = fopen(dcr_apk_path(), "rb");
  const int ok = g_file && fseek(g_file, (long)off, SEEK_SET) == 0 && fread(buf, 1, n, g_file) == n;
  mutexUnlock(&g_file_lock);
  return ok ? 0 : -1;
}

/* An entry's data follows its local header, whose name and extra field may
 * differ in length from the central directory's. */
static uint32_t data_offset(Asset *a) {
  if (a->data)
    return a->data;
  uint8_t h[30];
  if (apk_read(a->lho, h, sizeof h) != 0 || memcmp(h, "PK\3\4", 4) != 0)
    return 0;
  a->data = a->lho + 30 + (uint32_t)(h[26] | h[27] << 8) + (uint32_t)(h[28] | h[29] << 8);
  return a->data;
}

/* ------------------------------------------------------------ the index */
static int add_entry(const RtZipEntry *e, void *ctx) {
  int *deflated = ctx;
  if (e->name_len <= PREFIX_LEN || e->name_len >= sizeof e->name || strncmp(e->name, PREFIX, PREFIX_LEN) != 0 ||
      e->name[e->name_len - 1] == '/')
    return 0;
  if (g_nassets == g_cap) {
    const int cap = g_cap ? g_cap * 2 : 2048;
    Asset *grown = realloc(g_assets, sizeof *grown * (size_t)cap);
    if (!grown)
      return 1;
    g_assets = grown;
    g_cap = cap;
  }
  Asset *a = &g_assets[g_nassets];
  memset(a, 0, sizeof *a);
  if (!(a->name = strdup(e->name + PREFIX_LEN)))
    return 1;
  a->lho = e->lho;
  a->size = e->size;
  a->comp_size = e->comp_size;
  a->stored = e->method == 0;
  *deflated += !a->stored;
  g_nassets++;
  return 0;
}

static int by_name(const void *a, const void *b) {
  return strcmp(((const Asset *)a)->name, ((const Asset *)b)->name);
}

int ds_assets_init(void) {
  int deflated = 0;
  mutexInit(&g_file_lock);
  if (rt_zip_walk_path(dcr_apk_path(), add_entry, &deflated) != 0 || !g_nassets) {
    debugPrintf("[assets] %s: no assets/ could be read\n", dcr_apk_path());
    return -1;
  }
  qsort(g_assets, (size_t)g_nassets, sizeof *g_assets, by_name);
  debugPrintf("[assets] %d files in the APK's assets/ (%d still deflated)\n", g_nassets, deflated);
  return g_nassets;
}

/* The engine's names are relative to assets/; a leading "/" or "assets/" is
 * tolerated. */
static const char *normalize(const char *name) {
  while (*name == '/')
    name++;
  if (!strncmp(name, PREFIX, PREFIX_LEN))
    name += PREFIX_LEN;
  return name;
}

static Asset *find(const char *name) {
  Asset key = {.name = (char *)normalize(name)};
  return bsearch(&key, g_assets, (size_t)g_nassets, sizeof *g_assets, by_name);
}

static void *not_found(const char *what, const char *name) {
  static int said;
  if (said < 64) {
    said++;
    debugPrintf("[assets] %s(%s): not in the APK\n", what, name);
  }
  jni_throw("java/io/FileNotFoundException", name);
  return NULL;
}

/* ------------------------------------------------------------- streams */
static void stream_free(JObj *self) {
  Stream *s = self->p;
  if (s) {
    free(s->mem);
    free(s);
    self->p = NULL;
  }
}

static uint8_t *inflate_entry(Asset *a, uint32_t data) {
  uint8_t *comp = malloc(a->comp_size ? a->comp_size : 1), *out = malloc(a->size ? a->size : 1);
  if (comp && out && apk_read(data, comp, a->comp_size) == 0 &&
      tinfl_decompress_mem_to_mem(out, a->size, comp, a->comp_size, 0) == a->size) {
    free(comp);
    return out;
  }
  free(comp);
  free(out);
  return NULL;
}

JNI_H_DECL(ds_h_asset_open) {
  const char *name = jni_utf(a[0].l);
  Asset *as = find(name);
  const uint32_t data = as ? data_offset(as) : 0;
  if (!data)
    return jv_l(not_found("open", name));
  Stream *s = calloc(1, sizeof *s);
  if (!s)
    return jv_l(NULL);
  s->base = data;
  s->size = as->size;
  if (!as->stored && !(s->mem = inflate_entry(as, data))) {
    debugPrintf("[assets] open(%s): could not inflate %u bytes\n", name, (unsigned)as->size);
    free(s);
    jni_throw("java/io/IOException", name);
    return jv_l(NULL);
  }
  JObj *o = jni_new("java/io/InputStream");
  o->p = s;
  o->finalize = stream_free;
  return jv_l(o);
}

JNI_H_DECL(ds_h_stream_read) {
  Stream *s = self ? self->p : NULL;
  JObj *arr = a[0].l;
  jint off = a[1].i, len = a[2].i;
  if (!s || !arr || arr->kind != JK_ARRAY || off < 0 || len < 0 || off + len > arr->a.len)
    return jv_i(-1);
  if (s->pos >= s->size)
    return jv_i(-1); /* the end of the stream */
  if ((uint32_t)len > s->size - s->pos)
    len = (jint)(s->size - s->pos);
  uint8_t *dst = (uint8_t *)arr->a.data + off;
  if (s->mem)
    memcpy(dst, s->mem + s->pos, (size_t)len);
  else if (apk_read((uint64_t)s->base + s->pos, dst, (size_t)len) != 0)
    return jv_i(-1);
  s->pos += (uint32_t)len;
  return jv_i(len);
}

JNI_H_DECL(ds_h_stream_skip) {
  Stream *s = self ? self->p : NULL;
  jlong n = a[0].j;
  if (!s || n <= 0)
    return jv_j(0);
  if (n > (jlong)(s->size - s->pos))
    n = (jlong)(s->size - s->pos);
  s->pos += (uint32_t)n;
  return jv_j(n);
}

JNI_H_DECL(ds_h_stream_close) {
  if (self)
    stream_free(self);
  return jv_none();
}

/* --------------------------------------------------- AssetFileDescriptor */
JNI_H_DECL(ds_h_asset_open_fd) {
  const char *name = jni_utf(a[0].l);
  Asset *as = find(name);
  if (!as || !as->stored) /* Android: a compressed asset has no descriptor */
    return jv_l(not_found("openFd", name));
  JObj *o = jni_new("android/content/res/AssetFileDescriptor");
  o->v[0] = (intptr_t)as->size;
  return jv_l(o);
}

JNI_H_DECL(ds_h_asset_fd_length) { return jv_j(self ? (jlong)(uint32_t)self->v[0] : 0); }

/* ------------------------------------------------------------------ list */
/* The names directly in a folder, files and folders alike. The index is
 * sorted, so a folder's entries are together and each child's are too. */
JNI_H_DECL(ds_h_asset_list) {
  char dir[512];
  snprintf(dir, sizeof dir, "%s", normalize(jni_utf(a[0].l)));
  size_t dl = strlen(dir);
  while (dl && dir[dl - 1] == '/')
    dir[--dl] = 0;
  JObj *arr = NULL;
  for (int pass = 0; pass < 2; pass++) { /* count, then fill */
    int n = 0;
    const char *last = NULL;
    size_t last_len = 0;
    for (int i = 0; i < g_nassets; i++) {
      const char *p = g_assets[i].name;
      if (dl) {
        if (strncmp(p, dir, dl) != 0 || p[dl] != '/')
          continue;
        p += dl + 1;
      }
      const size_t len = strcspn(p, "/");
      if (last && len == last_len && !strncmp(p, last, len))
        continue;
      last = p;
      last_len = len;
      if (pass) {
        char child[256];
        snprintf(child, sizeof child, "%.*s", (int)len, p);
        ((JObj **)arr->a.data)[n] = jni_str(child);
      }
      n++;
    }
    if (!pass)
      arr = jni_array('L', n);
  }
  return jv_l(arr);
}
