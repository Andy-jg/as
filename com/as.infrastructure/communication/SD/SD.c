/*-------------------------------- Arctic Core ------------------------------
 * Copyright (C) 2013, ArcCore AB, Sweden, www.arccore.com.
 * Contact: <contact@arccore.com>
 *
 * You may ONLY use this file:
 * 1)if you have a valid commercial ArcCore license and then in accordance with
 * the terms contained in the written license agreement between you and ArcCore,
 * or alternatively
 * 2)if you follow the terms found in GNU General Public License version 2 as
 * published by the Free Software Foundation and appearing in the file
 * LICENSE.GPL included in the packaging of this file or here
 * <http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt>
 *-------------------------------- Arctic Core -----------------------------*/

/** @reqSettings DEFAULT_SPECIFICATION_REVISION=4.2.2 */

/** @req 4.2.2/SWS_SD_00001 Sd_Lcfg.c and Sd_PBcfg.c is generated */
/** @req 4.2.2/SWS_SD_00117 Imported Types */
/** @req 4.2.2/SWS_SD_00026 The Service Discovery module shall be able to reference RoutingGroup(s) per Service Instance/Eventgroup */
/** @req 4.2.2/SWS_SD_00700 The Service Discovery module shall be able to reference SocketConnections and SocketConnectionGroups per Service Instance/Eventgroup. */
/** @req 4.2.2/SWS_SD_00040 The Service Discovery module receives Service Discovery messages via the API Sd_SoAdIfRxIndication()
 *                    and the configuration items SdInstanceUnicastRxPdu and SdInstanceMulticastRxPdu.*/
/** @req 4.2.2/SWS_SD_00135 The Service Discovery module shall support tool based configuration. */
/** @req 4.2.2/SWS_SD_00136 The configuration tool shall check the consistency of the configuration parameters at system configuration time.*/
/** @req 4.2.2/SWS_SD_00303 The Service Instance ID shall not be set to 0xFFFF for any 鈥淚nstance鈥�. */
/** @req 4.2.2/SWS_SD_00400 It shall be possible to configure the Service Discovery module as an optional AUTOSAR BSW Module. */
/** @req 4.2.2/SWS_SD_00476 Entries for Eventgroups shall not use 鈥渁ny values鈥� as Service ID, Instance ID, Eventgroup ID, and/or MajorVersion. (Validation check)*/
/** @req 4.2.2/SWS_SD_00654 Different service instances of the same service on the same ECU shall use different endpoints (accomplished in SoAd configuration) */


#include "SD.h"
#include "SoAd.h"
#include "Det.h"
/** @req 4.2.2/SWS_SD_00003 */
#if defined (USE_DEM)
#include "Dem.h"
#endif
#include "MemMap.h"

#include "SD_Internal.h"


/* ----------------------------[private macro]-------------------------------*/
/** @req 4.2.2/SWS_SD_00109 */
/** @req 4.2.2/SWS_SD_00110 */

/*lint -emacro(904,VALIDATE_NO_RV,VALIDATE_RV,VALIDATE)*/ /*904 PC-Lint exception to MISRA 14.7 (validate DET macros)*/

#if defined(USE_DET) && (SD_DEV_ERROR_DETECT == STD_ON)
#define VALIDATE_RV(_exp,_api,_err,_rv) \
        if( !(_exp) ) { \
          (void)Det_ReportError(SD_MODULE_ID,0,_api,_err); \
          return _rv; \
        }

#define VALIDATE(_exp,_api,_err ) \
        VALIDATE_RV(_exp,_api,_err, (E_NOT_OK))

#define VALIDATE_NO_RV(_exp,_api,_err ) \
        if( !(_exp) ) { \
          (void)Det_ReportError(SD_MODULE_ID,0,_api,_err); \
          return; \
        }

#define DET_REPORTERROR(_x,_y,_z,_q) (void)Det_ReportError(_x, _y, _z, _q)
#else
#define VALIDATE_RV(_exp,_api,_err,_rv)
#define VALIDATE(_exp,_api,_err )
#define VALIDATE_NO_RV(_exp,_api,_err )
#define DET_REPORTERROR(_x,_y,_z,_q)
#endif


const Sd_ConfigType *SdCfgPtr = NULL;

