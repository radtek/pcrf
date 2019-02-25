#ifndef __PCRF_OTLV4_H__
#define __PCRF_OTLV4_H__

#define OTL_ORA11G_R2
#define OTL_STL
#define OTL_UBIGINT long unsigned int
#define OTL_STREAM_NO_PRIVATE_UNSIGNED_LONG_OPERATORS
#define OTL_ADD_NULL_TERMINATOR_TO_STRING_SIZE
#include "utils/otlv4.h"

#define PCRF_OTL_VALUES_ARE_NOT_EQUAL(a,b) ( ( (a).is_null() != (b).is_null() ) || ( 0 == (a).is_null() && ( (a).v != (b).v ) ) )

#endif /* __PCRF_OTLV4_H__ */
