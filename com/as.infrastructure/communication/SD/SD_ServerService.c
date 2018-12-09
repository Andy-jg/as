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

/** @req 4.2.2/SWS_SD_00029 The Service Discovery module shall only call SoAd_IfTransmit() if an IP address is assigned */

#include "SD.h"
#include "SD_Internal.h"

static Sd_Entry_Type1_Services entry11;
static Sd_Entry_Type2_EventGroups entry22;
static TcpIp_SockAddrType ipaddress;

static const TcpIp_SockAddrType wildcard = {
        (TcpIp_DomainType) TCPIP_AF_INET,
        0 /* TBD Port???? */,
        {TCPIP_IPADDR_ANY, TCPIP_IPADDR_ANY, TCPIP_IPADDR_ANY, TCPIP_IPADDR_ANY }
};


Sd_Message msg;

static void EntryReceived(Sd_DynServerServiceType *server, Sd_Entry_Type1_Services **entry1, Sd_Entry_Type2_EventGroups **entry2, TcpIp_SockAddrType *ipaddress, boolean *is_multicast)
{
    Sd_InstanceType *server_svc = NULL;
    uint8 *option_run1 [MAX_OPTIONS] = {NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL};
    uint8 *option_run2 [MAX_OPTIONS] = {NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL};
    uint8 no_of_endpoints = 0;
    uint8 no_of_capabilty_records = 0;
    Ipv4Endpoint endpoint[MAX_OPTIONS];
    Sd_CapabilityRecordType capabilty_record[MAX_OPTIONS];

    /* First check if there are more entries in the last read message to fetch */
    if (msg.ProtocolVersion != 0x01) {
        /* Fetch a new message from the queue */
        if (!ReceiveSdMessage(&msg, ipaddress, SERVER_QUEUE, &server_svc, is_multicast)) {
            *entry1 = NULL;
            *entry2 = NULL;
            return;
        }
    }

    /* Find out which type of entry */
    uint8 type = msg.EntriesArray[0];

    if ((type == 0) || (type == 1)) {
        /* FIND_SERVICE */
        DecodeType1Entry (msg.EntriesArray, *entry1);
        *entry2 = NULL;
        if (msg.LengthOfOptionsArray > 0) {
            OptionsReceived(msg.OptionsArray, msg.LengthOfOptionsArray, *entry1, NULL, option_run1, option_run2);
        }
        /* Move entry pointer */
        msg.EntriesArray += ENTRY_TYPE_1_SIZE;
    }
    else if ((type == 6) || (type == 7)) {
        /* SUBSCRIBE_EVENTGROUP or STOP_SUBSCRIBE_EVENTGROUP */
        DecodeType2Entry (msg.EntriesArray, *entry2);
        *entry1 = NULL;
        if (msg.LengthOfOptionsArray > 0) {
            OptionsReceived(msg.OptionsArray, msg.LengthOfOptionsArray, NULL, *entry2, option_run1, option_run2);
        }
        /* Move entry pointer */
        msg.EntriesArray += ENTRY_TYPE_2_SIZE;
    }


    /* Decode configuration option attribute */
    DecodeOptionConfiguration(option_run1,capabilty_record,&no_of_capabilty_records);

    /* Check if this message is aimed for this server instance */
    if (*entry1 != NULL) {
        /** @req 4.2.2/SWS_SD_00486 */
        /* Received FindService  */

        boolean matchServiceID = (((*entry1)->ServiceID == 0xFFFF) || ((*entry1)->ServiceID == server->ServerServiceCfg->Id));
        /** @req 4.2.2/SWS_SD_00295 */
        boolean matchInstanceID = (((*entry1)->InstanceID == 0xFFFF) || ((*entry1)->InstanceID == server->ServerServiceCfg->InstanceId));
        boolean matchMajorVersion = (((*entry1)->MajorVersion == 0xFF) || ((*entry1)->MajorVersion == server->ServerServiceCfg->MajorVersion));
        boolean matchMinorVersion = (((*entry1)->MinorVersion == 0xFFFFFFFF) || ((*entry1)->MinorVersion == server->ServerServiceCfg->MinorVersion));


        /* This server is the intended one. No endpoint option need to be analyzed for FindService entries.  */
        if (!(matchServiceID && matchInstanceID && matchMajorVersion && matchMinorVersion)){
            *entry1 = NULL;
            *entry2 = NULL;
        } else {
            /* This server is the intended one. No endpoint option need to be analyzed for FindService entries.
             * Fetch remote address from RxPdu  */
            if (server_svc != NULL) {
                SoAd_SoConIdType client_socket;
                if (*is_multicast) {
                    (void)SoAd_Arc_GetSoConIdFromRxPdu(server_svc->MulticastRxPduId, &client_socket);
                } else {
                    (void)SoAd_Arc_GetSoConIdFromRxPdu(server_svc->UnicastRxPduId, &client_socket);
                }
                (void)SoAd_GetRemoteAddr(client_socket, ipaddress);
                (void)SoAd_SetRemoteAddr(Sd_DynConfig.Instance->TxSoCon, &wildcard);
            }
            FreeSdMessage(SERVER_QUEUE);
        }

    } else if (entry2 != NULL) {
        /* Received SubscribeEventgroup or StopSubscribeEventgroup */
        /** @req 4.2.2/SWS_SD_00490 */
        boolean EventHandlerFound = FALSE;
        uint8 EventHandlerIndex = 0;
        for (uint8 i=0; i < server->ServerServiceCfg->NoOfEventHandlers; i++){
            if (((*entry2)->ServiceID == server->ServerServiceCfg->Id) &&
                ((*entry2)->InstanceID == server->ServerServiceCfg->InstanceId) &&
                ((*entry2)->MajorVersion == server->ServerServiceCfg->MajorVersion) &&
                ((*entry2)->EventgroupID == server->ServerServiceCfg->EventHandler[i].EventGroupId))
            {
                /* The entry was aimed for this server instance. */
                EventHandlerFound = TRUE;
                EventHandlerIndex = i;
                break;
            }
        }

        if (!EventHandlerFound)
        {
            *entry1 = NULL;
            *entry2 = NULL;
        } else {
            /* Decode and store the option parameters */
            DecodeOptionIpv4Endpoint(option_run1, endpoint, &no_of_endpoints);
            for (uint8 i=0; i < no_of_endpoints; i++){
               if (endpoint[i].Protocol == UDP_PROTO) {
                    memcpy(&server->EventHandlers[EventHandlerIndex].UdpEndpoint, &endpoint[i], sizeof(Ipv4Endpoint));
                    server->EventHandlers[EventHandlerIndex].UdpEndpoint.valid = TRUE;
               }
               else if (endpoint[i].Protocol == TCP_PROTO) {
                    memcpy(&server->EventHandlers[EventHandlerIndex].TcpEndpoint, &endpoint[i], sizeof(Ipv4Endpoint));
                    server->EventHandlers[EventHandlerIndex].TcpEndpoint.valid = TRUE;
               }
            }
            FreeSdMessage(SERVER_QUEUE);
        }

    }

    if (msg.EntriesArray == (msg.OptionsArray - 4)) {
        /* All entries are processed in this message.
         * Set protcolversion to 0 to indicate that a new message
         * should be fetched for the next entry. */
        msg.ProtocolVersion = 0x00;
    }

}