/* Checks if a serveHandleId is valid or not */
static boolean InvalidHandle (uint16 serviceHandleId){

    boolean status;
    status = TRUE;
    /* Check if serviceHandleId is stored in the service table */
    for (uint32 instance=0; ((instance < SD_NUMBER_OF_INSTANCES) && (status == TRUE)); instance++)
    {
        for (uint32 client=0; client < SdCfgPtr->Instance[instance].SdNoOfClientServices; client++)
        {
            if (SdCfgPtr->Instance[instance].SdClientService[client].HandleId == serviceHandleId){
                status = FALSE;
                break;
            }
        }
    }
    return status;
}


/* Checks if a an eventgroupHandleId is valid or not */
static boolean InvalidConsumedEventGroupHandle (uint16 eventgroupHandleId){

    boolean status;
    status = TRUE;
    /* Check if serviceHandleId is stored in the client consumed eventgroup table */
    for (uint32 instance=0; ((instance < SD_NUMBER_OF_INSTANCES) && (status == TRUE)); instance++)
    {
        for (uint32 client=0; ((client < SdCfgPtr->Instance[instance].SdNoOfClientServices) && (status == TRUE)); client++)
        {
            for (uint32 eg=0; eg < SdCfgPtr->Instance[instance].SdClientService[client].NoOfConsumedEventGroups; eg++)
            {
                if (SdCfgPtr->Instance[instance].SdClientService[client].ConsumedEventGroup[eg].HandleId == eventgroupHandleId){
                    status = FALSE;
                    break;
                }
            }
        }
    }
    return status;
}

/* Checks if a socket connection is valid or not */
static boolean InvalidSoCon (SoAd_SoConIdType soConId){
    /* IMPROVEMENT: Check if soConId is valid or not */
    (void)soConId;
    return FALSE;  /* TEMP */
}


/* Checks if a RxPduId is valid or not */
static boolean InvalidRxPduId (PduIdType rxPduId){
    /* IMPROVEMENT: Check if rxPduId is valid or not */
    (void)rxPduId;
    return FALSE;  /* TEMP */
}

static Sd_ModuleStateType Sd_ModuleState = SD_UNINITIALIZED;

