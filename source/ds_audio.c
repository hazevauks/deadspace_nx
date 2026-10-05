/* ds_audio.c -- android.media.AudioTrack, played through audout.
 *
 * EAAudioCore mixes the game's sound itself, on a thread of its own, and
 * writes the result to a Java AudioTrack. The Java side
 * (AndroidEAAudioCore.Startup) is small:
 *
 *   rate  = AudioTrack.getNativeOutputSampleRate(STREAM_MUSIC)
 *   bytes = AudioTrack.getMinBufferSize(rate, STEREO, PCM 16)
 *   track = new AudioTrack(STREAM_MUSIC, rate, STEREO, PCM 16, bytes, STREAM)
 *   Init(track, bytes, 2, rate)            the native: starts the mixer
 *
 * after which the engine calls track.write(short[], offset, count), which
 * blocks until the track took the samples: that paces its thread.
 *
 * Here the "native rate" is audout's (48 kHz), so the engine mixes at the
 * device's rate and nothing is resampled; what it writes is cut into
 * audout's buffers of RT_AUDOUT_FRAMES stereo frames, and the blocking
 * submit paces the engine as AudioTrack.write did. MIT.
 */
#include <string.h>
#include <switch.h>

#include "ds.h"
#include "rt_audout.h"
#include "util.h"

#define TRACK "android/media/AudioTrack"
#define CORE "com/ea/EAAudioCore/AndroidEAAudioCore"
/* getMinBufferSize's answer: two of audout's buffers. */
#define TRACK_BYTES (RT_AUDOUT_FRAMES * 4 * 2)

static int g_ao_ready, g_started;
static volatile int g_paused;
static volatile uint32_t g_blocks;
static int16_t g_block[RT_AUDOUT_FRAMES * 2];
static int g_fill; /* samples (not frames) in g_block */

uint32_t ds_audio_blocks(void) { return g_blocks; }

int ds_audio_init(void) {
  if (rt_audout_open() != 0) {
    debugPrintf("[audio] audout did not open: no sound\n");
    return -1;
  }
  g_ao_ready = 1;
  debugPrintf("[audio] audout open at %u Hz, %d-frame buffers\n", rt_audout_rate(), RT_AUDOUT_FRAMES);
  return 0;
}

/* AndroidEAAudioCore.Startup(): the track handed to the engine. */
void ds_audio_start(void) {
  if (!g_n.AudioInit) {
    debugPrintf("[audio] no AndroidEAAudioCore.Init native: no sound\n");
    return;
  }
  const jint rate = (jint)rt_audout_rate();
  debugPrintf("[audio] AndroidEAAudioCore.Init(track, %d bytes, 2 channels, %d Hz)\n", TRACK_BYTES, (int)rate);
  g_n.AudioInit(g_jni_env, jni_class(CORE)->obj, jni_singleton(TRACK), TRACK_BYTES, 2, rate);
  g_started = 1;
}

/* HOME: nothing is queued while paused (the engine's thread keeps writing,
 * at the pace sound would have taken, so its clock does not run ahead). */
void ds_audio_pause(int paused) { g_paused = paused; }

void ds_audio_shutdown(void) {
  if (!g_ao_ready)
    return;
  rt_audout_cancel(1); /* the engine's audio thread may be inside a submit */
  if (g_started && g_n.AudioRelease)
    g_n.AudioRelease(g_jni_env, jni_class(CORE)->obj);
  rt_audout_close();
  g_ao_ready = 0;
  debugPrintf("[audio] closed after %u blocks\n", (unsigned)g_blocks);
}

/* Interleaved stereo s16 from the engine, into audout's buffers. */
static void write_samples(const int16_t *src, int count) {
  while (count > 0) {
    int n = (int)(sizeof g_block / sizeof g_block[0]) - g_fill;
    if (n > count)
      n = count;
    memcpy(g_block + g_fill, src, sizeof(int16_t) * (size_t)n);
    g_fill += n;
    src += n;
    count -= n;
    if (g_fill < (int)(sizeof g_block / sizeof g_block[0]))
      break;
    g_fill = 0;
    if (g_paused || !g_ao_ready) {
      /* one buffer's time, as if it had played */
      svcSleepThread((s64)RT_AUDOUT_FRAMES * 1000000000ll / (s64)rt_audout_rate());
      continue;
    }
    if (rt_audout_submit(g_block) == 0)
      g_blocks++;
  }
}

/* AudioTrack.write(short[] or byte[], offset, count): the count written. */
JNI_H_DECL(ds_h_track_write) {
  const JObj *arr = a[0].l;
  const jint off = a[1].i, count = a[2].i;
  if (!arr || arr->kind != JK_ARRAY || !arr->a.data || off < 0 || count < 0 || off + count > arr->a.len)
    return jv_i(-2); /* AudioTrack.ERROR_BAD_VALUE */
  if (arr->a.elem == 'S')
    write_samples((const int16_t *)arr->a.data + off, (int)count);
  else /* bytes: s16, little-endian */
    write_samples((const int16_t *)((const uint8_t *)arr->a.data + off), (int)count / 2);
  return jv_i(count);
}
