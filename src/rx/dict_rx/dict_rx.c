/*********************************************************************************************************
 * Software License Agreement (BSD License)                                                               *
 * Author: Sabir Izrafilov         						                                                            *
 *													                                                                              *
 * Copyright (c) 2016, Sabir Izrafilov.                                                      	            *
 *										                                                                                    *
 * All rights reserved.											                                                              *
 * 													                                                                              *
 * Redistribution and use of this software in source and binary forms, with or without modification, are  *
 * permitted provided that the following conditions are met:						                                  *
 * 													                                                                              *
 * * Redistributions of source code must retain the above 						                                    *
 *   copyright notice, this list of conditions and the 							                                      *
 *   following disclaimer.										                                                            *
 *    													                                                                          *
 * * Redistributions in binary form must reproduce the above 						                                  *
 *   copyright notice, this list of conditions and the 							                                      *
 *   following disclaimer in the documentation and/or other						                                    *
 *   materials provided with the distribution.								                                            *
 * 													                                                                              *
 * * Neither the name of the Teraoka Laboratory nor the 							                                    *
 *   names of its contributors may be used to endorse or 						                                      *
 *   promote products derived from this software without 						                                      *
 *   specific prior written permission of Teraoka Laboratory 						                                  *
 *   													                                                                            *
 * 													                                                                              *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED *
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A *
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR *
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 	  *
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 	  *
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR *
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF   *
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.								                                              *
 *********************************************************************************************************/


/*********************************************************************************************************

 === Sabir Izrafilov (subbeer@gmail.com)  -  Nov/2016 ===
 === Dictionary Rx ===
 based on ETSI TS 129 214 V12.12.0 (2016-08)

 *********************************************************************************************************/

#include <freeDiameter/extension.h>

#include "dict_rx.h"

struct local_rules_definition {
  struct  dict_avp_request avp_vendor_plus_name;
  enum    rule_position	position;
  int 	  min;
  int     max;
};

#define RULE_ORDER( _position ) ((((_position) == RULE_FIXED_HEAD) || ((_position) == RULE_FIXED_TAIL)) ? 1 : 0 )

#define PARSE_loc_rules( _rulearray, _parent) {								                                    \
  size_t __ar;											                                                              \
  struct dict_rule_request __dict_req;                                                            \
  struct dict_object *__pDictObj;                                                                 \
  for (__ar = 0; __ar < sizeof(_rulearray) / sizeof((_rulearray)[0]); __ar++) {			              \
    struct dict_rule_data __data = {                                                              \
      NULL, 							                                                                        \
      (_rulearray)[__ar].position,							                                                  \
      0, 										                                                                      \
      (_rulearray)[__ar].min,								                                                      \
      (_rulearray)[__ar].max                                                                      \
    };							                                                                              \
    __data.rule_order = RULE_ORDER(__data.rule_position);					                                \
    CHECK_FCT (                                                                                   \
      fd_dict_search ( 								                                                            \
        fd_g_config->cnf_dict,								                                                    \
        DICT_AVP, 									                                                              \
        AVP_BY_NAME_AND_VENDOR, 							                                                    \
        &(_rulearray)[__ar].avp_vendor_plus_name,					                                        \
        &__data.rule_avp, 0));							                                                      \
    if ( !__data.rule_avp ) {								                                                      \
      TRACE_DEBUG(INFO, "AVP Not found: '%s'", (_rulearray)[__ar].avp_vendor_plus_name.avp_name); \
      return ENOENT;									                                                            \
    }											                                                                        \
    __dict_req.rule_parent  = _parent;                                                            \
    __dict_req.rule_avp     = __data.rule_avp;                                                    \
    __pDictObj = NULL;                                                                            \
    if (0 == fd_dict_search(fd_g_config->cnf_dict, DICT_RULE, RULE_BY_AVP_AND_PARENT, &__dict_req, &__pDictObj, ENOENT)) { \
      LOG_D("try to delete rule: %s", (_rulearray)[__ar].avp_vendor_plus_name.avp_name);          \
      CHECK_FCT_DO( fd_dict_delete(__pDictObj), LOG_D("can not delete rule: %s", (_rulearray)[__ar].avp_vendor_plus_name.avp_name); ); \
    }                                                                                             \
    CHECK_FCT_DO (                                                                                \
      fd_dict_new ( fd_g_config->cnf_dict, DICT_RULE, &__data, _parent, NULL),	                  \
      {							        		                                                                  \
        TRACE_DEBUG (INFO, "Error on rule with AVP '%s'",      			                              \
        (_rulearray)[__ar].avp_vendor_plus_name.avp_name);		                                    \
        return EINVAL;					      			                                                      \
      }                                                                                           \
    );							      			                                                                  \
  }									      			                                                                  \
}

#define REMOVE_RULE(__rule_code, __vendor, __parent)                                                                          \
struct dict_rule_request __dict_req;                                                                                          \
  struct dict_avp_request __avp_request;                                                                                      \
  struct dict_object *__pAVP;                                                                                                 \
  struct dict_object *__pRule;                                                                                                \
  __avp_request.avp_vendor = __vendor;                                                                                        \
  __avp_request.avp_code = __rule_code;                                                                                       \
  CHECK_FCT(                                                                                                                  \
    fd_dict_search(                                                                                                           \
      fd_g_config->cnf_dict,                                                                                                  \
      DICT_AVP,                                                                                                               \
      AVP_BY_CODE_AND_VENDOR,                                                                                                 \
      &__avp_request,                                                                                                         \
      &__pAVP, 0));							                                                                                              \
  if (!__pAVP) {                                                                                                              \
      TRACE_DEBUG(INFO, "AVP Not found: '%d'", __rule_code);                                                                  \
  } else {                                                                                                                    \
      __dict_req.rule_parent = __parent;                                                                                      \
      __dict_req.rule_avp = __pAVP;                                                                                           \
      if (0 == fd_dict_search(fd_g_config->cnf_dict, DICT_RULE, RULE_BY_AVP_AND_PARENT, &__dict_req, &__pRule, ENOENT)) {     \
          LOG_D("try to delete rule: %d", __rule_code);                                                                       \
          CHECK_FCT_DO(fd_dict_delete(__pRule), LOG_D("can not delete rule: %d", __rule_code); );                             \
      }                                                                                                                       \
  }




#define CHECK_DICT_SEARCH( _type, _criteria, _what, _result )   CHECK_FCT( fd_dict_search( fd_g_config->cnf_dict, (_type), (_criteria), (_what), (_result), ENOENT) );
#define CHECK_DICT_NEW( _type, _data, _parent, _ref )           CHECK_FCT( fd_dict_new( fd_g_config->cnf_dict, (_type), (_data), (_parent), (_ref)) );

