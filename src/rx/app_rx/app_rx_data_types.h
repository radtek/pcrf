#ifndef _APP_RX_DATA_TYPES_H_
#define _APP_RX_DATA_TYPES_H_

#include <freeDiameter/extension.h>

#include <string>
#include <vector>

#define OTL_STL
#define OTL_ORA11G
#include "utils/otlv4.h"

/*
  Media-Sub-Component ::= < AVP Header: 519 >
    { Flow-Number } ; Ordinal number of the IP flow
    0*2[ Flow-Description ] ; UL and/or DL
    [ Flow-Status ]
    [ Flow-Usage ]
    [ Max-Requested-Bandwidth-UL ]
    [ Max-Requested-Bandwidth-DL ]
    [ AF-Signalling-Protocol ]
*/
struct SMSC {
  otl_value<uint32_t> m_coFlowNumber;
  otl_value<std::string> m_mcoFlowDescription[2];
  otl_value<int32_t> m_coFlowStatus;
  otl_value<int32_t> m_coFlowUsage;
  otl_value<uint32_t> m_coMaxRequestedBandwidthUL;
  otl_value<uint32_t> m_coMaxRequestedBandwidthDL;
  otl_value<int32_t> m_coAFSignallingProtocol;
};

/*
  Media-Component-Description ::= < AVP Header: 517 >
    { Media-Component-Number }  ; Ordinal number of the media comp.
    *[ Media-Sub-Component ]    ; Set of flows for one flow identifier
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
struct SMCD {
  otl_value<uint32_t> m_coMediaComponentNumber;
  std::vector<SMSC> m_vectMediaSubComponent;
  otl_value<std::string> m_coAFApplicationIdentifier;
  otl_value<int32_t> m_coMediaType;
  otl_value<std::string> m_coMediaTypeEnum;
  otl_value<uint32_t> m_coMaxRequestedBandwidthUL;
  otl_value<uint32_t> m_coMaxRequestedBandwidthDL;
  otl_value<uint32_t> m_coMinRequestedBandwidthUL;
  otl_value<uint32_t> m_coMinRequestedBandwidthDL;
  otl_value<int32_t> m_coFlowStatus;
  otl_value<int32_t> m_coReservationPriority;
  otl_value<uint32_t> m_coRSBandwidth;
  otl_value<uint32_t> m_coRRBandwidth;
  std::vector<std::string> m_vectCodecData;
};

/*
  Subscription-Id ::= < AVP Header: 443 >
    { Subscription-Id-Type }
    { Subscription-Id-Data }
*/
struct SSId {
  otl_value<int32_t> m_coSubscriptionIdType;
  otl_value<std::string> m_coSubscriptionIdData;
};

/*
  OC-Supported-Features ::= < AVP Header: 621 >
    [ OC-Feature-Vector ]
*/
struct SOCSF {
  otl_value<uint64_t> m_coOCFeatureVector;
};

/*
  Supported-Features ::= < AVP header: 628 10415 >
    { Vendor-Id }
    { Feature-List-ID }
    { Feature-List }
*/
struct SSF {
  otl_value<uint32_t> m_coVendorId;
  otl_value<uint32_t> m_coFeatureListID;
  otl_value<uint32_t> m_coFeatureList;
};

struct SFramedIPAddress {
  union {
    uint32_t m_uiAddr;
    struct {
      unsigned char b1;
      unsigned char b2;
      unsigned char b3;
      unsigned char b4;
    } m_soAddr;
  } m_uAddr;
};

/*
  Unit-Value ::= < AVP Header: 445 >
    { Value-Digits }
    [ Exponent ]
*/
struct SUnitValue {
  otl_value<int64_t> m_coValueDigits;
  otl_value<int32_t> m_coExponent;
};

/*
  CC-Money ::= < AVP Header: 413 >
    { Unit-Value }
    [ Currency-Code ]
*/
struct SCCMoney {
  otl_value<SUnitValue> m_coUnitValue;
  otl_value<uint32_t> m_coCurrencyCode;
};

struct SSU {
  otl_value<uint32_t> m_coCCTime;
  otl_value<SCCMoney> m_coCCMoney;
  otl_value<uint64_t> m_coCCTotalOctets;
  otl_value<uint64_t> m_coCCInputOctets;
  otl_value<uint64_t> m_coCCOutputOctets;
  otl_value<uint64_t> m_coCCServiceSpecificUnits;
};

