/*
 * The handful of DOSBox typedefs and macros dbopl.cpp needs, so it builds
 * outside DOSBox. Everything else that dosbox.h and adlib.h used to supply went
 * with DBOPL::Handler - see the note at the top of dbopl.cpp.
 */
#ifndef PICOPOP_DBOPL_TYPES_H
#define PICOPOP_DBOPL_TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef uintptr_t Bitu;
typedef intptr_t  Bits;
typedef uint8_t   Bit8u;
typedef int8_t    Bit8s;
typedef uint16_t  Bit16u;
typedef int16_t   Bit16s;
typedef uint32_t  Bit32u;
typedef int32_t   Bit32s;
typedef uint64_t  Bit64u;
typedef int64_t   Bit64s;

#define INLINE           inline
#define DB_FASTCALL
#define GCC_UNLIKELY(x)  __builtin_expect(!!(x), 0)
#define GCC_LIKELY(x)    __builtin_expect(!!(x), 1)
#define GCC_ATTRIBUTE(x) __attribute__((x))

#endif /* PICOPOP_DBOPL_TYPES_H */
