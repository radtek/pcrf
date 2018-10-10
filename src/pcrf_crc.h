#ifndef __PCRF_CRC_H__
#define __PCRF_CRC_H__

#include <stdint.h>
#include <stddef.h>

uint16_t pcrf_calc_crc16( const unsigned char *p_pmucData, size_t p_stDataSize );

#endif /* __PCRF_CRC_H__ */
