////////////////////////////////////////////////////////////////////////////////
// /****************************************************************************
// **
// ** Copyright (C) 2015-2019 M-Way Solutions GmbH
// ** Contact: https://www.blureange.io/licensing
// **
// ** This file is part of the Bluerange/FruityMesh implementation
// **
// ** $BR_BEGIN_LICENSE:GPL-EXCEPT$
// ** Commercial License Usage
// ** Licensees holding valid commercial Bluerange licenses may use this file in
// ** accordance with the commercial license agreement provided with the
// ** Software or, alternatively, in accordance with the terms contained in
// ** a written agreement between them and M-Way Solutions GmbH.
// ** For licensing terms and conditions see https://www.bluerange.io/terms-conditions. For further
// ** information use the contact form at https://www.bluerange.io/contact.
// **
// ** GNU General Public License Usage
// ** Alternatively, this file may be used under the terms of the GNU
// ** General Public License version 3 as published by the Free Software
// ** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
// ** included in the packaging of this file. Please review the following
// ** information to ensure the GNU General Public License requirements will
// ** be met: https://www.gnu.org/licenses/gpl-3.0.html.
// **
// ** $BR_END_LICENSE$
// **
// ****************************************************************************/
////////////////////////////////////////////////////////////////////////////////

/*
 * The Connection manager manages all connections and schedules their packet
 * transmissions. It is the interface that must be used to send data.
 */

#pragma once

#include <GAPController.h>
#include <GATTController.h>
#include <BaseConnection.h>
#include <AppConnection.h>
#if IS_ACTIVE(CLC_MODULE)
#include <ClcAppConnection.h>
#endif
#include <MeshConnection.h>

typedef struct BaseConnections{
	u8 count;
	u32 connectionIndizes[TOTAL_NUM_CONNECTIONS];
} BaseConnections;
typedef struct MeshConnections{
	u8 count;
	MeshConnection* connections[TOTAL_NUM_CONNECTIONS];
} MeshConnections;
#if IS_ACTIVE(CLC_CONN)
typedef struct ClcAppConnections{
	u8 count;
	ClcAppConnection* connections[TOTAL_NUM_CONNECTIONS];
} ClcAppConnections;
#endif

typedef BaseConnection* (*ConnTypeResolver)(BaseConnection* oldConnection, BaseConnectionSendData* sendData, u8* data);

class ConnectionManager
{
	private:
		//Used within the send methods to put data
		void QueuePacket(BaseConnection* connection, u8* data, u16 dataLength, bool reliable) const;

		//Checks wether a successful connection is from a reestablishment
		BaseConnection* IsConnectionReestablishment(const GapConnectedEvent& connectedEvent) const;

		static constexpr u16 TIME_BETWEEN_TIME_SYNC_INTERVALS_DS = SEC_TO_DS(5);
		u16 timeSinceLastTimeSyncIntervalDs = 0;	//Let's not spam the connections with time syncs.

	public:
		ConnectionManager();
		static ConnectionManager& getInstance();

		//This method is called when empty buffers are available and there is data to send
		void fillTransmitBuffers() const;

		u8 freeMeshInConnections;
		u8 freeMeshOutConnections;

		BaseConnection* pendingConnection;

		u16 uniqueConnectionIdCounter; //Counts all created connections to assign "unique" ids

		u16 droppedMeshPackets;
		u16 sentMeshPacketsUnreliable;
		u16 sentMeshPacketsReliable;

		//ConnectionType Resolving
		void ResolveConnection(BaseConnection* oldConnection, BaseConnectionSendData* sendData, u8* data);


		BaseConnections GetBaseConnections(ConnectionDirection direction) const;
		MeshConnections GetMeshConnections(ConnectionDirection direction) const;
		BaseConnections GetConnectionsOfType(ConnectionType connectionType, ConnectionDirection direction) const;

		BaseConnection* allConnections[TOTAL_NUM_CONNECTIONS];

		i8 getFreeConnectionSpot() const;

		//Returns the connection that is currently doing a handshake or nullptr
		MeshConnection* GetConnectionInHandshakeState() const;

		void ConnectAsMaster(NodeId partnerId, fh_ble_gap_addr_t* address, u16 writeCharacteristicHandle, u16 connectionIv);

