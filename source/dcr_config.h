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
} DcrConfig;

/* Read config.ini (writing it with the defaults, or adding missing options,
 * first). Early in main(); the defaults hold until then. */
void dcr_config_load(void);
const DcrConfig *dcr_config(void);

#endif
