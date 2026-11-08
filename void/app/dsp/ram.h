/* SPDX-License-Identifier: MPL-2.0
 * Included first in each dsp/ unit (fm1.toml o2_include). The sound runs from RAM, out of the flash cache's way: on
 * the FM-1 a drum hit or a new chord ran code the cache had let go, the block came late, the sound broke up.
 * RAM code may call only RAM code, so every inline helper (fastmath.h's too) is made always inline: no copy of one
 * is left in the flash for the RAM code to call. */
#ifndef VOID_RAM_H
#define VOID_RAM_H
#define inline __attribute__((always_inline)) __inline__
#include "plat.h"
#define RAM PLAT_RAM_CODE                /* a function of the audio path, in RAM */
#endif