#if 0
/* This function is currenly not used, it is kept since it may be used as a part of SD server
 * functionality. It should be removed if not used when SD server has been added.
 */
/* Open TCP Connection if TcpRef is configured and was not opened before*/
static void SetRemoteTcpConnection(Sd_DynServerServiceType *server)
{
    if ((server->ServerServiceCfg->TcpSocketConnectionGroupId != SOCKET_CONNECTION_GROUP_NOT_SET))
    {
        /* Open all SoCons in Group */
        for (uint16 i=0;i<sizeof(server->ServerServiceCfg->TcpSocketConnectionGroupSocketConnectionIdsPtr)/sizeof(uint16); i++){
            (void)SoAd_OpenSoCon(server->ServerServiceCfg->TcpSocketConnectionGroupSocketConnectionIdsPtr[i]);
        }
        server->TcpSoConOpened = TRUE;
    }
}
#endif

/* Open TCP Connection if TcpRef is configured and was not opened before*/
static void OpenTcpConnection(Sd_DynServerServiceType *server)
{
    if ((server->ServerServiceCfg->TcpSocketConnectionGroupId != SOCKET_CONNECTION_GROUP_NOT_SET) && !(server->TcpSoConOpened))
    {
        /* Open all SoCons in Group */
        for (uint16 i=0;i<sizeof(server->ServerServiceCfg->TcpSocketConnectionGroupSocketConnectionIdsPtr)/sizeof(uint16); i++){
            (void)SoAd_OpenSoCon(server->ServerServiceCfg->TcpSocketConnectionGroupSocketConnectionIdsPtr[i]);
        }
        server->TcpSoConOpened = TRUE;
    }
}

/* Open Udp Connection if UdpRef is configured and was not opened before*/
static void OpenUdpConnection(Sd_DynServerServiceType *server)
{
    if ((server->ServerServiceCfg->UdpSocketConnectionGroupId != SOCKET_CONNECTION_GROUP_NOT_SET) && !(server->UdpSoConOpened))
    {
        /* Open all SoCons in Group */
        for (uint16 i=0;i<sizeof(server->ServerServiceCfg->UdpSocketConnectionGroupSocketConnectionIdsPtr)/sizeof(uint16); i++){
            (void)SoAd_OpenSoCon(server->ServerServiceCfg->UdpSocketConnectionGroupSocketConnectionIdsPtr[i]);
        }
        server->UdpSoConOpened = TRUE;
    }
}

static void OpenSocketConnections(Sd_DynServerServiceType *server)
{
    OpenTcpConnection(server); /** @req 4.2.2/SWS_SD_00421 */
    OpenUdpConnection(server);
    server->SocketConnectionOpened = TRUE;
}

