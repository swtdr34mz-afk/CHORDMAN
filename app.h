/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (CHORDMAN) */
/* CHORDMAN by swtdr34mz: a synth performance firmware for the M-VAVE FM-1 (on fm1-base). Four analog-style engines,
 * the Roland J-6's 100 chord sets on the lower twelve keys, a melody that follows the chord on the upper fifteen,
 * the J-6's grooves, a chord sequencer, and MIDI clock and transport in (from a computer, or from another FM-1 with
 * CHORDMAN as its USB host).
 * This header is the app's half of the contract with the platform (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "CHORDMAN"
#define APP_SPLASH_RGB RGB(255, 170, 40)
#define APP_BLOCK 128u                   /* frames per audio block: 2.9 ms at 44.1 kHz */
#define APP_TOUCH                        /* velocity from the keys (lib/touch.h) */

/* the store: the app's settings (the sounds as edited, the chord set, the groove, the sequence, the tempo), the panel
 * calibration (the platform's) */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}

/* backup and restore over USB: the settings */
#define APP_BACKUP_OBJS {OBJ_SETTINGS}
#define APP_BACKUP_MAGIC 0x4E4D4843u            /* "CHMN" */
#define APP_BACKUP_VERSION 1u
#define APP_BACKUP_WHAT "replaces sounds, sequence, settings"
int app_backup_playing(void);
void app_backup_flush(void);
void app_backup_hold(int on);
void app_backup_restored(void);

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
