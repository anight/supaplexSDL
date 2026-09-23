/* Minimal C interface to the vendored DOSBox OPL2/OPL3 emulator (DBOPL).
 * ADLIB.SND only ever drives an OPL2, so the chip is created in OPL2 mode
 * and rendered mono. */
#ifndef OPL_H
#define OPL_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void opl_init(int sample_rate);            /* also resets the chip */
void opl_write(uint8_t reg, uint8_t val);
void opl_render(int32_t *mono, int frames); /* accumulates; zero it yourself */

#ifdef __cplusplus
}
#endif
#endif