/*
  Granted-Service-Unit ::= < AVP Header: 431 >
    [ Tariff-Time-Change ]
    [ CC-Time ]
    [ CC-Money ]
    [ CC-Total-Octets ]
    [ CC-Input-Octets ]
    [ CC-Output-Octets ]
    [ CC-Service-Specific-Units ]
*/
struct SGSU {
  otl_value<uint32_t> m_coTariffTimeChange;
  SSU m_soServiceUnit;
};

/*
  Used-Service-Unit ::= < AVP Header: 446 >
    [ Tariff-Change-Usage ]
    [ CC-Time ]
    [ CC-Money ]
    [ CC-Total-Octets ]
    [ CC-Input-Octets ]
    [ CC-Output-Octets ]
    [ CC-Service-Specific-Units ]
*/
struct SUSU {
  otl_value<uint32_t> m_coTariffChangeUsage;
  SSU m_soServiceUnit;
};

/*
  Sponsored-Connectivity-Data::= < AVP Header: 530 >
    [ Sponsor-Identity ]
    [ Application-Service-Provider-Identity ]
    [ Granted-Service-Unit ]
    [ Used-Service-Unit ]
*/
struct SSCD {
  otl_value<std::string> m_coSponsorIdentity;
  otl_value<std::string> m_coApplicationServiceProviderIdentity;
  otl_value<SGSU> m_coGrantedServiceUnit;
  otl_value<SUSU> m_coUsedServiceUnit;
};

/*
  Proxy-Info ::= < AVP Header: 284 >
    { Proxy-Host }
    { Proxy-State }
*/
struct SProxyInfo {
  otl_value<std::string> m_coProxyHost;
  otl_value<std::string> m_coProxyState;
};


/*
  Flows::= < AVP Header: 510 >
    {Media-Component-Number}
    *[Flow-Number]
    [Final-Unit-Action]
*/
struct SFlows {
  otl_value<uint32_t> m_coMediaComponentNumber;
  std::vector<uint32_t> m_vectFlowNumber;
  otl_value<int32_t> m_coFinalUnitAction;
};

/*
  Access-Network-Charging-Identifier ::= < AVP Header: 502 >
    { Access-Network-Charging-Identifier-Value}
    *[ Flows ]
*/
struct SANCI {
  otl_value<std::string> m_coAccessNetworkChargingIdentifierValue;
  std::vector<SFlows> m_coFlows;
};

/*
  Acceptable-Service-Info::= < AVP Header: 526 >
    *[ Media-Component-Description]
    [ Max-Requested-Bandwidth-DL ]
    [ Max-Requested-Bandwidth-UL ]
*/
struct SASI {
  std::vector<SMCD> m_vectMediaComponentDescription;
  otl_value<uint32_t> m_coMaxRequestedBandwidthDL;
  otl_value<uint32_t> m_coMaxRequestedBandwidthUL;
};

/*
  OC-OLR ::= < AVP Header: 623 >
    < OC-Sequence-Number >
    < OC-Report-Type >
    [ OC-Reduction-Percentage ]
    [ OC-Validity-Duration ]
*/
struct SOCOLR {
  otl_value<uint64_t> m_coOCSequenceNumber;
  otl_value<int32_t> m_coOCReportType;
  otl_value<uint32_t> m_coOCReductionPercentage;
  otl_value<uint32_t> m_coOCValidityDuration;
};

/*
  <Failed-AVP> ::= < AVP Header: 279 >
    1* {AVP}
*/
struct SFailedAVP {
  std::vector<avp_value> m_vectAVP;
};


/*
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
    [ Framed-Ipv6-Prefix ]
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
*/
struct SAAR {
  otl_value<std::string>      m_coSessionId;
  otl_value<uint32_t>         m_coAuthApplicationId;
  otl_value<std::string>      m_coOriginHost;
  otl_value<std::string>      m_coOriginRealm;
  otl_value<std::string>      m_coDestinationRealm;
  otl_value<std::string>      m_coDestinationHost;
  otl_value<std::string>      m_coIPDomainId;
  otl_value<int32_t>          m_coAuthSessionState;
  otl_value<std::string>      m_coAFApplicationIdentifier;
  std::vector<SMCD>           m_vectMediaComponentDescription;
  otl_value<int32_t>          m_coServiceInfoStatus;
  otl_value<std::string>      m_coAFChargingIdentifier;
  otl_value<int32_t>          m_coSIPForkingIndication;
  std::vector<int32_t>        m_vectSpecificAction;
  std::vector<SSId>           m_vectSubscriptionId;
  otl_value<SOCSF>            m_coOCSupportedFeatures;
  std::vector<SSF>            m_vectSupportedFeatures;
  otl_value<int32_t>          m_coReservationPriority;
  otl_value<SFramedIPAddress> m_coFramedIPAddress;
  otl_value<std::string>      m_coFramedIpv6Prefix;
  otl_value<std::string>      m_coCalledStationId;
  otl_value<std::string>      m_coServiceURN;
  otl_value<SSCD>             m_coSponsoredConnectivityData;
  otl_value<std::string>      m_coMPSIdentifier;
  otl_value<std::string>      m_coGCSIdentifier;
  otl_value<int32_t>          m_coRxRequestType;
  std::vector<int32_t>        m_vectRequiredAccessInfo;
  otl_value<uint32_t>         m_coOriginStateId;
  std::vector<SProxyInfo>     m_vectProxyInfo;
  std::vector<std::string>    m_vectRouteRecord;
};