/* Initialize state machines for all Service instances */
void ClearSdDynConfig(void){
    const Ipv4Endpoint null_endpoint = {0,0,0,FALSE};
    const Ipv4Multicast null_multicast = {0,0,0,FALSE};
    const Sd_SubscriberType null_subscriber = {0, FALSE};

    Sd_DynConfig.Instance->TxPduIpAddressAssigned = FALSE;
    Sd_DynConfig.Instance->SdInitCalled = FALSE;
    Sd_DynConfig.Instance->TxSoCon = 0;
    /** @req 4.2.2/SWS_SD_00034 */
    Sd_DynConfig.Instance->MulticastSessionID = 1;
    for (uint8 i=0; i<Sd_DynConfig.Instance->InstanceCfg->SdNoOfClientServices; i++){
        Sd_DynConfig.Instance->SdClientService[i].Phase = SD_DOWN_PHASE;
        Sd_DynConfig.Instance->SdClientService[i].CurrentState = SD_CLIENT_SERVICE_DOWN;
        Sd_DynConfig.Instance->SdClientService[i].ClientServiceMode = SD_CLIENT_SERVICE_RELEASED;
        /** @req 4.2.2/SWS_SD_00021 */
        if(Sd_DynConfig.Instance->SdClientService[i].ClientServiceCfg->AutoRequire == TRUE){
            Sd_DynConfig.Instance->SdClientService[i].ClientServiceMode = SD_CLIENT_SERVICE_REQUESTED;
        }
        Sd_DynConfig.Instance->SdClientService[i].SocketConnectionOpened = FALSE;
        Sd_DynConfig.Instance->SdClientService[i].TTL_Timer_Value_ms = 0;
        Sd_DynConfig.Instance->SdClientService[i].TTL_Timer_Running = FALSE;
        Sd_DynConfig.Instance->SdClientService[i].FindDelay_Timer_Value_ms = 0;
        Sd_DynConfig.Instance->SdClientService[i].FindRepDelay_Timer_Value_ms = 0;
        for (uint8 j=0;j<Sd_DynConfig.Instance->SdClientService[i].ClientServiceCfg->NoOfConsumedEventGroups;j++){
            /** @req 4.2.2/SWS_SD_00440 */
            if (Sd_DynConfig.Instance->SdClientService[i].ClientServiceCfg->ConsumedEventGroup[j].AutoRequire == TRUE){
                Sd_DynConfig.Instance->SdClientService[i].ConsumedEventGroups[j].ConsumedEventGroupMode = SD_CONSUMED_EVENTGROUP_REQUESTED;
            }
            else {
                Sd_DynConfig.Instance->SdClientService[i].ConsumedEventGroups[j].ConsumedEventGroupMode = SD_CONSUMED_EVENTGROUP_RELEASED;
            }
            Sd_DynConfig.Instance->SdClientService[i].ConsumedEventGroups[j].ConsumedEventGroupState = SD_CONSUMED_EVENTGROUP_DOWN;
            Sd_DynConfig.Instance->SdClientService[i].ConsumedEventGroups[j].TTL_Timer_Value_ms = 0;
            Sd_DynConfig.Instance->SdClientService[i].ConsumedEventGroups[j].UdpEndpoint = null_endpoint;
            Sd_DynConfig.Instance->SdClientService[i].ConsumedEventGroups[j].TcpEndpoint = null_endpoint;
            Sd_DynConfig.Instance->SdClientService[i].ConsumedEventGroups[j].MulticastAddress = null_multicast;
        }
        Sd_DynConfig.Instance->SdClientService[i].OfferActive = FALSE;
        Sd_DynConfig.Instance->SdClientService[i].FindDelayTimerOn = FALSE;
        Sd_DynConfig.Instance->SdClientService[i].FindRepDelayTimerOn = FALSE;
        Sd_DynConfig.Instance->SdClientService[i].RepetitionFactor = 0;
        Sd_DynConfig.Instance->SdClientService[i].FindRepetitions = 0;
        Sd_DynConfig.Instance->SdClientService[i].TcpSoConOpened = FALSE;
        Sd_DynConfig.Instance->SdClientService[i].UdpSoConOpened = FALSE;
        Sd_DynConfig.Instance->SdClientService[i].UdpEndpoint = null_endpoint;
        Sd_DynConfig.Instance->SdClientService[i].TcpEndpoint = null_endpoint;
        /** @req 4.2.2/SWS_SD_00034 */
        Sd_DynConfig.Instance->SdClientService[i].UnicastSessionID = 1;
    }

    for (uint8 i=0; i<Sd_DynConfig.Instance->InstanceCfg->SdNoOfServerServices; i++){
        Sd_DynConfig.Instance->SdServerService[i].Phase = SD_DOWN_PHASE;
        Sd_DynConfig.Instance->SdServerService[i].ServerServiceMode = SD_SERVER_SERVICE_DOWN;
        /** @req 4.2.2/SWS_SD_00020 */
        if(Sd_DynConfig.Instance->SdServerService[i].ServerServiceCfg->AutoAvailable == TRUE){
            Sd_DynConfig.Instance->SdServerService[i].ServerServiceMode = SD_SERVER_SERVICE_AVAILABLE;
        }
        Sd_DynConfig.Instance->SdServerService[i].SocketConnectionOpened = FALSE;
        Sd_DynConfig.Instance->SdServerService[i].InitialOffer_Timer_Value_ms = 0;
        Sd_DynConfig.Instance->SdServerService[i].OfferRepDelay_Timer_Value_ms = 0;
        Sd_DynConfig.Instance->SdServerService[i].OfferCyclicDelay_Timer_Value_ms = 0;
        Sd_DynConfig.Instance->SdServerService[i].InitialOfferTimerOn = FALSE;
        Sd_DynConfig.Instance->SdServerService[i].OfferRepDelayTimerOn = FALSE;
        Sd_DynConfig.Instance->SdServerService[i].OfferCyclicDelayTimerOn = FALSE;
        Sd_DynConfig.Instance->SdServerService[i].RepetitionFactor = 0;
        Sd_DynConfig.Instance->SdServerService[i].OfferRepetitions = 0;
        for (uint8 j=0;j<Sd_DynConfig.Instance->SdServerService[i].ServerServiceCfg->NoOfEventHandlers;j++){
            Sd_DynConfig.Instance->SdServerService[i].EventHandlers[j].EventHandlerState = SD_EVENT_HANDLER_RELEASED;
            Sd_DynConfig.Instance->SdServerService[i].EventHandlers[j].NoOfSubscribers = 0;
            for (uint8 k=0; k < MAX_NO_OF_SUBSCRIBERS; k++) {
                Sd_DynConfig.Instance->SdServerService[i].EventHandlers[j].FanOut[k] = null_subscriber;
            }

        }
        Sd_DynConfig.Instance->SdServerService[i].TcpSoConOpened = FALSE;
        Sd_DynConfig.Instance->SdServerService[i].UdpSoConOpened = FALSE;
        /** @req 4.2.2/SWS_SD_00034 */
        Sd_DynConfig.Instance->SdServerService[i].UnicastSessionID = 1;
    }

}