static int dict_rx_entry (char *conffile)
{
  struct dict_object *psoRxApp;

  /* suppress compiler warning */
  conffile = conffile;

  /* application section *****************************************************/
  {
    struct dict_vendor_data vendor_data = { VENDOR_3GPP_ID, "3GPP" };
    struct dict_object *psoVendorDict;
    struct dict_application_data appl_data = { APP_RX_ID, "Rx application" };

    CHECK_DICT_NEW( DICT_VENDOR, &vendor_data, NULL, &psoVendorDict );
    CHECK_DICT_NEW( DICT_APPLICATION, &appl_data, psoVendorDict, &psoRxApp );
  }

  /* rx specific avp section *************************************************/
  struct dict_object *address_type;
	struct dict_object *UTF8string_type;
	struct dict_object *IPFilterRule_type;
  struct dict_object *Time_type;

  CHECK_DICT_SEARCH( DICT_TYPE, TYPE_BY_NAME, "Address", &address_type );
  CHECK_DICT_SEARCH( DICT_TYPE, TYPE_BY_NAME, "UTF8String", &UTF8string_type );
	CHECK_DICT_SEARCH( DICT_TYPE, TYPE_BY_NAME, "IPFilterRule", &IPFilterRule_type );
  CHECK_DICT_SEARCH( DICT_TYPE, TYPE_BY_NAME, "Time", &Time_type );

  /* Abort-Cause | 500 | 5.3.1 | Enumerated | M,V | P | | | Y */
  {
    size_t ind;
    struct dict_type_data     data_type = { .type_base = AVP_TYPE_INTEGER32, .type_name = "Enumerated*(Abort-Cause)" };
    struct dict_object        *enum_type;
    struct dict_enumval_data  data_enum[] = {
      { "BEARER_RELEASED",                          { .i32 = 0 } },
      { "INSUFFICIENT_SERVER_RESOURCES",            { .i32 = 1 } },
      { "INSUFFICIENT_BEARER_RESOURCES",            { .i32 = 2 } },
      { "PS_TO_CS_HANDOVER",                        { .i32 = 3 } },
      { "SPONSORED_DATA_CONNECTIVITY_ DISALLOWED",  { .i32 = 4 } }
    };
    struct dict_avp_data      data_avp = {
      500,
      VENDOR_3GPP_ID,
      "Abort-Cause",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_TYPE_INTEGER32
    };
    /* create enumerated type */
    CHECK_DICT_NEW( DICT_TYPE, &data_type, NULL, &enum_type );
    /* create enumerated values */
    for (ind = 0; ind < sizeof(data_enum)/sizeof(*data_enum); ++ind) {
      CHECK_DICT_NEW( DICT_ENUMVAL, &data_enum[ind], enum_type, NULL );
    }
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, enum_type, NULL );
  }

  /* Access-Network-Charging-Address | 501 | 5.3.2 | Address | M,V | P | | | Y */
  {
    struct dict_avp_data      data_avp = {
      501,
      VENDOR_3GPP_ID,
      "Access-Network-Charging-Address",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_TYPE_OCTETSTRING
    };
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, address_type, NULL );
  }

  /* Access-Network-Charging-Identifier | 502 | 5.3.3 | Grouped | M,V | P | | | Y */
  {
    struct dict_avp_data      data_avp = {
      502,
      VENDOR_3GPP_ID,
      "Access-Network-Charging-Identifier",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_TYPE_GROUPED
    };
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, NULL, NULL );
  }

  /* Access-Network-Charging-Identifier-Value | 503 | 5.3.4 | OctetString | M,V | | | P| Y */
  {
    struct dict_avp_data      data_avp = {
      503,
      VENDOR_3GPP_ID,
      "Access-Network-Charging-Identifier-Value",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_TYPE_OCTETSTRING
    };
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, NULL, NULL );
  }

  /* Acceptable-Service-Info | 526 | 5.3.24 | Grouped | M,V | P | | | Y */
  {
    struct dict_avp_data      data_avp = {
      526,
      VENDOR_3GPP_ID,
      "Acceptable-Service-Info",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_TYPE_GROUPED
    };
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, NULL, NULL );
  }

  /* AF-Application-Identifier | 504 | 5.3.5 | OctetString | M,V | P | | | Y */
  {
    struct dict_avp_data      data_avp = {
      504,
      VENDOR_3GPP_ID,
      "AF-Application-Identifier",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_TYPE_OCTETSTRING
    };
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, NULL, NULL );
  }

  /* AF-Charging-Identifier | 505 | 5.3.6 | OctetString | M,V | P | | | Y */
  {
    struct dict_avp_data      data_avp = {
      505,
      VENDOR_3GPP_ID,
      "AF-Charging-Identifier",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_TYPE_OCTETSTRING
    };
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, NULL, NULL );
  }

  /* Application-Service-Provider-Identity | 532 | 5.3.29 | UTF8String | V | P | M | | | Y */
  {
    struct dict_avp_data      data_avp = {
      532,
      VENDOR_3GPP_ID,
      "Application-Service-Provider-Identity",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR,
      AVP_TYPE_OCTETSTRING
    };
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, UTF8string_type, NULL );
  }

  /* Codec-Data | 524 | 5.3.7 | OctetString | M,V | P | | | Y */
  {
    struct dict_avp_data      data_avp = {
      524,
      VENDOR_3GPP_ID,
      "Codec-Data",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_TYPE_OCTETSTRING
    };
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, NULL, NULL );
  }

  /* Flow-Description | 507 | 5.3.8 | IPFilterRule | M,V | P | | | Y */
  {
    struct dict_avp_data      data_avp = {
      507,
      VENDOR_3GPP_ID,
      "Flow-Description",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_TYPE_OCTETSTRING
    };
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, IPFilterRule_type, NULL );
  }

  /* Flow-Number | 509 | 5.3.9 | Unsigned32 | M,V | P | | | Y */
  {
    struct dict_avp_data      data_avp = {
      509,
      VENDOR_3GPP_ID,
      "Flow-Number",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_TYPE_UNSIGNED32
    };
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, NULL, NULL );
  }

  /* Flows | 510 | 5.3.10 | Grouped | M,V | P | | | Y */
  {
    struct dict_avp_data      data_avp = {
      510,
      VENDOR_3GPP_ID,
      "Flows",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_TYPE_GROUPED
    };
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, NULL, NULL );
  }

  /* Flow-Status | 511 | 5.3.11 | Enumerated | M,V | P | | | Y */
  {
    size_t ind;
    struct dict_type_data     data_type = { .type_base = AVP_TYPE_INTEGER32, .type_name = "Enumerated*(Flow-Status)" };
    struct dict_object        *enum_type;
    struct dict_enumval_data  data_enum[] = {
      { "ENABLED-UPLINK",   { .i32 = 0 } },
      { "ENABLED-DOWNLINK", { .i32 = 1 } },
      { "ENABLED",          { .i32 = 2 } },
      { "DISABLED",         { .i32 = 3 } },
      { "REMOVED",          { .i32 = 4 } }
    };
    struct dict_avp_data      data_avp = {
      511,
      VENDOR_3GPP_ID,
      "Flow-Status",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_TYPE_INTEGER32
    };
    /* create enumerated type */
    CHECK_DICT_NEW( DICT_TYPE, &data_type, NULL, &enum_type );
    /* create enumerated values */
    for (ind = 0; ind < sizeof(data_enum)/sizeof(*data_enum); ++ind) {
      CHECK_DICT_NEW( DICT_ENUMVAL, &data_enum[ind], enum_type, NULL );
    }
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, enum_type, NULL );
  }

  /* Flow-Usage | 512 | 5.3.12 | Enumerated | M,V | P | | | Y */
  {
    size_t ind;
    struct dict_type_data     data_type = { .type_base = AVP_TYPE_INTEGER32, .type_name = "Enumerated*(Flow-Usage)" };
    struct dict_object        *enum_type;
    struct dict_enumval_data  data_enum[] = {
      { "NO_INFORMATION", { .i32 = 0 } },
      { "RTCP",           { .i32 = 1 } },
      { "AF_SIGNALLING",  { .i32 = 2 } }
    };
    struct dict_avp_data      data_avp = {
      512,
      VENDOR_3GPP_ID,
      "Flow-Usage",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_TYPE_INTEGER32
    };
    /* create enumerated type */
    CHECK_DICT_NEW( DICT_TYPE, &data_type, NULL, &enum_type );
    /* create enumerated values */
    for (ind = 0; ind < sizeof(data_enum)/sizeof(*data_enum); ++ind) {
      CHECK_DICT_NEW( DICT_ENUMVAL, &data_enum[ind], enum_type, NULL );
    }
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, enum_type, NULL );
  }

  /* GCS-Identifier | 538 | 5.3.36 | OctetString | V | P | | M | Y */
  {
    struct dict_avp_data      data_avp = {
      538,
      VENDOR_3GPP_ID,
      "GCS-Identifier",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR,
      AVP_TYPE_OCTETSTRING
    };
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, NULL, NULL );
  }

  /* Service-URN | 525 | 5.3.23 | OctetString | M,V | P | | | Y */
  {
    struct dict_avp_data      data_avp = {
      525,
      VENDOR_3GPP_ID,
      "Service-URN",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_TYPE_OCTETSTRING
    };
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, NULL, NULL );
  }

  /* Specific-Action | 513 | 5.3.13 | Enumerated | M,V | P | | | Y */
  {
    size_t ind;
    struct dict_type_data     data_type = { .type_base = AVP_TYPE_INTEGER32, .type_name = "Enumerated*(Specific-Action)" };
    struct dict_object        *enum_type;
    struct dict_enumval_data  data_enum[] = {
/*      { "Void", { .i32 = 0 } }, */
      { "CHARGING_CORRELATION_EXCHANGE",           { .i32 = 1 } },
      { "INDICATION_OF_LOSS_OF_BEARER",  { .i32 = 2 } },
      { "INDICATION_OF_RECOVERY_OF_BEARER",  { .i32 = 3 } },
      { "INDICATION_OF_RELEASE_OF_BEARER",  { .i32 = 4 } },
/*      { "Void",  { .i32 = 5 } }, */
      { "IP-CAN_CHANGE",  { .i32 = 6 } },
      { "INDICATION_OF_OUT_OF_CREDIT",  { .i32 = 7 } },
      { "INDICATION_OF_SUCCESSFUL_RESOURCES_ALLOCATION",  { .i32 = 8 } },
      { "INDICATION_OF_FAILED_RESOURCES_ALLOCATION",  { .i32 = 9 } },
      { "INDICATION_OF_LIMITED_PCC_DEPLOYMENT",  { .i32 = 10 } },
      { "USAGE_REPORT",  { .i32 = 11 } },
      { "ACCESS_NETWORK_INFO_REPORT",  { .i32 = 12 } },
      { "INDICATION_OF_RECOVERY_FROM_LIMITED_PCC_DEPLOYMENT",  { .i32 = 13 } },
      { "INDICATION_OF_ACCESS_NETWORK_INFO_REPORTING_FAILURE",  { .i32 = 14 } }
    };
    struct dict_avp_data      data_avp = {
      513,
      VENDOR_3GPP_ID,
      "Specific-Action",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_TYPE_INTEGER32
    };
    /* create enumerated type */
    CHECK_DICT_NEW( DICT_TYPE, &data_type, NULL, &enum_type );
    /* create enumerated values */
    for (ind = 0; ind < sizeof(data_enum)/sizeof(*data_enum); ++ind) {
      CHECK_DICT_NEW( DICT_ENUMVAL, &data_enum[ind], enum_type, NULL );
    }
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, enum_type, NULL );
  }

  /* Max-Requested-Bandwidth-DL | 515 | 5.3.14 | Unsigned32 | M,V | P | | | Y */
  {
    struct dict_avp_data      data_avp = {
      515,
      VENDOR_3GPP_ID,
      "Max-Requested-Bandwidth-DL",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_TYPE_UNSIGNED32
    };
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, NULL, NULL );
  }

  /* Max-Requested-Bandwidth-UL | 516 | 5.3.15 | Unsigned32 | M,V | P | | | Y */
  {
    struct dict_avp_data      data_avp = {
      516,
      VENDOR_3GPP_ID,
      "Max-Requested-Bandwidth-UL",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_TYPE_UNSIGNED32
    };
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, NULL, NULL );
  }

  /* Media-Component-Description | 517 | 5.3.16 | Grouped | M,V | P | | | Y */
  {
    struct dict_avp_data      data_avp = {
      517,
      VENDOR_3GPP_ID,
      "Media-Component-Description",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_TYPE_GROUPED
    };
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, NULL, NULL );
  }

  /* Media-Component-Number | 518 | 5.3.17 | Unsigned32 | M,V | P | | | Y */
  {
    struct dict_avp_data      data_avp = {
      518,
      VENDOR_3GPP_ID,
      "Media-Component-Number",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_TYPE_UNSIGNED32
    };
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, NULL, NULL );
  }

  /* Media-Sub-Component | 519 | 5.3.18 | Grouped | M,V | P | | | Y */
  {
    struct dict_avp_data      data_avp = {
      519,
      VENDOR_3GPP_ID,
      "Media-Sub-Component",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_TYPE_GROUPED
    };
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, NULL, NULL );
  }

  /* Media-Type | 520 | 5.3.19 | Enumerated | M,V | P | | | Y */
  {
    size_t ind;
    struct dict_type_data     data_type = { .type_base = AVP_TYPE_INTEGER32, .type_name = "Enumerated*(Media-Type)" };
    struct dict_object        *enum_type;
    struct dict_enumval_data  data_enum[] = {
      { "AUDIO",        { .i32 = 0 } },
      { "VIDEO",        { .i32 = 1 } },
      { "DATA",         { .i32 = 2 } },
      { "APPLICATION",  { .i32 = 3 } },
      { "CONTROL",      { .i32 = 4 } },
      { "TEXT",         { .i32 = 5 } },
      { "MESSAGE",      { .i32 = 6 } },
      { "OTHER",        { .i32 = 0xFFFFFFFF } }
    };
    struct dict_avp_data      data_avp = {
      520,
      VENDOR_3GPP_ID,
      "Media-Type",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_TYPE_INTEGER32
    };
    /* create enumerated type */
    CHECK_DICT_NEW( DICT_TYPE, &data_type, NULL, &enum_type );
    /* create enumerated values */
    for (ind = 0; ind < sizeof(data_enum)/sizeof(*data_enum); ++ind) {
      CHECK_DICT_NEW( DICT_ENUMVAL, &data_enum[ind], enum_type, NULL );
    }
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, enum_type, NULL );
  }

  /* MPS-Identifier | 528 | 5.3.30 | OctetString | V | P | | M | Y */
  {
    struct dict_avp_data      data_avp = {
      528,
      VENDOR_3GPP_ID,
      "MPS-Identifier",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR,
      AVP_TYPE_OCTETSTRING
    };
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, NULL, NULL );
  }

  /* Min-Requested-Bandwidth-DL | 534 | 5.3.32 | Unsigned32 | V | P | | M | Y */
  {
    struct dict_avp_data      data_avp = {
      534,
      VENDOR_3GPP_ID,
      "Min-Requested-Bandwidth-DL",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR,
      AVP_TYPE_UNSIGNED32
    };
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, NULL, NULL );
  }

  /* Min-Requested-Bandwidth-UL | 535 | 5.3.33 | Unsigned32 | V | P | | M | Y */
  {
    struct dict_avp_data      data_avp = {
      535,
      VENDOR_3GPP_ID,
      "Min-Requested-Bandwidth-UL",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR,
      AVP_TYPE_UNSIGNED32
    };
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, NULL, NULL );
  }

  /* RR-Bandwidth | 521 | 5.3.20 | Unsigned32 | M,V | P | | | Y */
  {
    struct dict_avp_data      data_avp = {
      521,
      VENDOR_3GPP_ID,
      "RR-Bandwidth",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_TYPE_UNSIGNED32
    };
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, NULL, NULL );
  }

  /* RS-Bandwidth | 522 | 5.3.21 | Unsigned32 | M,V | P | | | Y */
  {
    struct dict_avp_data      data_avp = {
      522,
      VENDOR_3GPP_ID,
      "RS-Bandwidth",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_TYPE_UNSIGNED32
    };
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, NULL, NULL );
  }

  /* Service-Info-Status | 527 | 5.3.25 | Enumerated | M,V | P | | | Y */
  {
    size_t ind;
    struct dict_type_data     data_type = { .type_base = AVP_TYPE_INTEGER32, .type_name = "Enumerated*(Service-Info-Status)" };
    struct dict_object        *enum_type;
    struct dict_enumval_data  data_enum[] = {
      { "FINAL SERVICE INFORMATION",        { .i32 = 0 } },
      { "PRELIMINARY SERVICE INFORMATION",  { .i32 = 1 } }
    };
    struct dict_avp_data      data_avp = {
      527,
      VENDOR_3GPP_ID,
      "Service-Info-Status",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_TYPE_INTEGER32
    };
    /* create enumerated type */
    CHECK_DICT_NEW( DICT_TYPE, &data_type, NULL, &enum_type );
    /* create enumerated values */
    for (ind = 0; ind < sizeof(data_enum)/sizeof(*data_enum); ++ind) {
      CHECK_DICT_NEW( DICT_ENUMVAL, &data_enum[ind], enum_type, NULL );
    }
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, enum_type, NULL );
  }

  /* SIP-Forking-Indication | 523 | 5.3.22 | Enumerated | M,V | P | | | Y */
  {
    size_t ind;
    struct dict_type_data     data_type = { .type_base = AVP_TYPE_INTEGER32, .type_name = "Enumerated*(SIP-Forking-Indication)" };
    struct dict_object        *enum_type;
    struct dict_enumval_data  data_enum[] = {
      { "SINGLE_DIALOGUE",    { .i32 = 0 } },
      { "SEVERAL_DIALOGUES",  { .i32 = 1 } }
    };
    struct dict_avp_data      data_avp = {
      523,
      VENDOR_3GPP_ID,
      "SIP-Forking-Indication",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_TYPE_INTEGER32
    };
    /* create enumerated type */
    CHECK_DICT_NEW( DICT_TYPE, &data_type, NULL, &enum_type );
    /* create enumerated values */
    for (ind = 0; ind < sizeof(data_enum)/sizeof(*data_enum); ++ind) {
      CHECK_DICT_NEW( DICT_ENUMVAL, &data_enum[ind], enum_type, NULL );
    }
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, enum_type, NULL );
  }

  /* Sponsor-Identity | 531 | 5.3.28 | UTF8String | V | P | | M | Y */
  {
    struct dict_avp_data      data_avp = {
      531,
      VENDOR_3GPP_ID,
      "Sponsor-Identity",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR,
      AVP_TYPE_OCTETSTRING
    };
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, UTF8string_type, NULL );
  }

  /* Sponsored-Connectivity-Data | 530 | 5.3.27 | Grouped | V | P | | M | Y */
  {
    struct dict_avp_data      data_avp = {
      530,
      VENDOR_3GPP_ID,
      "Sponsored-Connectivity-Data",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR,
      AVP_TYPE_GROUPED
    };
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, NULL, NULL );
  }

  /* AF-Signalling-Protocol | 529 | 5.3.26 | Enumerated | V | P | | M | Y */
  {
    size_t ind;
    struct dict_type_data     data_type = { .type_base = AVP_TYPE_INTEGER32, .type_name = "Enumerated*(AF-Signalling-Protocol)" };
    struct dict_object        *enum_type;
    struct dict_enumval_data  data_enum[] = {
      { "NO_INFORMATION", { .i32 = 0 } },
      { "SIP",            { .i32 = 1 } }
    };
    struct dict_avp_data      data_avp = {
      529,
      VENDOR_3GPP_ID,
      "AF-Signalling-Protocol",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR,
      AVP_TYPE_INTEGER32
    };
    /* create enumerated type */
    CHECK_DICT_NEW( DICT_TYPE, &data_type, NULL, &enum_type );
    /* create enumerated values */
    for (ind = 0; ind < sizeof(data_enum)/sizeof(*data_enum); ++ind) {
      CHECK_DICT_NEW( DICT_ENUMVAL, &data_enum[ind], enum_type, NULL );
    }
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, enum_type, NULL );
  }

  /* Required-Access-Info | 536 | 5.3.34 | Enumerated | V | P | | M | Y */
  {
    size_t ind;
    struct dict_type_data     data_type = { .type_base = AVP_TYPE_INTEGER32, .type_name = "Enumerated*(Required-Access-Info)" };
    struct dict_object        *enum_type;
    struct dict_enumval_data  data_enum[] = {
      { "USER_LOCATION",  { .i32 = 0 } },
      { "MS_TIME_ZONE",   { .i32 = 1 } }
    };
    struct dict_avp_data      data_avp = {
      536,
      VENDOR_3GPP_ID,
      "Required-Access-Info",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR,
      AVP_TYPE_INTEGER32
    };
    /* create enumerated type */
    CHECK_DICT_NEW( DICT_TYPE, &data_type, NULL, &enum_type );
    /* create enumerated values */
    for (ind = 0; ind < sizeof(data_enum)/sizeof(*data_enum); ++ind) {
      CHECK_DICT_NEW( DICT_ENUMVAL, &data_enum[ind], enum_type, NULL );
    }
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, enum_type, NULL );
  }

  /* Rx-Request-Type | 533 | 5.3.31 | Enumerated | V | P | | M | Y */
  {
    size_t ind;
    struct dict_type_data     data_type = { .type_base = AVP_TYPE_INTEGER32, .type_name = "Enumerated*(Rx-Request-Type)" };
    struct dict_object        *enum_type;
    struct dict_enumval_data  data_enum[] = {
      { "INITIAL_REQUEST",  { .i32 = 0 } },
      { "UPDATE_REQUEST",   { .i32 = 1 } },
      { "PCSCF_RESTORATION",   { .i32 = 2 } }
    };
    struct dict_avp_data      data_avp = {
      533,
      VENDOR_3GPP_ID,
      "Rx-Request-Type",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR,
      AVP_TYPE_INTEGER32
    };
    /* create enumerated type */
    CHECK_DICT_NEW( DICT_TYPE, &data_type, NULL, &enum_type );
    /* create enumerated values */
    for (ind = 0; ind < sizeof(data_enum)/sizeof(*data_enum); ++ind) {
      CHECK_DICT_NEW( DICT_ENUMVAL, &data_enum[ind], enum_type, NULL );
    }
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, enum_type, NULL );
  }

  /* IP-Domain-Id | 537 | 5.3.35 | OctetString | V | P | | M | Y */
  {
    struct dict_avp_data      data_avp = {
      537,
      VENDOR_3GPP_ID,
      "IP-Domain-Id",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR,
      AVP_TYPE_OCTETSTRING
    };
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, NULL, NULL );
  }

  /* reused avp section ******************************************************/
  /*
    OC-OLR ::= < AVP Header: 623 >
      < OC-Sequence-Number >
      < OC-Report-Type >
      [ OC-Reduction-Percentage ]
      [ OC-Validity-Duration ]
      * [ AVP ]
  */
  /* OC-Sequence-Number | 624 | RFC-7683[7.4] | Unsigned64 | | V */
  {
    struct dict_avp_data      data_avp = {
      624,
      VENDOR_DIAM_ID,
      "OC-Sequence-Number",
      AVP_FLAG_VENDOR,
      0,
      AVP_TYPE_UNSIGNED64
    };
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, NULL, NULL );
  }
  /* OC-Report-Type | 626 | RFC-7683[7.6] | Enumerated | | V */
  {
    size_t ind;
    struct dict_type_data     data_type = { .type_base = AVP_TYPE_INTEGER32, .type_name = "Enumerated*(OC-Report-Type)" };
    struct dict_object        *enum_type;
    struct dict_enumval_data  data_enum[] = {
      { "HOST_REPORT",  { .i32 =  0 } },
      { "REALM_REPORT", { .i32 =  1 } }
    };
    struct dict_avp_data      data_avp = {
      626,
      VENDOR_DIAM_ID,
      "OC-Report-Type",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR,
      AVP_TYPE_INTEGER32
    };
    /* create enumerated type */
    CHECK_DICT_NEW( DICT_TYPE, &data_type, NULL, &enum_type );
    /* create enumerated values */
    for (ind = 0; ind < sizeof(data_enum)/sizeof(*data_enum); ++ind) {
      CHECK_DICT_NEW( DICT_ENUMVAL, &data_enum[ind], enum_type, NULL );
    }
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, enum_type, NULL );
  }
  /* OC-Reduction-Percentage | 627 | RFC-7683[7.7] | Unsigned32 | | V */
  {
    struct dict_avp_data      data_avp = {
      627,
      VENDOR_DIAM_ID,
      "OC-Reduction-Percentage",
      AVP_FLAG_VENDOR,
      0,
      AVP_TYPE_UNSIGNED32
    };
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, NULL, NULL );
  }
  /* OC-Validity-Duration | 625 | RFC-7683[7.5] | Unsigned32 | | V */
  {
    struct dict_avp_data      data_avp = {
      625,
      VENDOR_DIAM_ID,
      "OC-Validity-Duration",
      AVP_FLAG_VENDOR,
      0,
      AVP_TYPE_UNSIGNED32
    };
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, NULL, NULL );
  }
  /* OC-OLR | 623 | RFC-7683[7.3] | Grouped | | V */
  {
    struct dict_object *group_avp;
    struct dict_avp_data      data_avp = {
      623,
      VENDOR_DIAM_ID,
      "OC-OLR",
      AVP_FLAG_VENDOR,
      0,
      AVP_TYPE_GROUPED
    };
    struct local_rules_definition avp_rules[] = {
      { { VENDOR_DIAM_ID, 0, "OC-Sequence-Number" }, RULE_REQUIRED,  1,  1 },
      { { VENDOR_DIAM_ID, 0, "OC-Report-Type" },  RULE_REQUIRED,  1,  1 },
      { { VENDOR_DIAM_ID, 0, "OC-Reduction-Percentage" },  RULE_OPTIONAL, -1,  1 },
      { { VENDOR_DIAM_ID, 0, "OC-Validity-Duration" },  RULE_OPTIONAL, -1,  1 }
    };

    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, NULL, &group_avp );
    PARSE_loc_rules (avp_rules, group_avp);
  }

  /*
    OC-Supported-Features ::= < AVP Header: 621 >
      [ OC-Feature-Vector ]
  */
  /* OC-Feature-Vector | 622 | RFC-7683[7.2] | Unsigned64 | | V */
  {
    struct dict_avp_data      data_avp = {
      622,
      VENDOR_DIAM_ID,
      "OC-Feature-Vector",
      AVP_FLAG_VENDOR,
      0,
      AVP_TYPE_UNSIGNED64
    };
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, NULL, NULL );
  }
  /* OC-Supported-Features | 621 | RFC-7683[7.1] | Grouped | | V */
  {
    struct dict_object *group_avp;
    struct dict_avp_data      data_avp = {
      621,
      VENDOR_DIAM_ID,
      "OC-Supported-Features",
      AVP_FLAG_VENDOR,
      0,
      AVP_TYPE_GROUPED
    };
    struct local_rules_definition avp_rules[] = {
      { { VENDOR_DIAM_ID, 0, "OC-Feature-Vector" }, RULE_OPTIONAL,  -1,  1 }
    };

    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, NULL, &group_avp );
    PARSE_loc_rules (avp_rules, group_avp);
  }

  /* Reservation-Priority | 458 | ETSI TS 183 017 V3.2.1 (2010-02)[7.3.9] | Enumerated | V | M | | | Y */
  {
    size_t ind;
    struct dict_type_data     data_type = { .type_base = AVP_TYPE_INTEGER32, .type_name = "Enumerated*(Reservation-Priority)" };
    struct dict_object        *enum_type;
    struct dict_enumval_data  data_enum[] = {
      { "DEFAULT",            { .i32 =  0 } },
      { "PRIORITY-ONE",       { .i32 =  1 } },
      { "PRIORITY-TWO",       { .i32 =  2 } },
      { "PRIORITY-THREE",     { .i32 =  3 } },
      { "PRIORITY-FOUR",      { .i32 =  4 } },
      { "PRIORITY-FIVE",      { .i32 =  5 } },
      { "PRIORITY-SIX",       { .i32 =  6 } },
      { "PRIORITY-SEVEN",     { .i32 =  7 } },
      { "PRIORITY-EIGHT",     { .i32 =  8 } },
      { "PRIORITY-NINE",      { .i32 =  9 } },
      { "PRIORITY-TEN",       { .i32 = 10 } },
      { "PRIORITY-ELEVEN",    { .i32 = 11 } },
      { "PRIORITY-TWELVE",    { .i32 = 12 } },
      { "PRIORITY-THIRTEEN",  { .i32 = 13 } },
      { "PRIORITY-FOURTEEN",  { .i32 = 14 } },
      { "PRIORITY-FIFTEEN",   { .i32 = 15 } }
    };
    struct dict_avp_data      data_avp = {
      458,
      VENDOR_3GPP_ID,
      "Reservation-Priority",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR,
      AVP_TYPE_INTEGER32
    };
    /* create enumerated type */
    CHECK_DICT_NEW( DICT_TYPE, &data_type, NULL, &enum_type );
    /* create enumerated values */
    for (ind = 0; ind < sizeof(data_enum)/sizeof(*data_enum); ++ind) {
      CHECK_DICT_NEW( DICT_ENUMVAL, &data_enum[ind], enum_type, NULL );
    }
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, enum_type, NULL );
  }

  /* Rx specific Experimental-Result-Code AVP values */
  {
    size_t ind;
    struct dict_object *avp;
    struct dict_object *avp_type;
    struct dict_enumval_data  data_enum[] = {
      { "INVALID_SERVICE_INFORMATION",              { .i32 =  5061 } },
      { "FILTER_RESTRICTIONS",                      { .i32 =  5062 } },
      { "REQUESTED_SERVICE_NOT_AUTHORIZED",         { .i32 =  5063 } },
      { "DUPLICATED_AF_SESSION",                    { .i32 =  5064 } },
      { "IP-CAN_SESSION_NOT_AVAILABLE",             { .i32 =  5065 } },
      { "UNAUTHORIZED_NON_EMERGENCY_SESSION",       { .i32 =  5066 } },
      { "UNAUTHORIZED_SPONSORED_DATA_CONNECTIVITY", { .i32 =  5067 } },
      { "TEMPORARY_NETWORK_FAILURE",                { .i32 =  5068 } }
    };

    CHECK_DICT_SEARCH( DICT_AVP, AVP_BY_NAME, "Experimental-Result-Code", &avp);
    CHECK_DICT_SEARCH( DICT_TYPE, TYPE_OF_AVP, avp, &avp_type );

    /* create enumerated values */
    for (ind = 0; ind < sizeof(data_enum)/sizeof(*data_enum); ++ind) {
      CHECK_DICT_NEW( DICT_ENUMVAL, &data_enum[ind], avp_type, NULL );
    }
  }

  /* NetLoc-Access-Support | 2824 | ETSI TS 129 212 V12.13.0 (2016-08)[5.3.11] | Enumerated | V | M | | | Y */
  {
    size_t ind;
    struct dict_type_data     data_type = { .type_base = AVP_TYPE_INTEGER32,.type_name = "Enumerated*(NetLoc-Access-Support)" };
    struct dict_object        *enum_type;
    struct dict_enumval_data  data_enum[] = {
      { "NETLOC_ACCESS_NOT_SUPPORTED",{ .i32 = 0 } }
    };
    struct dict_avp_data      data_avp = {
      2824,
      VENDOR_3GPP_ID,
      "NetLoc-Access-Support",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR,
      AVP_TYPE_INTEGER32
    };
    /* create enumerated type */
    CHECK_DICT_NEW(DICT_TYPE, &data_type, NULL, &enum_type);
    /* create enumerated values */
    for (ind = 0; ind < sizeof(data_enum) / sizeof(*data_enum); ++ind) {
      CHECK_DICT_NEW(DICT_ENUMVAL, &data_enum[ind], enum_type, NULL);
    }
    /* create avp */
    CHECK_DICT_NEW(DICT_AVP, &data_avp, enum_type, NULL);
  }

  /* RAT-Type | 1032 | ETSI TS 129 212 V12.13.0 (2016-08)[5.3.31] | Enumerated | V | M | | | Y */
  {
    size_t ind;
    struct dict_type_data     data_type = { .type_base = AVP_TYPE_INTEGER32,.type_name = "Enumerated*(RAT-Type)" };
    struct dict_object        *enum_type;
    struct dict_enumval_data  data_enum[] = {
      { "WLAN",           { .i32 = 0 } },
      { "VIRTUAL",        { .i32 = 1 } },
      { "UTRAN",          { .i32 = 1000 } },
      { "GERAN",          { .i32 = 1001 } },
      { "GAN",            { .i32 = 1002 } },
      { "HSPA_EVOLUTION", { .i32 = 1003 } },
      { "EUTRAN",         { .i32 = 1004 } },
      { "CDMA2000_1X",    { .i32 = 2000 } },
      { "HRPD",           { .i32 = 2001 } },
      { "UMB",            { .i32 = 2002 } },
      { "EHRPD",          { .i32 = 2003 } },
      { "EHRPD",{ .i32 = 2003 } }
    };
    struct dict_avp_data      data_avp = {
      1032,
      VENDOR_3GPP_ID,
      "RAT-Type",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR,
      AVP_TYPE_INTEGER32
    };
    /* create enumerated type */
    CHECK_DICT_NEW(DICT_TYPE, &data_type, NULL, &enum_type);
    /* create enumerated values */
    for (ind = 0; ind < sizeof(data_enum) / sizeof(*data_enum); ++ind) {
      CHECK_DICT_NEW(DICT_ENUMVAL, &data_enum[ind], enum_type, NULL);
    }
    /* create avp */
    CHECK_DICT_NEW(DICT_AVP, &data_avp, enum_type, NULL);
  }

  /* User-Location-Info-Time | 2812 | ETSI TS 129 212 V12.13.0 (2016-08)[5.3.101] | Time | V | M | | | Y */
  {
    struct dict_avp_data      data_avp = {
      2812,
      VENDOR_3GPP_ID,
      "User-Location-Info-Time",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_TYPE_OCTETSTRING
    };
    /* create avp */
    CHECK_DICT_NEW( DICT_AVP, &data_avp, Time_type, NULL );
  }

  /* RAN-NAS-Release-Cause | 2819 | ETSI TS 129 212 V12.13.0 (2016-08)[5.3.106] | Time | V | M | | | Y */
  {
    struct dict_avp_data      data_avp = {
      2819,
      VENDOR_3GPP_ID,
      "RAN-NAS-Release-Cause",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_TYPE_OCTETSTRING
    };
    /* create avp */
    CHECK_DICT_NEW(DICT_AVP, &data_avp, Time_type, NULL);
  }

  /* TWAN-Identifier | 29 | ETSI TS 129 061 | OctetString | V | M | | | Y */
  {
    struct dict_avp_data      data_avp = {
      29,
      VENDOR_3GPP_ID,
      "TWAN-Identifier",
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_FLAG_VENDOR | AVP_FLAG_MANDATORY,
      AVP_TYPE_OCTETSTRING
    };
    /* create avp */
    CHECK_DICT_NEW(DICT_AVP, &data_avp, Time_type, NULL);
  }

  /* grouped avp content section *********************************************/
  /*
    Access-Network-Charging-Identifier ::= < AVP Header: 502 >
    { Access-Network-Charging-Identifier-Value}
    *[ Flows ]
  */
  {
    struct dict_object *group_avp;
    struct dict_avp_request avp_req = { VENDOR_3GPP_ID, 0, "Access-Network-Charging-Identifier" };
    struct local_rules_definition avp_rules[] = {
      { { VENDOR_3GPP_ID, 0, "Access-Network-Charging-Identifier-Value" },  RULE_REQUIRED,  1,  1 },
      { { VENDOR_3GPP_ID, 0, "Flows" },                                     RULE_OPTIONAL, -1, -1 }
    };

    CHECK_DICT_SEARCH( DICT_AVP, AVP_BY_NAME_AND_VENDOR, &avp_req, &group_avp );
    PARSE_loc_rules (avp_rules, group_avp);
  }

  /*
    Acceptable-Service-Info::= < AVP Header: 526 >
    *[ Media-Component-Description ]
    [ Max-Requested-Bandwidth-DL ]
    [ Max-Requested-Bandwidth-UL ]
    *[ AVP ]
  */
  {
    struct dict_object *group_avp;
    struct dict_avp_request avp_req = { VENDOR_3GPP_ID, 0, "Acceptable-Service-Info" };
    struct local_rules_definition avp_rules[] = {
      { { VENDOR_3GPP_ID, 0, "Media-Component-Description" }, RULE_OPTIONAL, -1, -1 },
      { { VENDOR_3GPP_ID, 0, "Max-Requested-Bandwidth-DL" },  RULE_OPTIONAL, -1,  1 },
      { { VENDOR_3GPP_ID, 0, "Max-Requested-Bandwidth-UL" },  RULE_OPTIONAL, -1,  1 }
    };

    CHECK_DICT_SEARCH( DICT_AVP, AVP_BY_NAME_AND_VENDOR, &avp_req, &group_avp );
    PARSE_loc_rules (avp_rules, group_avp);
  }

  /*
    Flows::= < AVP Header: 510 >
    {Media-Component-Number}
    *[Flow-Number]
    [Final-Unit-Action]
  */
  {
    struct dict_object *group_avp;
    struct dict_avp_request avp_req = { VENDOR_3GPP_ID, 0, "Flows" };
    struct local_rules_definition avp_rules[] = {
      { { VENDOR_3GPP_ID, 0, "Media-Component-Number" },  RULE_REQUIRED,  1,  1 },
      { { VENDOR_3GPP_ID, 0, "Flow-Number" },             RULE_OPTIONAL, -1, -1 },
      { { VENDOR_DIAM_ID, 0, "Final-Unit-Action" },       RULE_OPTIONAL, -1,  1 }
    };

    CHECK_DICT_SEARCH( DICT_AVP, AVP_BY_NAME_AND_VENDOR, &avp_req, &group_avp );
    PARSE_loc_rules (avp_rules, group_avp);
  }

  /*
    Media-Component-Description ::= < AVP Header: 517 >
      { Media-Component-Number } ; Ordinal number of the media comp.
      *[ Media-Sub-Component ] ; Set of flows for one flow identifier
      [ AF-Application-Identifier ]
      [ Media-Type ]
      [ Max-Requested-Bandwidth-UL ]
      [ Max-Requested-Bandwidth-DL ]
      [ Min-Requested-Bandwidth-UL ]
      [ Min-Requested-Bandwidth-DL ]
      [ Flow-Status ]
      [ Reservation-Priority ]
      [ RS-Bandwidth ]
      [ RR-Bandwidth ]
      *[ Codec-Data ]
  */
  {
    struct dict_object *group_avp;
    struct dict_avp_request avp_req = { VENDOR_3GPP_ID, 0, "Media-Component-Description" };
    struct local_rules_definition avp_rules[] = {
      { { VENDOR_3GPP_ID, 0, "Media-Component-Number" },      RULE_REQUIRED,  1,  1 },
      { { VENDOR_3GPP_ID, 0, "Media-Sub-Component" },         RULE_OPTIONAL, -1, -1 },
      { { VENDOR_3GPP_ID, 0, "AF-Application-Identifier" },   RULE_OPTIONAL, -1,  1 },
      { { VENDOR_3GPP_ID, 0, "Media-Type" },                  RULE_OPTIONAL, -1,  1 },
      { { VENDOR_3GPP_ID, 0, "Max-Requested-Bandwidth-UL" },  RULE_OPTIONAL, -1,  1 },
      { { VENDOR_3GPP_ID, 0, "Max-Requested-Bandwidth-DL" },  RULE_OPTIONAL, -1,  1 },
      { { VENDOR_3GPP_ID, 0, "Min-Requested-Bandwidth-UL" },  RULE_OPTIONAL, -1,  1 },
      { { VENDOR_3GPP_ID, 0, "Min-Requested-Bandwidth-DL" },  RULE_OPTIONAL, -1,  1 },
      { { VENDOR_3GPP_ID, 0, "Flow-Status" },                 RULE_OPTIONAL, -1,  1 },
      { { VENDOR_3GPP_ID, 0, "Reservation-Priority" },        RULE_OPTIONAL, -1,  1 },
      { { VENDOR_3GPP_ID, 0, "RS-Bandwidth" },                RULE_OPTIONAL, -1,  1 },
      { { VENDOR_3GPP_ID, 0, "RR-Bandwidth" },                RULE_OPTIONAL, -1,  1 },
      { { VENDOR_3GPP_ID, 0, "Codec-Data" },                  RULE_OPTIONAL, -1, -1 }
    };

    CHECK_DICT_SEARCH( DICT_AVP, AVP_BY_NAME_AND_VENDOR, &avp_req, &group_avp );
    PARSE_loc_rules (avp_rules, group_avp);
  };

    /*
      Media-Sub-Component ::= < AVP Header: 519 >
        { Flow-Number } ; Ordinal number of the IP flow
        0*2[ Flow-Description ] ; UL and/or DL
        [ Flow-Status ]
        [ Flow-Usage ]
        [ Max-Requested-Bandwidth-UL ]
        [ Max-Requested-Bandwidth-DL ]
        [ AF-Signalling-Protocol ]
        *[ AVP ]
  */
  {
    struct dict_object *group_avp;
    struct dict_avp_request avp_req = { VENDOR_3GPP_ID, 0, "Media-Sub-Component" };
    struct local_rules_definition avp_rules[] = {
      { { VENDOR_3GPP_ID, 0, "Flow-Number" },                 RULE_REQUIRED,  1,  1 },
      { { VENDOR_3GPP_ID, 0, "Flow-Description" },            RULE_OPTIONAL, -1,  2 },
      { { VENDOR_3GPP_ID, 0, "Flow-Status" },                 RULE_OPTIONAL, -1,  1 },
      { { VENDOR_3GPP_ID, 0, "Flow-Usage" },                  RULE_OPTIONAL, -1,  1 },
      { { VENDOR_3GPP_ID, 0, "Max-Requested-Bandwidth-UL" },  RULE_OPTIONAL, -1,  1 },
      { { VENDOR_3GPP_ID, 0, "Max-Requested-Bandwidth-DL" },  RULE_OPTIONAL, -1,  1 },
      { { VENDOR_3GPP_ID, 0, "AF-Signalling-Protocol" },      RULE_OPTIONAL, -1,  1 }
    };

    CHECK_DICT_SEARCH( DICT_AVP, AVP_BY_NAME_AND_VENDOR, &avp_req, &group_avp );
    PARSE_loc_rules (avp_rules, group_avp);
  }

  /*
    Sponsored-Connectivity-Data::= < AVP Header: 530 >
      [ Sponsor-Identity ]
      [ Application-Service-Provider-Identity ]
      [ Granted-Service-Unit ]
      [ Used-Service-Unit ]
      *[ AVP ]
  */
  {
    struct dict_object *group_avp;
    struct dict_avp_request avp_req = { VENDOR_3GPP_ID, 0, "Sponsored-Connectivity-Data" };
    struct local_rules_definition avp_rules[] = {
      { { VENDOR_3GPP_ID, 0, "Sponsor-Identity" },                      RULE_OPTIONAL, -1,  1 },
      { { VENDOR_3GPP_ID, 0, "Application-Service-Provider-Identity" }, RULE_OPTIONAL, -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Granted-Service-Unit" },                  RULE_OPTIONAL, -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Used-Service-Unit" },                     RULE_OPTIONAL, -1,  1 }
    };

    CHECK_DICT_SEARCH( DICT_AVP, AVP_BY_NAME_AND_VENDOR, &avp_req, &group_avp );
    PARSE_loc_rules (avp_rules, group_avp);
  }

  /* command section */
  /* 5.6.1 AA-Request (AAR) command
    <AA-Request> ::= < Diameter Header: 265, REQ, PXY >
      < Session-Id >
      { Auth-Application-Id }
      { Origin-Host }
      { Origin-Realm }
      { Destination-Realm }
      [ Destination-Host ]
      [ IP-Domain-Id ]
      [ Auth-Session-State ]
      [ AF-Application-Identifier ]
      *[ Media-Component-Description ]
      [ Service-Info-Status ]
      [ AF-Charging-Identifier ]
      [ SIP-Forking-Indication ]
      *[ Specific-Action ]
      *[ Subscription-Id ]
      [ OC-Supported-Features ]
      *[ Supported-Features ]
      [ Reservation-Priority ]
      [ Framed-IP-Address ]
      [ Framed-IPv6-Prefix ]
      [ Called-Station-Id ]
      [ Service-URN ]
      [ Sponsored-Connectivity-Data ]
      [ MPS-Identifier ]
      [ GCS-Identifier ]
      [ Rx-Request-Type ]
      *[ Required-Access-Info ]
      [ Origin-State-Id ]
      *[ Proxy-Info ]
      *[ Route-Record ]
      *[ AVP ]
  */
  {
    struct dict_object *cmd;
    struct dict_cmd_data cmd_data = {
      265,
      "AA-Request",
      CMD_FLAG_REQUEST | CMD_FLAG_PROXIABLE | CMD_FLAG_ERROR,
      CMD_FLAG_REQUEST | CMD_FLAG_PROXIABLE
    };
    struct local_rules_definition cmd_rules[] = {
      { { VENDOR_DIAM_ID, 0, "Session-Id" },                  RULE_FIXED_HEAD,  1,  1 },
      { { VENDOR_DIAM_ID, 0, "Auth-Application-Id" },         RULE_REQUIRED,    1,  1 },
      { { VENDOR_DIAM_ID, 0, "Origin-Host" },                 RULE_REQUIRED,    1,  1 },
      { { VENDOR_DIAM_ID, 0, "Origin-Realm" },                RULE_REQUIRED,    1,  1 },
      { { VENDOR_DIAM_ID, 0, "Destination-Realm" },           RULE_REQUIRED,    1,  1 },
      { { VENDOR_DIAM_ID, 0, "Destination-Host" },            RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "IP-Domain-Id" },                RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Auth-Session-State" },          RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "AF-Application-Identifier" },   RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "Media-Component-Description" }, RULE_OPTIONAL,   -1, -1 },
      { { VENDOR_3GPP_ID, 0, "Service-Info-Status" },         RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "AF-Charging-Identifier" },      RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "SIP-Forking-Indication" },      RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "Specific-Action" },             RULE_OPTIONAL,   -1, -1 },
      { { VENDOR_DIAM_ID, 0, "Subscription-Id" },             RULE_OPTIONAL,   -1, -1 },
      { { VENDOR_DIAM_ID, 0, "OC-Supported-Features" },       RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "Supported-Features" },          RULE_OPTIONAL,   -1, -1 },
      { { VENDOR_3GPP_ID, 0, "Reservation-Priority" },        RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Framed-IP-Address" },           RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Framed-IPv6-Prefix" },          RULE_OPTIONAL,   -1, -1 },
      { { VENDOR_DIAM_ID, 0, "Called-Station-Id" },           RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "Service-URN" },                 RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "Sponsored-Connectivity-Data" }, RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "MPS-Identifier" },              RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "GCS-Identifier" },              RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "Rx-Request-Type" },             RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "Required-Access-Info" },        RULE_OPTIONAL,   -1, -1 },
      { { VENDOR_DIAM_ID, 0, "Origin-State-Id" },             RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Proxy-Info" },                  RULE_OPTIONAL,   -1, -1 },
      { { VENDOR_DIAM_ID, 0, "Route-Record" },                RULE_OPTIONAL,   -1, -1 }
    };

    CHECK_DICT_NEW( DICT_COMMAND, &cmd_data, psoRxApp, &cmd );
    PARSE_loc_rules( cmd_rules, cmd );
    REMOVE_RULE(274, VENDOR_DIAM_ID,cmd);
  }

  /* 5.6.2 AA-Answer (AAA) command
    <AA-Answer> ::= < Diameter Header: 265, PXY >
      < Session-Id >
      { Auth-Application-Id }
      { Origin-Host }
      { Origin-Realm }
      [ Result-Code ]
      [ Experimental-Result ]
      [ Auth-Session-State ]
      *[ Access-Network-Charging-Identifier ]
      [ Access-Network-Charging-Address ]
      [ Acceptable-Service-Info ]
      [ IP-CAN-Type ]
      [ NetLoc-Access-Support ]
      [ RAT-Type ]
      *[ Flows ]
      [ OC-Supported-Features ]
      [ OC-OLR ]
      *[ Supported-Features ]
      *[ Class ]
      [ Error-Message ]
      [ Error-Reporting-Host ]
      *[ Failed-AVP ]
      [ Origin-State-Id ]
      *[ Redirect-Host ]
      [ Redirect-Host-Usage ]
      [ Redirect-Max-Cache-Time ]
      *[ Proxy-Info ]
      *[ AVP ]
  */
  {
    struct dict_object *cmd;
    struct dict_cmd_data cmd_data = {
      265,
      "AA-Answer",
      CMD_FLAG_REQUEST | CMD_FLAG_PROXIABLE,
      CMD_FLAG_PROXIABLE
    };
    struct local_rules_definition cmd_rules[] = {
      { { VENDOR_DIAM_ID, 0, "Session-Id" },                          RULE_FIXED_HEAD,  1,  1 },
      { { VENDOR_DIAM_ID, 0, "Auth-Application-Id" },                 RULE_REQUIRED,    1,  1 },
      { { VENDOR_DIAM_ID, 0, "Origin-Host" },                         RULE_REQUIRED,    1,  1 },
      { { VENDOR_DIAM_ID, 0, "Origin-Realm" },                        RULE_REQUIRED,    1,  1 },
      { { VENDOR_DIAM_ID, 0, "Result-Code" },                         RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Experimental-Result" },                 RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Auth-Session-State" },                  RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "Access-Network-Charging-Identifier" },  RULE_OPTIONAL,   -1, -1 },
      { { VENDOR_3GPP_ID, 0, "Access-Network-Charging-Address" },     RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "Acceptable-Service-Info" },             RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "IP-CAN-Type" },                         RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "NetLoc-Access-Support" },               RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "RAT-Type" },                            RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "Flows" },                               RULE_OPTIONAL,   -1, -1 },
      { { VENDOR_DIAM_ID, 0, "OC-Supported-Features" },               RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "OC-OLR" },                              RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "Supported-Features" },                  RULE_OPTIONAL,   -1, -1 },
      { { VENDOR_DIAM_ID, 0, "Class" },                               RULE_OPTIONAL,   -1, -1 },
      { { VENDOR_DIAM_ID, 0, "Error-Message" },                       RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Error-Reporting-Host" },                RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Failed-AVP" },                          RULE_OPTIONAL,   -1, -1 },
      { { VENDOR_DIAM_ID, 0, "Origin-State-Id" },                     RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Redirect-Host" },                       RULE_OPTIONAL,   -1, -1 },
      { { VENDOR_DIAM_ID, 0, "Redirect-Host-Usage" },                 RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Redirect-Max-Cache-Time" },             RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Proxy-Info" },                          RULE_OPTIONAL,   -1, -1 }
    };

    CHECK_DICT_NEW( DICT_COMMAND, &cmd_data, psoRxApp, &cmd );
    PARSE_loc_rules (cmd_rules, cmd);
  }

  /* 5.6.3 Re-Auth-Request (RAR) command */
  /*
    <RA-Request> ::= < Diameter Header: 258, REQ, PXY >
      < Session-Id >
      { Origin-Host }
      { Origin-Realm }
      { Destination-Realm }
      { Destination-Host }
      { Auth-Application-Id }
      *{ Specific-Action }
      [ OC-Supported-Features ]
      *[ Access-Network-Charging-Identifier ]
      [ Access-Network-Charging-Address ]
      *[ Flows ]
      *[ Subscription-Id ]
      [ Abort-Cause ]
      [ IP-CAN-Type ]
      [ NetLoc-Access-Support ]
      [ RAT-Type ]
      [ Sponsored-Connectivity-Data ]
      [ 3GPP-User-Location-Info ]
      [ User-Location-Info-Time ]
      [ 3GPP-MS-TimeZone ]
      *[ RAN-NAS-Release-Cause ]
      [ 3GPP-SGSN-MCC-MNC ]
      [ TWAN-Identifier ]
      [ Origin-State-Id ]
      *[ Class ]
      *[ Proxy-Info ]
      *[ Route-Record ]
      *[ AVP ]
  */
  {
    struct dict_object *cmd;
    struct dict_cmd_data cmd_data = {
      258,
      "Re-Auth-Request",
      CMD_FLAG_REQUEST | CMD_FLAG_PROXIABLE | CMD_FLAG_ERROR,
      CMD_FLAG_REQUEST | CMD_FLAG_PROXIABLE
    };
    struct local_rules_definition cmd_rules[] = {
      { { VENDOR_DIAM_ID, 0, "Session-Id" },                          RULE_FIXED_HEAD,  1,  1 },
      { { VENDOR_DIAM_ID, 0, "Origin-Host" },                         RULE_REQUIRED,    1,  1 },
      { { VENDOR_DIAM_ID, 0, "Origin-Realm" },                        RULE_REQUIRED,    1,  1 },
      { { VENDOR_DIAM_ID, 0, "Destination-Realm" },                   RULE_REQUIRED,    1,  1 },
      { { VENDOR_DIAM_ID, 0, "Destination-Host" },                    RULE_REQUIRED,    1,  1 },
      { { VENDOR_DIAM_ID, 0, "Auth-Application-Id" },                 RULE_REQUIRED,    1,  1 },
      { { VENDOR_3GPP_ID, 0, "Specific-Action" },                     RULE_REQUIRED,    1, -1 },
      { { VENDOR_DIAM_ID, 0, "OC-Supported-Features" },               RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "Access-Network-Charging-Identifier" },  RULE_OPTIONAL,   -1, -1 },
      { { VENDOR_3GPP_ID, 0, "Access-Network-Charging-Address" },     RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "Flows" },                               RULE_OPTIONAL,   -1, -1 },
      { { VENDOR_DIAM_ID, 0, "Subscription-Id" },                     RULE_OPTIONAL,   -1, -1 },
      { { VENDOR_3GPP_ID, 0, "Abort-Cause" },                         RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "IP-CAN-Type" },                         RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "NetLoc-Access-Support" },               RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "RAT-Type" },                            RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "Sponsored-Connectivity-Data" },         RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "3GPP-User-Location-Info" },             RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "User-Location-Info-Time" },             RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "3GPP-MS-TimeZone" },                    RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "RAN-NAS-Release-Cause" },               RULE_OPTIONAL,   -1, -1 },
      { { VENDOR_3GPP_ID, 0, "3GPP-SGSN-MCC-MNC" },                   RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "TWAN-Identifier" },                     RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Origin-State-Id" },                     RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Class" },                               RULE_OPTIONAL,   -1, -1 },
      { { VENDOR_DIAM_ID, 0, "Proxy-Info" },                          RULE_OPTIONAL,   -1, -1 },
      { { VENDOR_DIAM_ID, 0, "Route-Record" },                        RULE_OPTIONAL,   -1, -1 }
    };

    CHECK_DICT_NEW( DICT_COMMAND, &cmd_data, psoRxApp, &cmd );
    PARSE_loc_rules (cmd_rules, cmd);
  }

  /* 5.6.4 Re-Auth-Answer (RAA) command */
  /*
    <RA-Answer> ::= < Diameter Header: 258, PXY >
      < Session-Id >
      { Origin-Host }
      { Origin-Realm }
      [ Result-Code ]
      [ Experimental-Result ]
      [ OC-Supported-Features ]
      [ OC-OLR ]
      *[ Media-Component-Description ]
      [ Service-URN ]
      [ Origin-State-Id ]
      *[ Class ]
      [ Error-Message ]
      [ Error-Reporting-Host ]
      *[ Redirect-Host ]
      [ Redirect-Host-Usage ]
      [ Redirect-Max-Cache-Time ]
      *[ Failed-AVP ]
      *[ Proxy-Info ]
      *[ AVP ]
  */
  {
    struct dict_object *cmd;
    struct dict_cmd_data cmd_data = {
      258,
      "Re-Auth-Answer",
      CMD_FLAG_REQUEST | CMD_FLAG_PROXIABLE,
      CMD_FLAG_PROXIABLE
    };
    struct local_rules_definition cmd_rules[] = {
      { { VENDOR_DIAM_ID, 0, "Session-Id" },                  RULE_FIXED_HEAD,  1,  1 },
      { { VENDOR_DIAM_ID, 0, "Origin-Host" },                 RULE_REQUIRED,    1,  1 },
      { { VENDOR_DIAM_ID, 0, "Origin-Realm" },                RULE_REQUIRED,    1,  1 },
      { { VENDOR_DIAM_ID, 0, "Result-Code" },                 RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Experimental-Result" },         RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "OC-Supported-Features" },       RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "OC-OLR" },                      RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "Media-Component-Description" }, RULE_OPTIONAL,   -1, -1 },
      { { VENDOR_3GPP_ID, 0, "Service-URN" },                 RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Origin-State-Id" },             RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Class" },                       RULE_OPTIONAL,   -1, -1 },
      { { VENDOR_DIAM_ID, 0, "Error-Message" },               RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Error-Reporting-Host" },        RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Redirect-Host" },               RULE_OPTIONAL,   -1, -1 },
      { { VENDOR_DIAM_ID, 0, "Redirect-Host-Usage" },         RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Redirect-Max-Cache-Time" },     RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Failed-AVP" },                  RULE_OPTIONAL,   -1, -1 },
      { { VENDOR_DIAM_ID, 0, "Proxy-Info" },                  RULE_OPTIONAL,   -1, -1 }
    };

    CHECK_DICT_NEW( DICT_COMMAND, &cmd_data, psoRxApp, &cmd );
    PARSE_loc_rules (cmd_rules, cmd);
  }

  /* 5.6.5 Session-Termination-Request (STR) command */
  /*
    <ST-Request> ::= < Diameter Header: 275, REQ, PXY >
      < Session-Id >
      { Origin-Host }
      { Origin-Realm }
      { Destination-Realm }
      { Auth-Application-Id }
      { Termination-Cause }
      [ Destination-Host ]
      [ OC-Supported-Features ]
      *[ Required-Access-Info ]
      *[ Class ]
      [ Origin-State-Id ]
      *[ Proxy-Info ]
      *[ Route-Record ]
      *[ AVP ]
  */
  {
    struct dict_object *cmd;
    struct dict_cmd_data cmd_data = {
      275,
      "Session-Termination-Request",
      CMD_FLAG_REQUEST | CMD_FLAG_PROXIABLE | CMD_FLAG_ERROR,
      CMD_FLAG_REQUEST | CMD_FLAG_PROXIABLE
    };
    struct local_rules_definition cmd_rules[] = {
      { { VENDOR_DIAM_ID, 0, "Session-Id" },                          RULE_FIXED_HEAD,  1,  1 },
      { { VENDOR_DIAM_ID, 0, "Origin-Host" },                         RULE_REQUIRED,    1,  1 },
      { { VENDOR_DIAM_ID, 0, "Origin-Realm" },                        RULE_REQUIRED,    1,  1 },
      { { VENDOR_DIAM_ID, 0, "Destination-Realm" },                   RULE_REQUIRED,    1,  1 },
      { { VENDOR_DIAM_ID, 0, "Auth-Application-Id" },                 RULE_REQUIRED,    1,  1 },
      { { VENDOR_DIAM_ID, 0, "Termination-Cause" },                   RULE_REQUIRED,    1,  1 },
      { { VENDOR_DIAM_ID, 0, "Destination-Host" },                    RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "OC-Supported-Features" },               RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "Required-Access-Info" },                RULE_OPTIONAL,   -1, -1 },
      { { VENDOR_DIAM_ID, 0, "Class" },                               RULE_OPTIONAL,   -1, -1 },
      { { VENDOR_DIAM_ID, 0, "Origin-State-Id" },                     RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Proxy-Info" },                          RULE_OPTIONAL,   -1, -1 },
      { { VENDOR_DIAM_ID, 0, "Route-Record" },                        RULE_OPTIONAL,   -1, -1 }
    };

    CHECK_DICT_NEW( DICT_COMMAND, &cmd_data, psoRxApp, &cmd );
    PARSE_loc_rules (cmd_rules, cmd);
  }

  /* 5.6.6 Session-Termination-Answer (STA) command */
  /*
    <ST-Answer> ::= < Diameter Header: 275, PXY >
      < Session-Id >
      { Origin-Host }
      { Origin-Realm }
      [ Result-Code ]
      [ Error-Message ]
      [ Error-Reporting-Host ]
      [ OC-Supported-Features ]
      [ OC-OLR ]
      [ Failed-AVP ]
      [ Sponsored-Connectivity-Data ]
      [ Origin-State-Id ]
      [ 3GPP-User-Location-Info ]
      [ User-Location-Info-Time ]
      [ 3GPP-MS-TimeZone ]
      *[ RAN-NAS-Release-Cause ]
      [ 3GPP-SGSN-MCC-MNC ]
      [ TWAN-Identifier ]
      [ NetLoc-Access-Support ]
      *[ Class ]
      *[ Redirect-Host ]
      [ Redirect-Host-Usage ]
      [ Redirect-Max-Cache-Time ]
      *[ Proxy-Info ]
      *[ AVP ]
  */
  {
    struct dict_object *cmd;
    struct dict_cmd_data cmd_data = {
      275,
      "Session-Termination-Answer",
      CMD_FLAG_REQUEST | CMD_FLAG_PROXIABLE,
      CMD_FLAG_PROXIABLE
    };
    struct local_rules_definition cmd_rules[] = {
      { { VENDOR_DIAM_ID, 0, "Session-Id" },                  RULE_FIXED_HEAD,  1,  1 },
      { { VENDOR_DIAM_ID, 0, "Origin-Host" },                 RULE_REQUIRED,    1,  1 },
      { { VENDOR_DIAM_ID, 0, "Origin-Realm" },                RULE_REQUIRED,    1,  1 },
      { { VENDOR_DIAM_ID, 0, "Result-Code" },                 RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Error-Message" },               RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Error-Reporting-Host" },        RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "OC-Supported-Features" },       RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "OC-OLR" },                      RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Failed-AVP" },                  RULE_OPTIONAL,   -1, -1 },
      { { VENDOR_3GPP_ID, 0, "Sponsored-Connectivity-Data" }, RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Origin-State-Id" },             RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "3GPP-User-Location-Info" },     RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "User-Location-Info-Time" },     RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "3GPP-MS-TimeZone" },            RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "RAN-NAS-Release-Cause" },       RULE_OPTIONAL,   -1, -1 },
      { { VENDOR_3GPP_ID, 0, "3GPP-SGSN-MCC-MNC" },           RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "TWAN-Identifier" },             RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "NetLoc-Access-Support" },       RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Class" },                       RULE_OPTIONAL,   -1, -1 },
      { { VENDOR_DIAM_ID, 0, "Redirect-Host" },               RULE_OPTIONAL,   -1, -1 },
      { { VENDOR_DIAM_ID, 0, "Redirect-Host-Usage" },         RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Redirect-Max-Cache-Time" },     RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Proxy-Info" },                  RULE_OPTIONAL,   -1, -1 }
    };

    CHECK_DICT_NEW( DICT_COMMAND, &cmd_data, psoRxApp, &cmd );
    PARSE_loc_rules (cmd_rules, cmd);
  }

  /* 5.6.7 Abort-Session-Request (ASR) command */
  /*
    <AS-Request> ::= < Diameter Header: 274, REQ, PXY >
      < Session-Id >
      { Origin-Host }
      { Origin-Realm }
      { Destination-Realm }
      { Destination-Host }
      { Auth-Application-Id }
      [ OC-Supported-Features ]
      { Abort-Cause }
      [ Origin-State-Id ]
      *[ Proxy-Info ]
      *[ Route-Record ]
      *[ AVP ]
  */
  {
    struct dict_object *cmd;
    struct dict_cmd_data cmd_data = {
      274,
      "Abort-Session-Request",
      CMD_FLAG_REQUEST | CMD_FLAG_PROXIABLE | CMD_FLAG_ERROR,
      CMD_FLAG_REQUEST | CMD_FLAG_PROXIABLE
    };
    struct local_rules_definition cmd_rules[] = {
      { { VENDOR_DIAM_ID, 0, "Session-Id" },            RULE_FIXED_HEAD,  1,  1 },
      { { VENDOR_DIAM_ID, 0, "Origin-Host" },           RULE_REQUIRED,    1,  1 },
      { { VENDOR_DIAM_ID, 0, "Origin-Realm" },          RULE_REQUIRED,    1,  1 },
      { { VENDOR_DIAM_ID, 0, "Destination-Realm" },     RULE_REQUIRED,    1,  1 },
      { { VENDOR_DIAM_ID, 0, "Destination-Host" },      RULE_REQUIRED,    1,  1 },
      { { VENDOR_DIAM_ID, 0, "Auth-Application-Id" },   RULE_REQUIRED,    1,  1 },
      { { VENDOR_DIAM_ID, 0, "OC-Supported-Features" }, RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_3GPP_ID, 0, "Abort-Cause" },           RULE_REQUIRED,    1,  1 },
      { { VENDOR_DIAM_ID, 0, "Origin-State-Id" },       RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Proxy-Info" },            RULE_OPTIONAL,   -1, -1 },
      { { VENDOR_DIAM_ID, 0, "Route-Record" },          RULE_OPTIONAL,   -1, -1 }
    };

    CHECK_DICT_NEW( DICT_COMMAND, &cmd_data, psoRxApp, &cmd );
    PARSE_loc_rules (cmd_rules, cmd);
  }

  /* 5.6.8 Abort-Session-Answer (ASA) command */
  /*
    <AS-Answer> ::= < Diameter Header: 274, PXY >
      < Session-Id >
      { Origin-Host }
      { Origin-Realm }
      [ Result-Code ]
      [ OC-Supported-Features ]
      [ OC-OLR ]
      [ Origin-State-Id ]
      [ Error-Message ]
      [ Error-Reporting-Host ]
      *[ Failed-AVP ]
      *[ Redirect-Host ]
      [ Redirect-Host-Usage ]
      [ Redirect-Max-Cache-Time ]
      *[ Proxy-Info ]
      *[ AVP ]
  */
  {
    struct dict_object *cmd;
    struct dict_cmd_data cmd_data = {
      274,
      "Abort-Session-Answer",
      CMD_FLAG_REQUEST | CMD_FLAG_PROXIABLE,
      CMD_FLAG_PROXIABLE
    };
    struct local_rules_definition cmd_rules[] = {
      { { VENDOR_DIAM_ID, 0, "Session-Id" },              RULE_FIXED_HEAD,  1,  1 },
      { { VENDOR_DIAM_ID, 0, "Origin-Host" },             RULE_REQUIRED,    1,  1 },
      { { VENDOR_DIAM_ID, 0, "Origin-Realm" },            RULE_REQUIRED,    1,  1 },
      { { VENDOR_DIAM_ID, 0, "Result-Code" },             RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "OC-Supported-Features" },   RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "OC-OLR" },                  RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Origin-State-Id" },         RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Error-Message" },           RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Error-Reporting-Host" },    RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Failed-AVP" },              RULE_OPTIONAL,   -1, -1 },
      { { VENDOR_DIAM_ID, 0, "Redirect-Host" },           RULE_OPTIONAL,   -1, -1 },
      { { VENDOR_DIAM_ID, 0, "Redirect-Host-Usage" },     RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Redirect-Max-Cache-Time" }, RULE_OPTIONAL,   -1,  1 },
      { { VENDOR_DIAM_ID, 0, "Proxy-Info" },              RULE_OPTIONAL,   -1, -1 }
    };

    CHECK_DICT_NEW( DICT_COMMAND, &cmd_data, psoRxApp, &cmd );
    PARSE_loc_rules (cmd_rules, cmd);
  }

  return 0;
}

EXTENSION_ENTRY( "dict_rx", dict_rx_entry );
