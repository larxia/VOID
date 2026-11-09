/* SPDX-License-Identifier: MPL-2.0
 * VOID's drums: six synthesized voices, four kits (engine.h's P_DMKIT; each voice's TUNE, DECAY, VOLUME in the
 * pattern). In the audio interrupt only. */
#pragma once
#include <stdint.h>
enum { DM_KICK, DM_SNARE, DM_CLAP, DM_HAT, DM_OHAT, DM_PERC };
void drums_reset(void);
void drums_hit(uint32_t v, float vel);
void drums_render(float *l, float *r, uint32_t n, float level);   /* added into l, r */
   int drums_snared(void);                 /* a kick since the last call (PUMP) */
