/* ds_gyro.c -- motion aiming: the controller's gyroscope turns the camera.
 *
 * The sensor of whatever is in the player's hands is read: the console's
 * own in handheld mode, a Pro Controller's, or the right Joy-Con's of a
 * pair. It gives how fast the controller turns about its own three axes (1.0
 * is a full turn a second) and, at rest, where gravity is. From those:
 *   up and down      the turn about the controller's x axis (left to right)
 *   left and right   the turn about the vertical, wherever that is in the
 *                    controller: the same motion works with the console
 *                    held upright or lying flat
 * The result goes the way the right stick's does, as pixels a finger would
 * have moved (ds_engine_look). MIT.
 */
#include <math.h>
#include <switch.h>

#include "dcr_config.h"
#include "ds.h"
#include "util.h"

/* A finger's pixels for one degree turned, at gyro_sensitivity 1. */
#define PIXELS_PER_DEGREE 12.0f
/* Slower than this (turns a second) is the hand's tremor, not aiming. */
#define STILL 0.004f

enum { H_HANDHELD, H_PRO, H_PAIR_LEFT, H_PAIR_RIGHT, HANDLES };
static HidSixAxisSensorHandle g_handle[HANDLES];
static int g_started;
static float g_up[3] = {0, 0, 1};  /* against gravity, in the controller's axes */

void ds_gyro_init(void) {
  Result rc = hidGetSixAxisSensorHandles(&g_handle[H_HANDHELD], 1, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld);
  Result rc2 = hidGetSixAxisSensorHandles(&g_handle[H_PRO], 1, HidNpadIdType_No1, HidNpadStyleTag_NpadFullKey);
  Result rc3 = hidGetSixAxisSensorHandles(&g_handle[H_PAIR_LEFT], 2, HidNpadIdType_No1, HidNpadStyleTag_NpadJoyDual);
  if (R_SUCCEEDED(rc) && R_SUCCEEDED(rc2) && R_SUCCEEDED(rc3)) {
    g_started = 1;
    for (int i = 0; i < HANDLES; i++)
      if (R_FAILED(rc = hidStartSixAxisSensor(g_handle[i])))
        g_started = 0;
  }
  debugPrintf("[gyro] the motion sensors %s (0x%x 0x%x 0x%x)\n", g_started ? "are on" : "did not start",
              (unsigned)rc, (unsigned)rc2, (unsigned)rc3);
}

/* The pixels to look by for dt seconds of the controller's motion; 0 when
 * there is no sensor to read. `handheld`: the console is in the hands, else
 * player 1's controller of this style. */
int ds_gyro_read(int handheld, u64 style, float dt, float *dx, float *dy) {
  if (!g_started)
    return 0;
  const int which = handheld ? H_HANDHELD : (style & HidNpadStyleTag_NpadJoyDual) ? H_PAIR_RIGHT : H_PRO;
  HidSixAxisSensorState s;
  if (hidGetSixAxisSensorStates(g_handle[which], &s, 1) < 1)
    return 0;
  /* gravity, followed slowly: while the controller turns it reads more than
   * gravity alone */
  const float ax = s.acceleration.x, ay = s.acceleration.y, az = s.acceleration.z;
  const float len = sqrtf(ax * ax + ay * ay + az * az);
  if (len > 0.7f && len < 1.3f) {
    g_up[0] += (-ax / len - g_up[0]) * 0.1f;
    g_up[1] += (-ay / len - g_up[1]) * 0.1f;
    g_up[2] += (-az / len - g_up[2]) * 0.1f;
  }
  const float wx = s.angular_velocity.x, wy = s.angular_velocity.y, wz = s.angular_velocity.z;
  if (fabsf(wx) + fabsf(wy) + fabsf(wz) < STILL) {
    *dx = *dy = 0;
    return 1;
  }
  const float yaw = wx * g_up[0] + wy * g_up[1] + wz * g_up[2]; /* about the vertical */
  const float k = 360.0f * PIXELS_PER_DEGREE * dcr_config()->gyro_sensitivity * dt;
  *dx = -yaw * k * (dcr_config()->gyro_invert_x ? -1.0f : 1.0f);
  *dy = -wx * k * (dcr_config()->gyro_invert_y ? -1.0f : 1.0f);
  return 1;
}
