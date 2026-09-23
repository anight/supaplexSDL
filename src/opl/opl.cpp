/* Glue between picosupaplex's C code and DBOPL's C++ core.
 *
 * Setup() calibrates the envelope rate tables by brute force for the given
 * sample rate, which costs tens of milliseconds, so it happens once at
 * init and never per song. */
#include <new>
#include <string.h>

#include "dbopl_types.h"
#include "dbopl.h"
#include "opl.h"

alignas(DBOPL::Chip) static unsigned char chip_storage[sizeof(DBOPL::Chip)];
static DBOPL::Chip *chip;

extern "C" void opl_init(int sample_rate)
{
    DBOPL::InitTables();
    memset(chip_storage, 0, sizeof chip_storage);
    chip = new (chip_storage) DBOPL::Chip(false);   /* false = OPL2 */
    chip->Setup((Bit32u)(sample_rate > 0 ? sample_rate : 44100));
}

extern "C" void opl_write(uint8_t reg, uint8_t val)
{
    if (chip) chip->WriteReg(reg, val);
}

extern "C" void opl_render(int32_t *mono, int frames)
{
    if (!chip || frames <= 0) { return; }
    /* GenerateBlock2 writes one 32-bit sample per frame and DOSBox caps a
     * call at 512; keep to that. */
    while (frames > 0) {
        int n = frames > 512 ? 512 : frames;
        chip->GenerateBlock2((Bitu)n, mono);
        mono   += n;
        frames -= n;
    }
}