/* Close TCP Connection if TcpRef is configured and was not opened before*/
static void CloseTcpConnection(Sd_DynServerServiceType *server, boolean abort)
{
    if ((server->ServerServiceCfg->TcpSocketConnectionGroupId != SOCKET_CONNECTION_GROUP_NOT_SET) && (server->TcpSoConOpened)) {
        /* Open all SoCons in Group */
        for (uint16 i=0;i<sizeof(server->ServerServiceCfg->TcpSocketConnectionGroupSocketConnectionIdsPtr)/sizeof(uint16); i++) {
            (void)SoAd_CloseSoCon(server->ServerServiceCfg->TcpSocketConnectionGroupSocketConnectionIdsPtr[i], abort);
        }

        server->TcpSoConOpened = FALSE;
    }
}

/* Close Udp Connection if UdpRef is configured and was not opened before*/
static void CloseUdpConnection(Sd_DynServerServiceType *server, boolean abort)
{
    if ((server->ServerServiceCfg->UdpSocketConnectionGroupId != SOCKET_CONNECTION_GROUP_NOT_SET) && (server->UdpSoConOpened)) {
        for (uint16 i=0;i<sizeof(server->ServerServiceCfg->UdpSocketConnectionGroupSocketConnectionIdsPtr)/sizeof(uint16); i++) {
            (void)SoAd_CloseSoCon(server->ServerServiceCfg->UdpSocketConnectionGroupSocketConnectionIdsPtr[i], abort);
        }
        server->UdpSoConOpened = FALSE;
    }
}

static void CloseSocketConnections(Sd_DynServerServiceType *server, boolean abort)
{
    CloseTcpConnection(server, abort);
    CloseUdpConnection(server, abort);
    server->SocketConnectionOpened = FALSE;
}