/** @req 4.2.2/SWS_SD_00119 */
void Sd_Init( const Sd_ConfigType* ConfigPtr ){

/** @req 4.2.2/SWS_SD_00109 */
/** @req 4.2.2/SWS_SD_00110 */

    if(NULL == ConfigPtr) {
        ConfigPtr = &Sd_Config;
    }

    /** @req 4.2.2/SWS_SD_00121 */
    SdCfgPtr = ConfigPtr;

    /** @req 4.2.2/SWS_SD_00120 */
    /* IMPROVEMENT: Initialize state machines for all Service instances */
    ClearSdDynConfig();

    InitMessagePool();

    /** @req 4.2.2/SWS_SD_00122 */
    Sd_ModuleState = SD_INITIALIZED;

    /* Indicate for ClientService state machine that SdInit was called since last Sd_MainFunction cycle */
    Sd_DynConfig.Instance->SdInitCalled = TRUE;

    /* Open Sockets for SD TX and RX messages */

    (void)SoAd_GetSoConId(SdCfgPtr->Instance->TxPduId, &Sd_DynConfig.Instance->TxSoCon);
    (void)SoAd_OpenSoCon(Sd_DynConfig.Instance->TxSoCon);

    if (SdCfgPtr->Instance->MulticastRxPduSoConRef != SOCKET_CONNECTION_GROUP_NOT_SET){
        Sd_DynConfig.Instance->MulticastRxSoCon = SdCfgPtr->Instance->MulticastRxPduSoConRef;
        (void)SoAd_OpenSoCon(Sd_DynConfig.Instance->MulticastRxSoCon);
    }

    if (SdCfgPtr->Instance->UnicastRxPduSoConRef != SOCKET_CONNECTION_GROUP_NOT_SET){
        Sd_DynConfig.Instance->UnicastRxSoCon = SdCfgPtr->Instance->UnicastRxPduSoConRef;
        (void)SoAd_OpenSoCon(Sd_DynConfig.Instance->UnicastRxSoCon);
    }

}

/** @req 4.2.2/SWS_SD_00126 */
#if (SD_VERSION_INFO_API == STD_ON)
/** @req 4.2.2/SWS_SD_00124 */
void Sd_GetVersionInfo( Std_VersionInfoType* versioninfo ) {
#if defined(USE_DET) && (SD_DEV_ERROR_DETECT == STD_ON)
    /** @req 4.2.2/SWS_SD_00497 */
    VALIDATE_NO_RV( ( NULL != versioninfo ), SD_GET_VERSION_INFO_ID, SD_E_PARAM_POINTER);
#endif

    /** @req 4.2.2/SWS_SD_00125 */
    versioninfo->vendorID = SD_VENDOR_ID;
    versioninfo->moduleID = SD_MODULE_ID;
    versioninfo->sw_major_version = SD_SW_MAJOR_VERSION;
    versioninfo->sw_minor_version = SD_SW_MINOR_VERSION;
    versioninfo->sw_patch_version = SD_SW_PATCH_VERSION;

    return;
}
#endif

