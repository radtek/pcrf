#ifndef __PCRF_COMMON_DATA_TYPES_H__
#define __PCRF_COMMON_DATA_TYPES_H__

#include <stdint.h>

/*
Supported-Features ::= < AVP header: 628 10415 >
{ Vendor-Id }
{ Feature-List-ID }
{ Feature-List }
*/
struct SSF {
  uint32_t m_ui32VendorId;
  uint32_t m_ui32FeatureListID;
  uint32_t m_ui32FeatureList;
  SSF() :m_ui32VendorId( 0 ), m_ui32FeatureListID( 0 ), m_ui32FeatureList( 0 ) { }
};


#endif /* __PCRF_COMMON_DATA_TYPES_H__ */