void UpdateServerService(const Sd_ConfigType *cfgPtr, uint32 instanceno, uint32 serverno){
    Sd_DynServerServiceType *server = &Sd_DynConfig.Instance[instanceno].SdServerService[serverno];
    Sd_DynInstanceType *sd_instance = &Sd_DynConfig.Instance[instanceno];
    Sd_Entry_Type1_Services *entry1 = &entry11;
    Sd_Entry_Type2_EventGroups *entry2 = &entry22;
    boolean is_multicast = FALSE;
    memset(&entry11, 0, sizeof(Sd_Entry_Type1_Services));
    memset(&entry22, 0, sizeof(Sd_Entry_Type2_EventGroups));

    EntryReceived(server, &entry1, &entry2, &ipaddress, &is_multicast);

    switch (server->Phase)
    {
    case SD_DOWN_PHASE:
        /** @req 4.2.2/SWS_SD_00317 */
        if ((server->ServerServiceMode == SD_SERVER_SERVICE_AVAILABLE) && sd_instance->TxPduIpAddressAssigned) {

            /** @req 4.2.2/SWS_SD_00330 */
            /** @req 4.2.2/SWS_SD_00024 */
            /*  Call SoAd_EnableRouting(): IMPROVEMENT: Is this correct?*/
            (void)SoAd_EnableRouting (server->ServerServiceCfg->ProvidedMethods.ServerServiceActivationRef);

            server->Phase = SD_INITIAL_WAIT_PHASE;  /* DOWN -> INITIAL_WAIT */

            /** @req 4.2.2/SWS_SD_00606 */
            OpenSocketConnections(server);
        }
        break;

    case SD_INITIAL_WAIT_PHASE:
        /** @req 4.2.2/SWS_SD_00319 */

        /** @req 4.2.2/SWS_SD_00323 */
        if (server->ServerServiceMode != SD_SERVER_SERVICE_AVAILABLE) {
            server->InitialOffer_Timer_Value_ms = 0;
            server->InitialOfferTimerOn = FALSE;
            server->Phase = SD_DOWN_PHASE; /* INITIAL_WAIT -> DOWN */

            /* Set all EventHandlersCurrentState to RELEASED */
            for (uint8 eh = 0; eh < server->ServerServiceCfg->NoOfEventHandlers; eh++) {
                BswM_Sd_EventHandlerCurrentState(
                        server->ServerServiceCfg->EventHandler[eh].HandleId,
                        SD_EVENT_HANDLER_RELEASED);
            }

            /** @req 4.2.2/SWS_SD_00605 */
            if (server->SocketConnectionOpened) {
                /* Call SoAd_CloseSoCon() for all socket connections
                 * in this server service instance */
                CloseSocketConnections(server, TRUE); // IMPROVEMENT: Investigate abort parameter
            }

        }
        /** @req 4.2.2/SWS_SD_00325 */
        if (!sd_instance->TxPduIpAddressAssigned) {
            server->InitialOffer_Timer_Value_ms = 0;
            server->InitialOfferTimerOn = FALSE;
            server->Phase = SD_DOWN_PHASE; /* INITIAL_WAIT -> DOWN */
            /** @req 4.2.2/SWS_SD_00605 */

            if (server->SocketConnectionOpened) {
                /* Call SoAd_CloseSoCon() for all socket connections
                 * in this server service instance */
                CloseSocketConnections(server, TRUE); // IMPROVEMENT: Investigate abort parameter
            }
        }

        if (!server->InitialOfferTimerOn) {
            /** @req 4.2.2/SWS_SD_00318 */
            /* Start random InitialOffer Timer */
            server->InitialOffer_Timer_Value_ms = RandomDelay(server->ServerServiceCfg->TimerRef->InitialOfferDelayMin_ms,
                    server->ServerServiceCfg->TimerRef->InitialOfferDelayMax_ms);
            server->InitialOfferTimerOn = TRUE;
        }
        else
        {
            /** @req 4.2.2/SWS_SD_00320 */
            /* TBD: How to interpret this req? The same as 4.2.2/SWS_SD_00333,4.2.2/SWS_SD_00334,4.2.2/SWS_SD_00344, 4.2.2/SWS_SD_00345? */

            /** @req 4.2.2/SWS_SD_00321 */
            server->InitialOffer_Timer_Value_ms -= SD_MAIN_FUNCTION_CYCLE_TIME_MS;
            if (server->InitialOffer_Timer_Value_ms <= 0) {
                /* Send OfferService Entry */
                TransmitSdMessage(sd_instance, (Sd_DynClientServiceType *)NULL, server, NULL, 0, SD_OFFER_SERVICE, NULL, is_multicast); // IMPROVEMENT: Should ipaddress parameter be used
                server->InitialOffer_Timer_Value_ms = 0;
                server->InitialOfferTimerOn = FALSE;
                /** @req 4.2.2/SWS_SD_00434 */
                /** @req 4.2.2/SWS_SD_00435 */
                if (server->ServerServiceCfg->TimerRef->InitialOfferRepetitionsMax == 0){
                    server->Phase = SD_MAIN_PHASE; /* INITIAL_WAIT -> MAIN */
                }
                else {
                    server->Phase = SD_REPETITION_PHASE; /* INITIAL_WAIT -> REPETITION */
                    server->RepetitionFactor = 1;
                    server->OfferRepetitions = 0;
                }
                break;
            }

        }
        break;
    case SD_REPETITION_PHASE:
        /* Step the EventHandler TTL timers */
        for (uint8 eh=0; eh < server->ServerServiceCfg->NoOfEventHandlers; eh++){
            if (server->EventHandlers[eh].EventHandlerState == SD_EVENT_HANDLER_REQUESTED){
                for(uint8 sub=0; sub < MAX_NO_OF_SUBSCRIBERS; sub++) {

                    if (server->EventHandlers[eh].FanOut[sub].TTL_Timer_On){
                        server->EventHandlers[eh].FanOut[sub].TTL_Timer_Value_ms -= SD_MAIN_FUNCTION_CYCLE_TIME_MS;
                        /** @req 4.2.2/SWS_SD_00458 */
                        if (server->EventHandlers[eh].FanOut[sub].TTL_Timer_Value_ms <= 0){
                            /* Timer expired */
                            server->EventHandlers[eh].FanOut[sub].TTL_Timer_On = FALSE;
                            server->EventHandlers[eh].FanOut[sub].TTL_Timer_Value_ms = 0;
                            server->EventHandlers[eh].NoOfSubscribers--;
                            if (server->EventHandlers[eh].NoOfSubscribers == 0) {
                                server->EventHandlers[eh].EventHandlerState = SD_EVENT_HANDLER_RELEASED;
                                /* Change state for the EventHandler in BswM. */
                                /* IMPROVEMENT: Do this only if current value is != RELEASED */
                                BswM_Sd_EventHandlerCurrentState(server->ServerServiceCfg->EventHandler[eh].HandleId, SD_EVENT_HANDLER_RELEASED);
                            }

                        }
                    }
                }

            }
        }
        /** @req 4.2.2/SWS_SD_00338 */
        if (server->ServerServiceMode != SD_SERVER_SERVICE_AVAILABLE) {
            server->Phase = SD_DOWN_PHASE; /* INITIAL_WAIT -> DOWN */

            /* Set all EventHandlersCurrentState to RELEASED */
            for (uint8 eh = 0; eh < server->ServerServiceCfg->NoOfEventHandlers; eh++){
                BswM_Sd_EventHandlerCurrentState(
                        server->ServerServiceCfg->EventHandler[eh].HandleId,
                        SD_EVENT_HANDLER_RELEASED);
            }

            /** @req 4.2.2/SWS_SD_00341 */
            /** @req 4.2.2/SWS_SD_00024 */
            /*  Call SoAd_DisableRouting(): IMPROVEMENT: Is this correct?*/
            (void)SoAd_DisableRouting (server->ServerServiceCfg->ProvidedMethods.ServerServiceActivationRef);

            /** @req 4.2.2/SWS_SD_00605 */
            if (server->SocketConnectionOpened){
                /* Call SoAd_CloseSoCon() for all socket connections
                 * in this server service instance */
                CloseSocketConnections(server, TRUE);  // IMPROVEMENT: Investigate abort parameter
            }


        }

        /** @req 4.2.2/SWS_SD_00340 */
        if (!sd_instance->TxPduIpAddressAssigned) {
            server->Phase = SD_DOWN_PHASE; /* INITIAL_WAIT -> DOWN */

            /** @req 4.2.2/SWS_SD_00605 */
            if (server->SocketConnectionOpened){
                /* Call SoAd_CloseSoCon() for all socket connections
                 * in this server service instance */
                CloseSocketConnections(server, TRUE);  // IMPROVEMENT: Investigate abort parameter
            }

        }

        if (!server->OfferRepDelayTimerOn) {
            /** @req 4.2.2/SWS_SD_00329 */
            /* Start OfferRepDelay Timer */
            server->OfferRepDelay_Timer_Value_ms = server->RepetitionFactor * server->ServerServiceCfg->TimerRef->InitialOfferRepetitionBaseDelay_ms;
            server->OfferRepDelayTimerOn = TRUE;
        }
//		else
//		{
            /** @req 4.2.2/SWS_SD_00331 */
            /* IMPROVEMENT: Check the calculation for the timer step. */
            server->OfferRepDelay_Timer_Value_ms -= SD_MAIN_FUNCTION_CYCLE_TIME_MS;

            if (server->OfferRepDelay_Timer_Value_ms <= 0) {
                /* Send OfferService Entry */
                TransmitSdMessage(sd_instance, NULL, server, NULL, 0, SD_OFFER_SERVICE,NULL,FALSE); // IMPROVEMENT: Should ipaddress parameter be used
                server->OfferRepDelayTimerOn = FALSE;
                server->OfferRepetitions++;
                server->RepetitionFactor = server->RepetitionFactor * 2; /* Doubles the interval for next FindService */

                /** @req 4.2.2/SWS_SD_00336 */
                if (server->OfferRepetitions >= server->ServerServiceCfg->TimerRef->InitialOfferRepetitionsMax){
                    server->Phase = SD_MAIN_PHASE; /* REPETITION -> MAIN */
                    break;
                }

            }

            /** @req 4.2.2/SWS_SD_00332 */
            if (entry1 != (Sd_Entry_Type1_Services *) NULL){
                if ((entry1->Type == FIND_SERVICE_TYPE)) {
                    /* Send OfferService Entry */
                    TransmitSdMessage(sd_instance, NULL, server, NULL, 0, SD_OFFER_SERVICE,NULL,is_multicast); // IMPROVEMENT: Should ipaddress parameter be used
                }
            }

            /** @req 4.2.2/SWS_SD_00333 */
            if (entry2 != (Sd_Entry_Type2_EventGroups *) NULL){
                if ((entry2->Type == SUBSCRIBE_EVENTGROUP_TYPE) && (entry2->TTL > 0)) {
                    boolean event_handler_found = FALSE;
                    uint8 event_handler_index = 0;

                    /* Find the subscribed event handler. Set it to REQUESTED, and start the TTL Timer for the subscriber
                     * IMROVEMENT: For now we use the Counter parameter as index. Maybe not correct? */
                    for (uint8 eh=0; eh < server->ServerServiceCfg->NoOfEventHandlers; eh++){
                        if (server->ServerServiceCfg->EventHandler[eh].EventGroupId == entry2->EventgroupID) {

//							/** @ req 4.2.2/SWS_SD_00454 */
//							//IMPROVEMENT: More work is needed.
//
//							if (server->ServerServiceCfg->EventHandler[eh].Udp != NULL) {
//								if (server->ServerServiceCfg->EventHandler[eh].Udp->EventActivationRef != ACTIVATION_REF_NOT_SET) {
//
//									/* Set socket remote address */
//									/* IMPROVEMENT: Go through the udp socket connections and compare it with Ipv4EndpointOptionUdp for this eventgroup.
//									 * If none is found, set the remote address on a wildcard. */
//
//									/* Enable routing */
//									if (server->EventHandlers[eh].NoOfSubscribers == 0){
//										(void)SoAd_EnableSpecificRouting
//											(server->ServerServiceCfg->EventHandler[eh].Udp->EventActivationRef,
//											        server->ServerServiceCfg->UdpSocketConnectionGroupId);
//
////										(void)SoAd_IfSpecificRoutingGroupTransmit
////											(server->ServerServiceCfg->EventHandler[eh].Udp->EventTriggeringRef,
////											        server->ServerServiceCfg->UdpSocketConnectionGroupId);
//
//									}
//								}
//							} else {
//
//								/** @ req 4.2.2/SWS_SD_00453 */
//								if (server->ServerServiceCfg->EventHandler[eh].Tcp != NULL) {
//									if (server->ServerServiceCfg->EventHandler[eh].Tcp->EventActivationRef != ACTIVATION_REF_NOT_SET) {
//
//										/* Set socket remote address */
//										/* IMPROVEMENT: Go through the tcp socket connections and compare it with Ipv4EndpointOptionTcp.
//										 * If none is found, set the remote address on a wildcard. */
//
//										/* Enable routing */
//										if (server->EventHandlers[eh].NoOfSubscribers == 0){
//											(void)SoAd_EnableSpecificRouting
//												(server->ServerServiceCfg->EventHandler[eh].Tcp->EventActivationRef,
//														server->ServerServiceCfg->TcpSocketConnectionGroupId);
//
////											(void)SoAd_IfSpecificRoutingGroupTransmit
////												(server->ServerServiceCfg->EventHandler[eh].Tcp->EventTriggeringRef,
////														server->ServerServiceCfg->TcpSocketConnectionGroupId);
//
//										}
//									}
//								}
//							}

                            server->EventHandlers[eh].EventHandlerState = SD_EVENT_HANDLER_REQUESTED;
                            server->EventHandlers[eh].NoOfSubscribers++;
                            server->EventHandlers[eh].FanOut [entry2->Counter].TTL_Timer_Value_ms = entry2->TTL * 1000;
                            server->EventHandlers[eh].FanOut [entry2->Counter].TTL_Timer_On = TRUE;
                            /* Change state for the EventHandler in BswM. */
                            if (server->EventHandlers[eh].NoOfSubscribers == 1) {
                                BswM_Sd_EventHandlerCurrentState(server->ServerServiceCfg->EventHandler[eh].HandleId, SD_EVENT_HANDLER_REQUESTED);
                            }
                            event_handler_found = TRUE;
                            event_handler_index = eh;
                            break;
                        }
                    }

                    if (event_handler_found) {
                        /* Send SubscribeEventGroupAck Entry */
                        TransmitSdMessage(sd_instance, NULL, server, entry2, event_handler_index, SD_SUBSCRIBE_EVENTGROUP_ACK, &ipaddress, is_multicast);
                    }
                }

                /** @req 4.2.2/SWS_SD_00334 */
                if (entry2 != (Sd_Entry_Type2_EventGroups *) NULL){
                    if ((entry2->Type == STOP_SUBSCRIBE_EVENTGROUP_TYPE) && (entry2->TTL == 0)) {

                        /* Find the subscribed event handler. Set it to RELEASED, and stop the TTL Timer.
                         * IMROVEMENT: For now we use the Counter parameter as index. Maybe not correct? */
                        for (uint8 eh=0; eh < server->ServerServiceCfg->NoOfEventHandlers; eh++){
                            if (server->ServerServiceCfg->EventHandler[eh].EventGroupId == entry2->EventgroupID) {



                                server->EventHandlers[eh].FanOut[entry2->Counter].TTL_Timer_On = FALSE;
                                server->EventHandlers[eh].FanOut[entry2->Counter].TTL_Timer_Value_ms = 0;
                                server->EventHandlers[eh].NoOfSubscribers--;
                                if (server->EventHandlers[eh].NoOfSubscribers == 0) {

//									/** @ req 4.2.2/SWS_SD_00454 */

                                    /* IMPROVEMENT: More work is needed */

//									if (server->ServerServiceCfg->EventHandler[eh].Udp != NULL) {
//										if (server->ServerServiceCfg->EventHandler[eh].Udp->EventActivationRef != ACTIVATION_REF_NOT_SET) {
//
//											/* Disable routing */
//											(void)SoAd_DisableSpecificRouting
//												(server->ServerServiceCfg->EventHandler[eh].Udp->EventActivationRef,
//														server->ServerServiceCfg->UdpSocketConnectionGroupId);
//
//										}
//									} else {
//
//										/** @ req 4.2.2/SWS_SD_00453 */
//										if (server->ServerServiceCfg->EventHandler[eh].Tcp != NULL) {
//											if (server->ServerServiceCfg->EventHandler[eh].Tcp->EventActivationRef != ACTIVATION_REF_NOT_SET) {
//
//												/* Disable routing */
//												(void)SoAd_DisableSpecificRouting
//													(server->ServerServiceCfg->EventHandler[eh].Tcp->EventActivationRef,
//															server->ServerServiceCfg->TcpSocketConnectionGroupId);
//												}
//											}
//										}
//									}



                                    server->EventHandlers[eh].EventHandlerState = SD_EVENT_HANDLER_RELEASED;
                                    /* Change state for the EventHandler in BswM. */
                                    /* IMPROVEMENT: Do this only if current value is != RELEASED */
                                    BswM_Sd_EventHandlerCurrentState(server->ServerServiceCfg->EventHandler[eh].HandleId, SD_EVENT_HANDLER_RELEASED);
                                }
                                break;
                            }
                        }

                    }
                }
//			}



        }
        break;
    case SD_MAIN_PHASE:
        /* Step the EventHandler TTL timers */
        for (uint8 eh=0; eh < server->ServerServiceCfg->NoOfEventHandlers; eh++){
            if (server->EventHandlers[eh].EventHandlerState == SD_EVENT_HANDLER_REQUESTED){
                for(uint8 sub=0; sub < MAX_NO_OF_SUBSCRIBERS; sub++) {

                    if (server->EventHandlers[eh].FanOut[sub].TTL_Timer_On){
                        server->EventHandlers[eh].FanOut[sub].TTL_Timer_Value_ms -= SD_MAIN_FUNCTION_CYCLE_TIME_MS;
                        /** @req 4.2.2/SWS_SD_00403 */
                        if (server->EventHandlers[eh].FanOut[sub].TTL_Timer_Value_ms <= 0){
                            /* Timer expired */
                            server->EventHandlers[eh].FanOut[sub].TTL_Timer_On = FALSE;
                            server->EventHandlers[eh].FanOut[sub].TTL_Timer_Value_ms = 0;
                            server->EventHandlers[eh].NoOfSubscribers--;
                            if (server->EventHandlers[eh].NoOfSubscribers == 0) {
                                server->EventHandlers[eh].EventHandlerState = SD_EVENT_HANDLER_RELEASED;
                                /* Change state for the EventHandler in BswM. */
                                /* IMPROVEMENT: Do this only if current value is != RELEASED */
                                BswM_Sd_EventHandlerCurrentState(server->ServerServiceCfg->EventHandler[eh].HandleId, SD_EVENT_HANDLER_RELEASED);
                            }

                        }
                    }
                }

            }
        }

        /** @req 4.2.2/SWS_SD_00451 */
        if (server->ServerServiceCfg->TimerRef->OfferCyclicDelay_ms > 0) {
            /** @req 4.2.2/SWS_SD_00449 */
            if (!server->OfferCyclicDelayTimerOn) {
                /** @req 4.2.2/SWS_SD_00450 */
                /* Start OfferCyclicDelay Timer */
                server->OfferCyclicDelay_Timer_Value_ms = server->ServerServiceCfg->TimerRef->OfferCyclicDelay_ms;
                server->OfferCyclicDelayTimerOn = TRUE;
            }
            else
            {
                server->OfferCyclicDelay_Timer_Value_ms -= SD_MAIN_FUNCTION_CYCLE_TIME_MS;
                if (server->OfferCyclicDelay_Timer_Value_ms <= 0) {
                    /* Send OfferService Entry */
                    TransmitSdMessage(sd_instance, NULL, server, NULL, 0, SD_OFFER_SERVICE,NULL, FALSE); // IMPROVEMENT: Should ipaddress parameter be used
                    /* Reset Timer */
                    server->OfferCyclicDelay_Timer_Value_ms = server->ServerServiceCfg->TimerRef->OfferCyclicDelay_ms;
                }
            }
        }

        /** @req 4.2.2/SWS_SD_00343 */
        if (entry1 != (Sd_Entry_Type1_Services *) NULL){
            if ((entry1->Type == FIND_SERVICE_TYPE)) {
                /* Send OfferService Entry */
                TransmitSdMessage(sd_instance, NULL, server, NULL, 0, SD_OFFER_SERVICE,NULL,is_multicast); // IMPROVEMENT: Should ipaddress parameter be used
            }
        }

        /** @req 4.2.2/SWS_SD_00344 */
        if (entry2 != (Sd_Entry_Type2_EventGroups *) NULL){
            if ((entry2->Type == SUBSCRIBE_EVENTGROUP_TYPE) && (entry2->TTL > 0)) {

                boolean event_handler_found = FALSE;
                uint8 event_handler_index = 0;

                /* Find the subscribed event handler. Set it to REQUESTED, and start the TTL Timer for the subscriber
                 * IMROVEMENT: For now we use the Counter parameter as index. Maybe not correct? */
                for (uint8 eh=0; eh < server->ServerServiceCfg->NoOfEventHandlers; eh++){
                    if (server->ServerServiceCfg->EventHandler[eh].EventGroupId == entry2->EventgroupID) {
                        server->EventHandlers[eh].EventHandlerState = SD_EVENT_HANDLER_REQUESTED;
                        server->EventHandlers[eh].NoOfSubscribers++;
                        server->EventHandlers[eh].FanOut [entry2->Counter].TTL_Timer_Value_ms = entry2->TTL * 1000;
                        server->EventHandlers[eh].FanOut [entry2->Counter].TTL_Timer_On = TRUE;
                        /* Change state for the EventHandler in BswM. */
                        if (server->EventHandlers[eh].NoOfSubscribers == 1) {
                            BswM_Sd_EventHandlerCurrentState(server->ServerServiceCfg->EventHandler[eh].HandleId, SD_EVENT_HANDLER_REQUESTED);
                        }
                        event_handler_found = TRUE;
                        event_handler_index = eh;
                        break;
                    }
                }

                if (event_handler_found) {
                    /* Send SubscribeEventGroupAck Entry */
                    TransmitSdMessage(sd_instance, NULL, server, entry2, event_handler_index, SD_SUBSCRIBE_EVENTGROUP_ACK, &ipaddress,is_multicast);
                }

            }

            /** @req 4.2.2/SWS_SD_00345 */
            if (entry2 != (Sd_Entry_Type2_EventGroups *) NULL){
                if ((entry2->Type == STOP_SUBSCRIBE_EVENTGROUP_TYPE) && (entry2->TTL == 0)) {

                    /* Find the subscribed event handler. Set it to RELEASED, and stop the TTL Timer.
                     * IMROVEMENT: For now we use the Counter parameter as index. Maybe not correct? */
                    for (uint8 eh=0; eh < server->ServerServiceCfg->NoOfEventHandlers; eh++){
                        if (server->ServerServiceCfg->EventHandler[eh].EventGroupId == entry2->EventgroupID) {
                            server->EventHandlers[eh].FanOut[entry2->Counter].TTL_Timer_On = FALSE;
                            server->EventHandlers[eh].FanOut[entry2->Counter].TTL_Timer_Value_ms = 0;
                            server->EventHandlers[eh].NoOfSubscribers--;
                            if (server->EventHandlers[eh].NoOfSubscribers <= 0) {
                                server->EventHandlers[eh].EventHandlerState = SD_EVENT_HANDLER_RELEASED;
                                /* Change state for the EventHandler in BswM. */
                                /* IMPROVEMENT: Do this only if current value is != RELEASED */
                                BswM_Sd_EventHandlerCurrentState(server->ServerServiceCfg->EventHandler[eh].HandleId, SD_EVENT_HANDLER_RELEASED);
                            }
                            break;
                        }
                    }

                }
            }
        }

        /** @req 4.2.2/SWS_SD_00347 */
        if (!sd_instance->TxPduIpAddressAssigned) {
            server->Phase = SD_DOWN_PHASE; /* INITIAL_WAIT -> DOWN */

            /* Set all EventHandlersCurrentState to RELEASED */
            for (uint8 eh = 0; eh < server->ServerServiceCfg->NoOfEventHandlers;
                    eh++) {
                for (uint8 sub=0; sub < MAX_NO_OF_SUBSCRIBERS; sub++) {
                    server->EventHandlers[eh].FanOut[sub].TTL_Timer_On = FALSE;
                    server->EventHandlers[eh].FanOut[sub].TTL_Timer_Value_ms = 0;
                }
                server->EventHandlers[eh].NoOfSubscribers = 0;
                server->EventHandlers[eh].EventHandlerState = SD_EVENT_HANDLER_RELEASED;
                BswM_Sd_EventHandlerCurrentState(
                        server->ServerServiceCfg->EventHandler[eh].HandleId,
                        SD_EVENT_HANDLER_RELEASED);
            }

            /** @req 4.2.2/SWS_SD_00349 */
            /*  Call SoAd_DisableRouting(): IMPROVEMENT: Is this correct?*/
            (void)SoAd_DisableRouting(
                    server->ServerServiceCfg->ProvidedMethods.ServerServiceActivationRef);

            /** @req 4.2.2/SWS_SD_00605 */
            if (server->SocketConnectionOpened) {
                /* Call SoAd_CloseSoCon() for all socket connections
                 * in this server service instance */
                CloseSocketConnections(server, TRUE); // IMPROVEMENT: Investigate abort parameter
            }
            break;

        }

        /** @req 4.2.2/SWS_SD_00342 */
        /** @req 4.2.2/SWS_SD_00348 */
        if ((server->ServerServiceMode == SD_SERVER_SERVICE_DOWN)
                && sd_instance->TxPduIpAddressAssigned) {
            server->Phase = SD_DOWN_PHASE; /* INITIAL_WAIT -> DOWN */

            /* Send a StopOffer Message */
            TransmitSdMessage(sd_instance, NULL, server, NULL, 0, SD_STOP_OFFER_SERVICE, NULL, FALSE); // IMPROVEMENT: Should ipaddress parameter be used

            /* Set all EventHandlersCurrentState to RELEASED */
            for (uint8 eh = 0; eh < server->ServerServiceCfg->NoOfEventHandlers;
                    eh++) {
                for (uint8 sub=0; sub < MAX_NO_OF_SUBSCRIBERS; sub++) {
                    server->EventHandlers[eh].FanOut[sub].TTL_Timer_On = FALSE;
                    server->EventHandlers[eh].FanOut[sub].TTL_Timer_Value_ms = 0;
                }
                server->EventHandlers[eh].NoOfSubscribers = 0;
                server->EventHandlers[eh].EventHandlerState = SD_EVENT_HANDLER_RELEASED;
                BswM_Sd_EventHandlerCurrentState(
                        server->ServerServiceCfg->EventHandler[eh].HandleId,
                        SD_EVENT_HANDLER_RELEASED);
            }

            /** @req 4.2.2/SWS_SD_00349 */
            /*  Call SoAd_DisableRouting(): IMPROVEMENT: Is this correct?*/
            (void)SoAd_DisableRouting(
                    server->ServerServiceCfg->ProvidedMethods.ServerServiceActivationRef);

            /** @req 4.2.2/SWS_SD_00605 */
            if (server->SocketConnectionOpened) {
                /* Call SoAd_CloseSoCon() for all socket connections
                 * in this server service instance */
                CloseSocketConnections(server, TRUE); // IMPROVEMENT: Investigate abort parameter
            }

        }

        break;
    }
}