/** @req 4.2.2/SWS_SD_00496 */
Std_ReturnType Sd_ServerServiceSetState( uint16 SdServerServiceHandleId, Sd_ServerServiceSetStateType ServerServiceState) {
    Std_ReturnType result = E_OK;
    /** @req 4.2.2/SWS_SD_00407 */
#if defined(USE_DET) && (SD_DEV_ERROR_DETECT == STD_ON)
    VALIDATE_RV(Sd_ModuleState==SD_INITIALIZED, SD_SERVER_SERVICE_SET_STATE_ID, SD_E_NOT_INITIALIZED, E_NOT_OK);

    #endif

    /** @req 4.2.2/SWS_SD_00408 */
    if ((ServerServiceState != SD_SERVER_SERVICE_DOWN) && (ServerServiceState != SD_SERVER_SERVICE_AVAILABLE)) {
#if defined(USE_DET) && (SD_DEV_ERROR_DETECT == STD_ON)
        (void)Det_ReportError(SD_MODULE_ID, 0, SD_SERVER_SERVICE_SET_STATE_ID, SD_E_INV_MODE);
#endif
        /*lint -e{904} ARGUMENT CHECK */
        return E_NOT_OK;
    }

    /** @req 4.2.2/SWS_SD_00607 */
    if (InvalidHandle(SdServerServiceHandleId)) {
#if defined(USE_DET) && (SD_DEV_ERROR_DETECT == STD_ON)
        (void)Det_ReportError(SD_MODULE_ID, 0, SD_SERVER_SERVICE_SET_STATE_ID, SD_E_INV_ID);
#endif
        /*lint -e{904} ARGUMENT CHECK */
        return E_NOT_OK;
    }

    /** @req 4.2.2/SWS_SD_00005 */
    /* Store ServerServiceState for this Server instance */
    for (uint32 instance=0; instance < SD_NUMBER_OF_INSTANCES; instance++)
    {
        for (uint32 server=0; server < SdCfgPtr->Instance[instance].SdNoOfServerServices; server++)
        {
            if (SdCfgPtr->Instance[instance].SdServerService[server].HandleId == SdServerServiceHandleId){
                Sd_DynConfig.Instance[instance].SdServerService[server].ServerServiceMode = ServerServiceState;
                break;
            }
        }
    }

    return result;

}

/** @req 4.2.2/SWS_SD_00409 */
Std_ReturnType Sd_ClientServiceSetState( uint16 ClientServiceHandleID, Sd_ClientServiceSetStateType ClientServiceState ) {
    Std_ReturnType result = E_OK;

    /** @req 4.2.2/SWS_SD_00410 */
#if defined(USE_DET) && (SD_DEV_ERROR_DETECT == STD_ON)
    VALIDATE_RV(Sd_ModuleState==SD_INITIALIZED, SD_CLIENT_SERVICE_SET_STATE_ID, SD_E_NOT_INITIALIZED, E_NOT_OK);
#endif

    /** @req 4.2.2/SWS_SD_00411 */
    if ((ClientServiceState != SD_CLIENT_SERVICE_RELEASED) && (ClientServiceState != SD_CLIENT_SERVICE_REQUESTED)) {
#if defined(USE_DET) && (SD_DEV_ERROR_DETECT == STD_ON)
        (void)Det_ReportError(SD_MODULE_ID, 0, SD_CLIENT_SERVICE_SET_STATE_ID, SD_E_INV_MODE);
#endif
        /*lint -e{904} ARGUMENT CHECK */
        return E_NOT_OK;
    }

    /** @req 4.2.2/SWS_SD_00608 */
    if (InvalidHandle(ClientServiceHandleID)) {
#if defined(USE_DET) && (SD_DEV_ERROR_DETECT == STD_ON)
        (void)Det_ReportError(SD_MODULE_ID, 0, SD_CLIENT_SERVICE_SET_STATE_ID, SD_E_INV_ID);
#endif
        /*lint -e{904} ARGUMENT CHECK */
        return E_NOT_OK;
    }

    /** @req 4.2.2/SWS_SD_00005 */
    /* Store ClientServiceState for this Client instance */
    for (uint32 instance=0; instance < SD_NUMBER_OF_INSTANCES; instance++)
    {
        for (uint32 client=0; client < SdCfgPtr->Instance[instance].SdNoOfClientServices; client++)
        {
            if (SdCfgPtr->Instance[instance].SdClientService[client].HandleId == ClientServiceHandleID){
                Sd_DynConfig.Instance[instance].SdClientService[client].ClientServiceMode = ClientServiceState;

                /** @req 4.2.2/SWS_SD_00443 */
                if (ClientServiceState == SD_CLIENT_SERVICE_RELEASED) {
                    for(uint8 event_group_index= 0; \
                            event_group_index < SdCfgPtr->Instance[instance].SdClientService[client].NoOfConsumedEventGroups; event_group_index++){
                            Sd_DynConfig.Instance[instance].SdClientService[client].ConsumedEventGroups[event_group_index].ConsumedEventGroupMode =
                                    SD_CONSUMED_EVENTGROUP_RELEASED;
                    }
                }
                else
                {
                    /** @req 4.2.2/SWS_SD_00440 */
                    for(uint8 event_group_index= 0; \
                        event_group_index < SdCfgPtr->Instance[instance].SdClientService[client].NoOfConsumedEventGroups; event_group_index++){
                        if(SdCfgPtr->Instance[instance].SdClientService[client].ConsumedEventGroup[event_group_index].AutoRequire == TRUE){
                            Sd_DynConfig.Instance[instance].SdClientService[client].ConsumedEventGroups[event_group_index].ConsumedEventGroupMode =
                                    SD_CONSUMED_EVENTGROUP_REQUESTED;
                        }
                    }
                }
                break;
            }
        }
    }

    return result;
}