/*
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
*/
struct SAAA {
  otl_value<std::string> m_coSessionId;
  otl_value<uint32_t> m_coAuthApplicationId;
  otl_value<std::string> m_coOriginHost;
  otl_value<std::string> m_coOriginRealm;
  otl_value<uint32_t> m_coResultCode;
  otl_value<uint32_t> m_coExperimentalResult;
  otl_value<int32_t> m_coAuthSessionState;
  std::vector<SANCI> m_vectAccessNetworkChargingIdentifier;
  otl_value<std::string> m_coAccessNetworkChargingAddress;
  otl_value<SASI> m_coAcceptableServiceInfo;
  otl_value<int32_t> m_coIPCANType;
  otl_value<uint32_t> m_coNetLocAccessSupport;
  otl_value<int32_t> m_coRATType;
  std::vector<SFlows> m_vectFlows;
  otl_value<uint64_t> m_coOCSupportedFeatures;
  otl_value<SOCOLR> m_coOCOLR;
  std::vector<SSF> m_vectSupportedFeatures;
  otl_value<std::string> m_coClass;
  otl_value<std::string> m_coErrorMessage;
  otl_value<std::string> m_coErrorReportingHost;
  otl_value<SFailedAVP> m_coFailedAVP;
  otl_value<uint32_t> m_coOriginStateId;
  std::vector<std::string> m_vectRedirectHost;
  otl_value<int32_t> m_coRedirectHostUsage;
  otl_value<uint32_t> m_coRedirectMaxCacheTime;
  std::vector<SProxyInfo> m_vectProxyInfo;
};

/*  RAR
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
  [ Framed-Ipv6-Prefix ]
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
*/


/* RAA
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
*/
struct SRAA {
  otl_value<std::string> m_coSessionId;
  otl_value<uint32_t> m_coAuthApplicationId;
  otl_value<std::string> m_coOriginHost;
  otl_value<std::string> m_coOriginRealm;
  otl_value<uint32_t> m_coResultCode;
  otl_value<uint32_t> m_coExperimentalResult;
  otl_value<int32_t> m_coAuthSessionState;
  std::vector<SANCI> m_vectAccessNetworkChargingIdentifier;
  otl_value<std::string> m_coAccessNetworkChargingAddress;
  otl_value<SASI> m_coAcceptableServiceInfo;
  otl_value<int32_t> m_coIPCANType;
  otl_value<uint32_t> m_coNetLocAccessSupport;
  otl_value<int32_t> m_coRATType;
  std::vector<SFlows> m_vectFlows;
  otl_value<uint64_t> m_coOCSupportedFeatures;
  otl_value<SOCOLR> m_coOCOLR;
  std::vector<SSF> m_vectSupportedFeatures;
  otl_value<std::string> m_coClass;
  otl_value<std::string> m_coErrorMessage;
  otl_value<std::string> m_coErrorReportingHost;
  otl_value<SFailedAVP> m_coFailedAVP;
  otl_value<uint32_t> m_coOriginStateId;
  std::vector<std::string> m_vectRedirectHost;
  otl_value<int32_t> m_coRedirectHostUsage;
  otl_value<uint32_t> m_coRedirectMaxCacheTime;
  std::vector<SProxyInfo> m_vectProxyInfo;
};

/* получение именованного значения константы */
void app_rx_get_enum_val (vendor_id_t p_tVendId, avp_code_t p_tAVPCode, int32_t p_iVal, otl_value<std::string> &p_coValue);

#endif /* _APP_RX_DATA_TYPES_H_ */
