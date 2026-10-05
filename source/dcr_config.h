/* dcr_config.h -- the user's settings, from <game folder>/config.ini (dcr_config.c). */
#ifndef DCR_USER_CONFIG_H
#define DCR_USER_CONFIG_H

typedef struct {
  int swap_ab;      /* [controls] swap_a_b */
  int touch;        /* [touch] enabled */
  int res_w, res_h; /* [display] resolution */
  int boost;        /* [performance] boost_cpu_when_loading */
  int gl_selftest;  /* [debug] gl_selftest */
  int boot_log;     /* [debug] boot_log_on_screen */
  int log_jni;      /* [debug] log_java_calls */
  int log_input;    /* [debug] log_input */
  float look;       /* [controls] look_sensitivity */
  int gyro;         /* [motion] enabled */
  int gyro_aim_only;                /* [motion] only_while_aiming */
  float gyro_sensitivity;           /* [motion] sensitivity */
  int gyro_invert_x, gyro_invert_y; /* [motion] invert_horizontal, invert_vertical */
} DcrConfig;

/* Read config.ini (writing it with the defaults, or adding missing options,
 * first). Early in main(); the defaults hold until then. */
void dcr_config_load(void);
const DcrConfig *dcr_config(void);
/* [motion] enabled, changed while playing (the left stick's click): written
 * to config.ini for the next start. */
void dcr_config_set_gyro(int on);

#endif
