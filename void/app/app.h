/* SPDX-License-Identifier: MPL-2.0
 * VOID for the M-VAVE FM-1: the app's half of the contract with the platform (fm1-base: device/unity.c,
 * host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "VOID"
#define APP_SPLASH                       /* the boot screen is the app's: VOID on black */
#define APP_OVERLOAD                     /* the render ran late: fewer drone voices at once */
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#define APP_TOUCH                        /* velocity from the keys (lib/touch.h) */
#define APP_CANVAS_PX (240u * 110u)      /* the screen drawn in bands of up to 110 rows: the pool keeps the cloud */

/* the store: the state as it was left (OBJ_STATE), the panel's calibration (the platform's), the 8 scenes */
enum { OBJ_STATE, OBJ_PANEL, OBJ_SCENES, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDA000u}

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
void app_splash(void);
void app_overload(void);

#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