		void ForceDisconnectOtherMeshConnections(const MeshConnection* ignoreConnection, AppDisconnectReason appDisconnectReason) const;
		void ForceDisconnectOtherHandshakedMeshConnections(const MeshConnection* ignoreConnection, AppDisconnectReason appDisconnectReason) const;
		void ForceDisconnectAllConnections(AppDisconnectReason appDisconnectReason) const;

		int ReestablishConnections() const;

		//Functions used for sending messages
		void SendMeshMessage(u8* data, u16 dataLength, DeliveryPriority priority) const;

		void SendModuleActionMessage(MessageType messageType, ModuleId moduleId, NodeId toNode, u8 actionType, u8 requestHandle, const u8* additionalData, u16 additionalDataSize, bool reliable) const;

		void BroadcastMeshPacket(u8* data, u16 dataLength, DeliveryPriority priority, bool reliable) const;

		void RouteMeshData(BaseConnection* connection, BaseConnectionSendData* sendData, u8* data) const;
		void BroadcastMeshData(const BaseConnection* ignoreConnection, BaseConnectionSendData* sendData, u8* data, RoutingDecision routingDecision) const;

		//Call this to dispatch a message to the node and all modules, this method will perform some basic
		//checks first, e.g. if the receiver matches
		void DispatchMeshMessage(BaseConnection* connection, BaseConnectionSendData* sendData, connPacketHeader* packet, bool checkReceiver) const;

		//Internal use only, do not use
		//Can send packets as WRITE_REQ (required for some internal functionality) but can lead to problems with the SoftDevice
		void SendMeshMessageInternal(u8* data, u16 dataLength, DeliveryPriority priority, bool reliable, bool loopback, bool toMeshAccess) const;


		BaseConnection* GetConnectionFromHandle(u16 connectionHandle) const;
		BaseConnection* GetConnectionByUniqueId(u16 uniqueConnectionId) const;
		MeshConnection* GetMeshConnectionToPartner(NodeId partnerId) const;

		MeshConnection* GetMeshConnectionToShortestSink(const BaseConnection* excludeConnection) const;
		ClusterSize GetMeshHopsToShortestSink(const BaseConnection* excludeConnection) const;

		u16 GetPendingPackets() const;

		void SetMeshConnectionInterval(u16 connectionInterval) const;

		void DeleteConnection(BaseConnection* connection, AppDisconnectReason reason);

		//Connection callbacks
		void MessageReceivedCallback(BaseConnectionSendData* sendData, u8* data) const;

		//These methods can be accessed by the Connection classes

		//GAPController Handlers
		void GapConnectingTimeoutHandler(const GapTimeoutEvent & gapTimeoutEvent);
		void GapConnectionConnectedHandler(const GapConnectedEvent & connectedEvent);
		void GapConnectionEncryptedHandler(const GapConnectionSecurityUpdateEvent &connectionSecurityUpdateEvent);
		void GapConnectionDisconnectedHandler(const GapDisconnectedEvent& disconnectedEvent);

		//GATTController Handlers
		void ForwardReceivedDataToConnection(u16 connectionHandle, BaseConnectionSendData &sendData, u8* data);
		void GattsWriteEventHandler(const GattsWriteEvent& gattsWriteEvent);
		void GattcHandleValueEventHandler(const GattcHandleValueEvent& handleValueEvent);
		void GattDataTransmittedEventHandler(const GattDataTransmittedEvent& gattDataTransmitted);
		void GattcWriteResponseEventHandler(const GattcWriteResponseEvent& writeResponseEvent);
		void GATTServiceDiscoveredHandler(u16 connHandle, ble_db_discovery_evt_t &evt);
		void GattcTimeoutEventHandler(const GattcTimeoutEvent& gattcTimeoutEvent);

		void PacketSuccessfullyQueuedCallback(MeshConnection* connection, SizedData packetData) const;

		//Callbacks are kinda complicated, so we handle BLE events directly in this class
		void GapRssiChangedEventHandler(const GapRssiChangedEvent& rssiChangedEvent) const;
		void TimerEventHandler(u16 passedTimeDs);

		void ResetTimeSync();
		bool IsAnyConnectionCurrentlySyncing();
		void TimeSyncInitialReplyReceivedHandler(const TimeSyncInitialReply& reply);
		void TimeSyncCorrectionReplyReceivedHandler(const TimeSyncCorrectionReply& reply);

};