/** @req 4.2.2/SWS_SD_00560 */
Std_ReturnType Sd_ConsumedEventGroupSetState( uint16 SdConsumedEventGroupHandleId, Sd_ConsumedEventGroupSetStateType ConsumedEventGroupState ) {
    Std_ReturnType result = E_OK;


    /** @req 4.2.2/SWS_SD_00469 */
#if defined(USE_DET) && (SD_DEV_ERROR_DETECT == STD_ON)
    VALIDATE_RV(Sd_ModuleState==SD_INITIALIZED, SD_CONSUMED_EVENT_GROUP_SET_STATE_ID, SD_E_NOT_INITIALIZED, E_NOT_OK);
#endif

    /** @req 4.2.2/SWS_SD_00470 */
    if ((ConsumedEventGroupState != SD_CONSUMED_EVENTGROUP_RELEASED) && (ConsumedEventGroupState != SD_CONSUMED_EVENTGROUP_REQUESTED)) {
#if defined(USE_DET) && (SD_DEV_ERROR_DETECT == STD_ON)
        (void)Det_ReportError(SD_MODULE_ID, 0, SD_CONSUMED_EVENT_GROUP_SET_STATE_ID, SD_E_INV_MODE);
#endif
        /*lint -e{904} ARGUMENT CHECK */
        return E_NOT_OK;
    }

    /** @req 4.2.2/SWS_SD_00609 */
    if (InvalidConsumedEventGroupHandle(SdConsumedEventGroupHandleId)) {
#if defined(USE_DET) && (SD_DEV_ERROR_DETECT == STD_ON)
        (void)Det_ReportError(SD_MODULE_ID, 0, SD_CONSUMED_EVENT_GROUP_SET_STATE_ID, SD_E_INV_ID);
#endif
        /*lint -e{904} ARGUMENT CHECK */
        return E_NOT_OK;
    }

    /** @req 4.2.2/SWS_SD_00005 */
    /* Store ConsumedEventGroupState for this EventGroup instance */
    for (uint32 instance=0; instance < SD_NUMBER_OF_INSTANCES; instance++)
    {
        for (uint32 client=0; client < SdCfgPtr->Instance[instance].SdNoOfClientServices; client++)
        {
            if (SdCfgPtr->Instance[instance].SdClientService[client].ConsumedEventGroup != NULL){
                for (uint32 evGrp=0; evGrp < SdCfgPtr->Instance[instance].SdClientService[client].NoOfConsumedEventGroups; evGrp++){
                    if (SdCfgPtr->Instance[instance].SdClientService[client].ConsumedEventGroup[evGrp].HandleId == SdConsumedEventGroupHandleId){

                        /** @req 4.2.2/SWS_SD_00442 */
                        if (Sd_DynConfig.Instance[instance].SdClientService[client].ClientServiceMode == SD_CLIENT_SERVICE_RELEASED) {
                            return E_NOT_OK;
                        }
                        Sd_DynConfig.Instance[instance].SdClientService[client].ConsumedEventGroups[evGrp].ConsumedEventGroupMode = ConsumedEventGroupState;
                        break;
                    }

                }
            }
        }
    }

    return result;
}

