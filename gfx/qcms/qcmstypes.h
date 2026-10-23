#ifndef QCMS_TYPES_H
#define QCMS_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(IS_LITTLE_ENDIAN) && !defined(IS_BIG_ENDIAN)
#if BYTE_ORDER == LITTLE_ENDIAN
#define IS_LITTLE_ENDIAN
#elif BYTE_ORDER == BIG_ENDIAN
#define IS_BIG_ENDIAN
#endif

/* all of the platforms that we use _MSC_VER on are little endian
 * so this is sufficient for now */

#ifdef __OS2__
#define IS_LITTLE_ENDIAN
#endif
#endif

#if !defined(IS_LITTLE_ENDIAN) && !defined(IS_BIG_ENDIAN)
#error Unknown endianess
#endif

#if defined (_SVR4) || defined (SVR4) || defined (__OpenBSD__) || defined (_sgi) || defined (__sun) || defined (sun) || defined (__digital__)
#  include <inttypes.h>
#elif defined (_AIX)
#  include <sys/inttypes.h>
#else
#  include <stdint.h>
#endif

#include <stdbool.h>

#ifdef __cplusplus
}
#endif

#endif