/** @req 4.2.2/SWS_SD_00412 */
void Sd_LocalIpAddrAssignmentChg( SoAd_SoConIdType SoConId, TcpIp_IpAddrStateType State ){

    /** @req 4.2.2/SWS_SD_00471 */
#if defined(USE_DET) && (SD_DEV_ERROR_DETECT == STD_ON)
    VALIDATE_NO_RV(Sd_ModuleState==SD_INITIALIZED, SD_LOCAL_IP_ADDR_ASSIGNMENT_CHG_ID, SD_E_NOT_INITIALIZED);
#endif

    /** @req 4.2.2/SWS_SD_00472 */
    if ((State != TCPIP_IPADDR_STATE_ASSIGNED) && (State != TCPIP_IPADDR_STATE_ONHOLD) && (State != TCPIP_IPADDR_STATE_UNASSIGNED)) {
#if defined(USE_DET) && (SD_DEV_ERROR_DETECT == STD_ON)
        (void)Det_ReportError(SD_MODULE_ID, 0, SD_LOCAL_IP_ADDR_ASSIGNMENT_CHG_ID, SD_E_INV_MODE);
#endif
        /*lint -e{904} ARGUMENT CHECK */
        return;
    }

    /** @req 4.2.2/SWS_SD_00610 */
    if (InvalidSoCon(SoConId)) {
#if defined(USE_DET) && (SD_DEV_ERROR_DETECT == STD_ON)
        (void)Det_ReportError(SD_MODULE_ID, 0, SD_LOCAL_IP_ADDR_ASSIGNMENT_CHG_ID, SD_E_INV_ID);
#endif
        /*lint -e{904} ARGUMENT CHECK */
        return;
    }

    /* Assign state of IP address assignment for this socket connection */
    for (uint32 instance=0; instance < SD_NUMBER_OF_INSTANCES; instance++)
    {
        if (Sd_DynConfig.Instance[instance].TxSoCon == SoConId) {
            Sd_DynConfig.Instance[instance].TxPduIpAddressAssigned = (State == TCPIP_IPADDR_STATE_ASSIGNED);
            break;
        }
    }

}

/** @req 4.2.2/SWS_SD_00129 */
/*lint -e{818} Pointer parameter 'PduInfoPtr' could be declared as pointing to const */
void Sd_RxIndication( PduIdType RxPduId, PduInfoType* PduInfoPtr){

/** @req 4.2.2/SWS_SD_00473 */
#if defined(USE_DET) && (SD_DEV_ERROR_DETECT == STD_ON)
    VALIDATE_NO_RV(Sd_ModuleState==SD_INITIALIZED, SD_RX_INDICATION_ID, SD_E_NOT_INITIALIZED);
#endif

    /** @req 4.2.2/SWS_SD_00474 */
    if (InvalidRxPduId(RxPduId)) {
#if defined(USE_DET) && (SD_DEV_ERROR_DETECT == STD_ON)
        (void)Det_ReportError(SD_MODULE_ID, 0, SD_RX_INDICATION_ID, SD_E_INV_ID);
#endif
        /*lint -e{904} ARGUMENT CHECK */
        return;
    }

    /** @req 4.2.2/SWS_SD_00475 */
#if defined(USE_DET) && (SD_DEV_ERROR_DETECT == STD_ON)
    VALIDATE_NO_RV(PduInfoPtr != NULL, SD_RX_INDICATION_ID, SD_E_PARAM_POINTER);
#endif

    /* Handle recieved Pdu */
    Handle_RxIndication(RxPduId, PduInfoPtr);

}

/** @req 4.2.2/SWS_SD_00130 */
/** @req 4.2.2/SWS_SD_00004 */
void Sd_MainFunction( void ){

/** @req 4.2.2/SWS_SD_00132 */
#if defined(USE_DET) && (SD_DEV_ERROR_DETECT == STD_ON)
    VALIDATE_NO_RV(Sd_ModuleState==SD_INITIALIZED, SD_MAIN_FUNCTION_ID, SD_E_NOT_INITIALIZED);
#endif


/** @req 4.2.2/SWS_SD_00131 */
/* Update counters, times, states and phases */
    Handle_PendingRespMessages();

    for (uint32 instance=0; instance < SD_NUMBER_OF_INSTANCES; instance++)
    {
        for (uint32 client=0; client < SdCfgPtr->Instance[instance].SdNoOfClientServices; client++)
        {
            UpdateClientService(SdCfgPtr, instance, client);
        }
        for (uint32 server=0; server < SdCfgPtr->Instance[instance].SdNoOfServerServices; server++)
        {
            UpdateServerService(SdCfgPtr, instance, server);
        }
    }

}
