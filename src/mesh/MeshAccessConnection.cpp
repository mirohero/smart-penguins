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

#include <MeshAccessConnection.h>
#include <MeshAccessModule.h>
#include <Node.h>
#include <Logger.h>
#include <Utility.h>
#include <GlobalState.h>
#include "ConnectionAllocator.h"

/**
 * The MeshAccessConnection provides access to a node through a connection that is manually encrypted using
 * AES-128 CCM with either the nodeKey, networkKey, userBaseKey or any derived userKey.
 * A special service is provided and a custom encryption handshake is done when setting up the connection.
 * The packets sent over this connection are in standard mesh format but encrypted, the connection will
 * decrypt and assemble split pakcets before relaying them.
 *
 * Reading and Writing is done using a tx and rx characteristic that are present on the peripheral side.
 * The central must activate notifications on the tx characteristic and can write to the rx characteristic.
 *
 * To establish a connection, the following has to be done:
 * 	- Central connects to peripheral
 * 	- Central discovers the MeshAccessService of the peripheral with its rx/tx characteristics and the cccd of the tx characteristic
 * 	- Central enables notifications on cccd of tx characteristic
 * 	- Peripheral will notice the enabled notification and will instantiate a MeshAccessConnection throught the ResolverConnections
 * 	- Central starts handshake by requesting a nonce
 * 	- Peripheral anwers with ANonce
 * 	- Central answers with SNonce in an encrypted packet (enables auto encrypt/decrypt)
 * 	- Peripheral checks encrypted packet, sends encrypted HandshakeDone packet and enables auto encrypt/decrypt
 *
 *	Encryption and MIC calculation uses three AES encryptions at the moment to prevent a discovered packet forgery
 *	attack under certain conditions. Future versions of the handshake may employ different encryption
 *
 * @param id
 * @param direction
 */

//Register the resolver for MeshAccessConnections
#ifndef SIM_ENABLED
uint32_t meshAccessConnTypeResolver __attribute__((section(".ConnTypeResolvers"), used)) = (u32)MeshAccessConnection::ConnTypeResolver;
#endif

MeshAccessConnection::MeshAccessConnection(u8 id, ConnectionDirection direction, fh_ble_gap_addr_t* partnerAddress, u32 fmKeyId, MeshAccessTunnelType tunnelType)
	: AppConnection(id, direction, partnerAddress)
{
	logt("MACONN", "New MeshAccessConnection");

	//Save correct connectionType
	this->connectionType = ConnectionType::MESH_ACCESS;
	this->fmKeyId = fmKeyId;
	CheckedMemset(this->key, 0x00, 16);
	this->useCustomKey = false;

	this->partnerRxCharacteristicHandle = 0;
	this->partnerTxCharacteristicCccdHandle = 0;
	this->partnerTxCharacteristicHandle = 0;

	if(direction != ConnectionDirection::DIRECTION_OUT){
		this->handshakeStartedDs = GS->appTimerDs;
	}

	this->lastProcessedMessageType = MessageType::INVALID;

	this->tunnelType = tunnelType;

	this->connectionStateSubscriberId = 0;

	//The partner is assigned a unique nodeId in our mesh network that is not already taken
	//This is only possible if less than NODE_ID_VIRTUAL_BASE nodes are in the network and if
	//the enrollment ensures that successive nodeIds are used
	this->virtualPartnerId = GS->node.configuration.nodeId + (this->connectionId+1) * NODE_ID_VIRTUAL_BASE;

	//Fetch the MeshAccessModule reference
	this->meshAccessMod = (MeshAccessModule*)GS->node.GetModuleById(ModuleId::MESH_ACCESS_MODULE);
	if(meshAccessMod != nullptr){
		this->meshAccessService = &meshAccessMod->meshAccessService;
	} else {
		this->meshAccessService = nullptr;
	}
}

//Can be used to use a custom key for connecting to a partner,
//should be called directly after constructing and before connecting
//Will not work if the partner starts the encryption handshake
void MeshAccessConnection::SetCustomKey(u8* key)
{
	memcpy(this->key, key, 16);
	useCustomKey = true;
}

MeshAccessConnection::~MeshAccessConnection(){
	logt("MACONN", "Deleted MeshAccessConnection");
}

BaseConnection* MeshAccessConnection::ConnTypeResolver(BaseConnection* oldConnection, BaseConnectionSendData* sendData, u8* data)
{
	//Check if data was written to our service rx characteristic
	MeshAccessModule* meshAccessMod = (MeshAccessModule*)GS->node.GetModuleById(ModuleId::MESH_ACCESS_MODULE);
	if(meshAccessMod != nullptr){
		if(
			sendData->characteristicHandle == meshAccessMod->meshAccessService.rxCharacteristicHandle.value_handle
			|| sendData->characteristicHandle == meshAccessMod->meshAccessService.txCharacteristicHandle.cccd_handle
		){
			return ConnectionAllocator::getInstance().allocateMeshAccessConnection(
					oldConnection->connectionId,
					oldConnection->direction,
					&oldConnection->partnerAddress,
					0, //fmKeyId unknown at this point, partner must query
					MeshAccessTunnelType::INVALID); //TunnelType also unknown
		}
	}

	return nullptr;
}


#define ________________________CONNECTION_________________________

u16 MeshAccessConnection::ConnectAsMaster(fh_ble_gap_addr_t* address, u16 connIntervalMs, u16 connectionTimeoutSec, u32 fmKeyId, u8* customKey, MeshAccessTunnelType tunnelType)
{
	//Only connect when not currently in another connection or when there are no more free connections
	if (GS->cm.pendingConnection != nullptr) return 0;

	//Check if we already have a MeshAccessConnection to this address and do not allow a second
	BaseConnections conns = GS->cm.GetConnectionsOfType(ConnectionType::MESH_ACCESS, ConnectionDirection::INVALID);
	for(u32 i=0; i<conns.count; i++)
	{
		BaseConnection* conn = GS->cm.allConnections[conns.connectionIndizes[i]];
		if (conn != nullptr) {
			u32 result = memcmp(&(conn->partnerAddress), address, FH_BLE_SIZEOF_GAP_ADDR);
			if (result == 0) {
				return 0;
			}
		}
	}

	//Create the connection and set it as pending, this is done before starting the GAP connect to avoid race conditions
	for (u32 i = 0; i < TOTAL_NUM_CONNECTIONS; i++){
		if (GS->cm.allConnections[i] == nullptr){
			MeshAccessConnection* conn = ConnectionAllocator::getInstance().allocateMeshAccessConnection(i, ConnectionDirection::DIRECTION_OUT, address, fmKeyId, tunnelType);
			GS->cm.pendingConnection = GS->cm.allConnections[i] = conn;

			//Set the timeout big enough so that it is not killed by the ConnectionManager
			conn->handshakeStartedDs = GS->appTimerDs + SEC_TO_DS(connectionTimeoutSec + 2);

			//If customKey is not nullptr and not set to FF:FF...., we use it
			if(customKey != nullptr && !Utility::CompareMem(0xFF, customKey, 16)){
				((MeshAccessConnection*)GS->cm.pendingConnection)->SetCustomKey(customKey);
			}
			break;
		}
	}
	if(GS->cm.pendingConnection == nullptr){
		logt("ERROR", "No free connection");
		return 0;
	}

	//Tell the GAP Layer to connect, it will return if it is trying or if there was an error
	u32 err = GS->gapController.connectToPeripheral(*address, MSEC_TO_UNITS(connIntervalMs, UNIT_1_25_MS), connectionTimeoutSec);

	if (err == FruityHal::SUCCESS)
	{
		logt("MACONN", "Trying to connect");
		return GS->cm.pendingConnection->uniqueConnectionId;
	} else {
		//Clean the connection that has just been created
		GS->cm.DeleteConnection(GS->cm.pendingConnection, AppDisconnectReason::GAP_ERROR);
	}

	return 0;
}


#define ________________________HANDSHAKE_________________________

//Discovery example: https://devzone.nordicsemi.com/question/119274/sd_ble_gattc_characteristics_discover-only-discovers-one-characteristic/

// The Central must register for notifications on the tx characteristic of the peripheral
void MeshAccessConnection::RegisterForNotifications()
{
	logt("MACONN", "Registering for notifications");

    u16 data = 0x0001; //Bit to enable the notifications

    u32 err = GS->gattController.bleWriteCharacteristic(connectionHandle, partnerTxCharacteristicCccdHandle, (u8*)&data, 2, true);
    if(err == 0){
    	manualPacketsSent++;
    	reliableBuffersFree--;
    }

    //After the write REQ for enabling notifications was queued, we can safely send data
    StartHandshake(fmKeyId);
}

//This method is called by the Central and will start the encryption handshake
void MeshAccessConnection::StartHandshake(u16 fmKeyId)
{
	if(connectionState >= ConnectionState::HANDSHAKING) return;

	logt("MACONN", "-- TX Start Handshake");

	//Save the fmKeyId that we want to use
	this->fmKeyId = fmKeyId;

	connectionState = ConnectionState::HANDSHAKING;
	handshakeStartedDs = GS->appTimerDs; //Refresh handshake timer
	//C=>P: Type=RequestANuonce, fmKeyId=#,Authorize(true/false), Authenticate(true/false)

	connPacketEncryptCustomStart packet;
	CheckedMemset(&packet, 0x00, sizeof(connPacketEncryptCustomStart));
	packet.header.messageType = MessageType::ENCRYPT_CUSTOM_START;
	packet.header.sender = GS->node.configuration.nodeId;
	packet.header.receiver = virtualPartnerId;
	packet.version = 1;
	packet.fmKeyId = fmKeyId;
	packet.tunnelType = (u8)tunnelType;

	SendData(
		(u8*)&packet,
		SIZEOF_CONN_PACKET_ENCRYPT_CUSTOM_START,
		DeliveryPriority::MESH_INTERNAL_HIGH,
		false);
}

//This method is called by the peripheral after the Encryption Start Handshake packet was received
void MeshAccessConnection::HandshakeANonce(connPacketEncryptCustomStart* inPacket){
	//Process Starthandshake packet
	//P=>C: Type=ANouce (Will stay the same random number until attempt was made), supportedKeyIds=1,2,345,56,...,supportsAuthenticate(true/false)

	logt("MACONN", "-- TX ANonce, fmKeyId %u", inPacket->fmKeyId);

	connectionState = ConnectionState::HANDSHAKING;

	//C=>P: Type=RequestANuonce, fmKeyId=#,Authorize(true/false), Authenticate(true/false)

	//We do not want to accept certain key types
	fmKeyId = inPacket->fmKeyId;
	partnerId = inPacket->header.sender;

	if (partnerId == NODE_ID_BROADCAST){
		logt("ERROR", "Wrong partnerId");
		DisconnectAndRemove(AppDisconnectReason::WRONG_PARTNERID);
		return;
	}

	//The tunnel type is the opposite of the partners tunnel type
	if(inPacket->tunnelType == (u8)MeshAccessTunnelType::PEER_TO_PEER){
			tunnelType = MeshAccessTunnelType::PEER_TO_PEER;
	} else if(inPacket->tunnelType == (u8)MeshAccessTunnelType::LOCAL_MESH){
		tunnelType = MeshAccessTunnelType::REMOTE_MESH;
	} else if(inPacket->tunnelType == (u8)MeshAccessTunnelType::REMOTE_MESH){
		tunnelType = MeshAccessTunnelType::LOCAL_MESH;
	} else {
		logt("ERROR", "Illegal TunnelType %u", (u32)inPacket->tunnelType);
		DisconnectAndRemove(AppDisconnectReason::ILLEGAL_TUNNELTYPE);
		return;
	}

	connPacketEncryptCustomANonce packet;
	CheckedMemset(&packet, 0x00, sizeof(connPacketEncryptCustomANonce));
	packet.header.messageType = MessageType::ENCRYPT_CUSTOM_ANONCE;
	packet.header.sender = GS->node.configuration.nodeId;
	packet.header.receiver = virtualPartnerId;

	decryptionNonce[0] = packet.anonce[0] = Utility::GetRandomInteger();
	decryptionNonce[1] = packet.anonce[1] = Utility::GetRandomInteger();

	//Generate the session key for decryption
	bool keyValid = GenerateSessionKey((u8*)decryptionNonce, partnerId, fmKeyId, sessionDecryptionKey);

	if(!keyValid){
		logt("ERROR", "Invalid Key");
		DisconnectAndRemove(AppDisconnectReason::INVALID_KEY);
		return;
	}

	SendData(
		(u8*)&packet,
		SIZEOF_CONN_PACKET_ENCRYPT_CUSTOM_ANONCE,
		DeliveryPriority::MESH_INTERNAL_HIGH,
		false);

	//Set encryption state to encrypted because we await the next packet to be encrypted
	encryptionState = EncryptionState::ENCRYPTED;
}

//This method is called by the Central after the ANonce was received
void MeshAccessConnection::HandshakeSNonce(connPacketEncryptCustomANonce* inPacket)
{
	logt("MACONN", "-- TX SNonce, anonce %u", inPacket->anonce[1]);

	// Process Handshake ANonce
	// C=>P: EncS(StartEncryptCustom, SNonce),MIC

	partnerId = inPacket->header.sender;

	//Save the partners nonce for use as encryption nonce
	encryptionNonce[0] = inPacket->anonce[0];
	encryptionNonce[1] = inPacket->anonce[1];

	//Send an encrypted packet containing the sNonce
	const u8 len = SIZEOF_CONN_PACKET_ENCRYPT_CUSTOM_SNONCE + MESH_ACCESS_MIC_LENGTH;
	u8 buffer[len];
	CheckedMemset(buffer, 0x00, len);
	connPacketEncryptCustomSNonce* packet = (connPacketEncryptCustomSNonce*)buffer;
	packet->header.messageType = MessageType::ENCRYPT_CUSTOM_SNONCE;
	packet->header.sender = GS->node.configuration.nodeId;
	packet->header.receiver = virtualPartnerId;

	//Save self-generated nonce to decrypt packets
	decryptionNonce[0] = packet->snonce[0] = Utility::GetRandomInteger();
	decryptionNonce[1] = packet->snonce[1] = Utility::GetRandomInteger();

	//Generate the session keys for encryption and decryption
	bool keyValidA = GenerateSessionKey((u8*)encryptionNonce, GS->node.configuration.nodeId, fmKeyId, sessionEncryptionKey);
	bool keyValidB = GenerateSessionKey((u8*)decryptionNonce, GS->node.configuration.nodeId, fmKeyId, sessionDecryptionKey);

	if(!keyValidA || !keyValidB){
		logt("ERROR", "Invalid Key %u %u", (u32)keyValidA, (u32)keyValidB);
		DisconnectAndRemove(AppDisconnectReason::INVALID_KEY);
		return;
	}

	//Log encryption and decryption keys
	TO_HEX(sessionEncryptionKey, 16);
	TO_HEX(sessionDecryptionKey, 16);
	logt("MACONN", "EncrKey: %s", sessionEncryptionKeyHex);
	logt("MACONN", "DecrKey: %s", sessionDecryptionKeyHex);

	//Pay attention that we must only increment the encryption counter once the
	//message is placed in the SoftDevice, otherwise we will break the message flow


	//Set encryption state to encrypted because we await the next packet to be encrypted, our next one is as well
	encryptionState = EncryptionState::ENCRYPTED;

	SendData(
		(u8*)packet,
		SIZEOF_CONN_PACKET_ENCRYPT_CUSTOM_SNONCE,
		DeliveryPriority::MESH_INTERNAL_HIGH,
		false);

	connectionState = ConnectionState::HANDSHAKE_DONE;

	//Needed by our packet splitting methods, payload is now less than before because of MIC
	connectionPayloadSize = connectionMtu - MESH_ACCESS_MIC_LENGTH;

	//Send the current mesh state to our partner
	SendClusterState();

	NotifyConnectionStateSubscriber(ConnectionState::HANDSHAKE_DONE);
}

//This method is called by the Peripheral after the SNonce was received
void MeshAccessConnection::HandshakeDone(connPacketEncryptCustomSNonce* inPacket)
{

	logt("MACONN", "-- TX Handshake Done, snonce %u", encryptionNonce[1]);

	// Process Handshake SNonce
	// P=>C: EncS(EncryptionSuccessful)+MIC

	//Save nonce to encrypt packets for partner
	encryptionNonce[0] = inPacket->snonce[0];
	encryptionNonce[1] = inPacket->snonce[1];

	//Generate key for encryption
	bool keyValid = GenerateSessionKey((u8*)encryptionNonce, partnerId, fmKeyId, sessionEncryptionKey);

	if(!keyValid){
		logt("ERROR", "Invalid Key in HD");
		DisconnectAndRemove(AppDisconnectReason::INVALID_KEY);
		return;
	}

	//Log encryption and decryption keys
	TO_HEX(sessionEncryptionKey, 16);
	TO_HEX(sessionDecryptionKey, 16);
	logt("MACONN", "EncrKey: %s", sessionEncryptionKeyHex);
	logt("MACONN", "DecrKey: %s", sessionDecryptionKeyHex);

	//Send an encrypted packet to say that we are done
	connPacketEncryptCustomDone packet;
	CheckedMemset(&packet, 0x00, sizeof(connPacketEncryptCustomDone));
	packet.header.messageType = MessageType::ENCRYPT_CUSTOM_DONE;
	packet.header.sender = GS->node.configuration.nodeId;
	packet.header.receiver = virtualPartnerId;
	packet.status = FruityHal::SUCCESS;

	//From now on, we can just send data the normal way and the encryption is done automatically
	SendData(
		(u8*)&packet,
		SIZEOF_CONN_PACKET_ENCRYPT_CUSTOM_DONE,
		DeliveryPriority::MESH_INTERNAL_HIGH,
		false);

	connectionState = ConnectionState::HANDSHAKE_DONE;

	//Needed by our packet splitting methods, payload is now less than before because of MIC
	connectionPayloadSize = connectionMtu - MESH_ACCESS_MIC_LENGTH;

	//Send the current mesh state to our partner
	SendClusterState();

	NotifyConnectionStateSubscriber(ConnectionState::HANDSHAKE_DONE);
}

void MeshAccessConnection::SendClusterState()
{
	connPacketClusterInfoUpdate packet;
	CheckedMemset(&packet, 0, sizeof(connPacketClusterInfoUpdate));
	packet.header.messageType = MessageType::CLUSTER_INFO_UPDATE;
	packet.header.sender = GS->node.configuration.nodeId;
	packet.header.receiver = NODE_ID_BROADCAST;

	packet.payload.clusterSizeChange = GS->node.clusterSize;
	packet.payload.connectionMasterBitHandover = GS->node.HasAllMasterBits();
	packet.payload.hopsToSink = GS->cm.GetMeshHopsToShortestSink(nullptr);

	SendData((u8*)&packet, sizeof(connPacketClusterInfoUpdate), DeliveryPriority::LOW, false);
}

void MeshAccessConnection::NotifyConnectionStateSubscriber(ConnectionState state) const
{
	if(connectionStateSubscriberId != 0)
	{
		MeshAccessModuleConnectionStateMessage data;
		data.vPartnerId = virtualPartnerId;
		data.state = (u8)state;

		GS->cm.SendModuleActionMessage(
			MessageType::MODULE_GENERAL,
			ModuleId::MESH_ACCESS_MODULE,
			connectionStateSubscriberId,
			(u8)MeshAccessModuleGeneralMessages::MA_CONNECTION_STATE,
			0, //TODO: maybe store the request handle and send it back here?
			(u8*)&data,
			SIZEOF_MA_MODULE_CONNECTION_STATE_MESSAGE,
			false
		);
	}

}

#define ________________________ENCRYPTION_________________________

//Session Key S generated as Enc#(Anonce, nodeIndex); Enc# is the chosen key

bool MeshAccessConnection::GenerateSessionKey(u8* nonce, NodeId centralNodeId, u32 fmKeyId, u8* keyOut)
{
	u8 ltKey[16];

	if(useCustomKey){
		logt("MACONN", "Using custom key");
		memcpy(ltKey, key, 16);
	} else if(fmKeyId == FM_KEY_ID_ZERO
			&& meshAccessMod->IsZeroKeyConnectable(direction)) {
		//If the fmKeyId is FM_KEY_ID_ZERO and we allow unsecure connections, we use
		//the zero encryption key (basically no encryption) if we are not enrolled or 
		//we are the one opening the connection.
		logt("MACONN", "Using key none");
		CheckedMemset(ltKey, 0x00, 16);
	} else if(fmKeyId == FM_KEY_ID_NODE) {
		logt("MACONN", "Using node key");
		memcpy(ltKey, RamConfig->GetNodeKey(), 16);
	} else if(fmKeyId == FM_KEY_ID_NETWORK) {
		logt("MACONN", "Using network key");
		memcpy(ltKey, GS->node.configuration.networkKey, 16);
	} else if(fmKeyId == FM_KEY_ID_ORGANIZATION) {
		logt("MACONN", "Using orga key");
		memcpy(ltKey, GS->node.configuration.organizationKey, 16);
	} else if (fmKeyId == FM_KEY_ID_RESTRAINED) {
		logt("MACONN", "Using restrained key");
		RamConfig->GetRestrainedKey(ltKey);
	}
	else if(fmKeyId >= FM_KEY_ID_USER_DERIVED_START && fmKeyId <= FM_KEY_ID_USER_DERIVED_END)
	{
		logt("MACONN", "Using derived user key %u", fmKeyId);
		//Construct some cleartext with the user id to construct the user key
		u8 cleartext[16];
		CheckedMemset(cleartext, 0x00, 16);
		memcpy(cleartext, &fmKeyId, 4);


		Utility::Aes128BlockEncrypt(
				(Aes128Block*)cleartext,
				(Aes128Block*)GS->node.configuration.userBaseKey,
				(Aes128Block*)ltKey);

	}
	else {
		logt("MACONN", "Invalid key generated");
		//No key
		CheckedMemset(keyOut, 0x00, 16);
		return false;
	}

	// TO_HEX(ltKey, 16);
	// logt("ERROR", "LongTerm Key is %s", ltKeyHex);

	//Check if Long Term Key is empty
	if(Utility::CompareMem(0xFF, ltKey, 16)){
		logt("ERROR", "Key was empty, can not be used");
		return false;
	}

	//Generate cleartext with NodeId and ANonce
	u8 cleartext[16];
	CheckedMemset(cleartext, 0x00, 16);
	memcpy(cleartext, (u8*)&centralNodeId, 2);
	memcpy(((u8*)cleartext) + 2, nonce, MESH_ACCESS_HANDSHAKE_NONCE_LENGTH);

//	TO_HEX(cleartext, 16);
//	logt("ERROR", "SessionKeyCleartext %s", cleartextHex);

	//Encrypt with our chosen Long Term Key
	Utility::Aes128BlockEncrypt(
			(Aes128Block*)cleartext,
			(Aes128Block*)ltKey,
			(Aes128Block*)keyOut);

	return true;
}

/**
 * Encryption is done using a counter chaining mode with AES.
 * The nonce/counter + padding is encrypted with the session key to generate a keystream. This keystream is
 * then xored with the cleartext to produce a ciphertext of variable length.
 * To calculate the MIC, the nonce/counter is incremented, then it is xored with the ciphertext of the message
 * before being encrypted with the session key. The first bytes of this nonce+message ciphertext are then
 * used as the MIC which is appended to the end of the data
 *
 * @param data[in/out] must be big enough to hold the additional bytes for the MIC which is placed at the end
 * @param dataLength[in]
 */
void MeshAccessConnection::EncryptPacket(u8* data, u16 dataLength)
{
	TO_HEX(data, dataLength);
	logt("MACONN", "Encrypting %s (%u) with nonce %u", dataHex, dataLength, encryptionNonce[1]);

	u8 cleartext[16];
	u8 keystream[16];
	u8 ciphertext[16];

	//Generate keystream with nonce
	CheckedMemset(cleartext, 0x00, 16);
	memcpy(cleartext, encryptionNonce, MESH_ACCESS_HANDSHAKE_NONCE_LENGTH);
	Utility::Aes128BlockEncrypt(
			(Aes128Block*)cleartext,
			(Aes128Block*)sessionEncryptionKey,
			(Aes128Block*)keystream);

//	TO_HEX(keystream, 16);
//	logt("ERROR", "Encryption Keystream %s", keystreamHex);

	//Xor cleartext with keystream to get the ciphertext
	memcpy(cleartext, data, dataLength);
	Utility::XorBytes(keystream, cleartext, 16, ciphertext);
	memcpy(data, ciphertext, dataLength);

	//Increment nonce being used as a counter
	encryptionNonce[1]++;

	//Generate a new Keystream with an updated counter for MIC calculateion
	CheckedMemset(cleartext, 0x00, 16);
	memcpy(cleartext, encryptionNonce, MESH_ACCESS_HANDSHAKE_NONCE_LENGTH);
	Utility::Aes128BlockEncrypt( //encrypts nonce
				(Aes128Block*)cleartext,
				(Aes128Block*)sessionEncryptionKey,
				(Aes128Block*)keystream);

	//To generate the MIC, we xor the new keystream with our cleartext and encrypt it again
	//we therefore create a pair that cannot be reproduced by an attacker (hopefully :-))
	CheckedMemset(cleartext, 0x00, 16);
	memcpy(cleartext, data, dataLength);
	Utility::XorBytes(keystream, cleartext, 16, cleartext);
	Utility::Aes128BlockEncrypt(
			(Aes128Block*)cleartext,
			(Aes128Block*)sessionEncryptionKey,
			(Aes128Block*)keystream);

//	//Log the keystream generated by the nonce, 4 bytes of keystream are used as MIC
//	u8 keystream2[16];
//	memcpy(keystream2, keystream, 16);
//	TO_HEX(keystream2, 16);
//	logt("ERROR", "MIC nonce %u produces Keystream %s", encryptionNonce[1], keystream2Hex);

	//Reset nonce, it is incremented once the packet was successfully queued with the softdevice
	encryptionNonce[1]--;

	//Copy nonce to the end of the packet
	u8* micPtr = data + dataLength;
	memcpy(micPtr, keystream, MESH_ACCESS_MIC_LENGTH);

	//Log the encrypted packet
	DYNAMIC_ARRAY(data2, dataLength + MESH_ACCESS_MIC_LENGTH);
	memcpy(data2, data, dataLength + MESH_ACCESS_MIC_LENGTH);
	TO_HEX(data2, dataLength + MESH_ACCESS_MIC_LENGTH);
	logt("MACONN", "Encrypted as %s (%u)", data2Hex, dataLength + MESH_ACCESS_MIC_LENGTH);
}

bool MeshAccessConnection::DecryptPacket(u8* data, u16 dataLength)
{
	if(dataLength < 4) return false;

	TO_HEX(data, dataLength);
	logt("MACONN", "Decrypting %s (%u) with nonce %u", dataHex, dataLength, decryptionNonce[1]);

	u8 cleartext[16];
	u8 keystream[16];
	u8 ciphertext[16];

	//We need to calculate the MIC from the ciphertext as was done by the sender
	decryptionNonce[1]++;

	//Generate a keystream from the nonce
	CheckedMemset(cleartext, 0x00, 16);
	memcpy(cleartext, decryptionNonce, MESH_ACCESS_HANDSHAKE_NONCE_LENGTH);
	Utility::Aes128BlockEncrypt( //encrypts nonce
				(Aes128Block*)cleartext,
				(Aes128Block*)sessionDecryptionKey,
				(Aes128Block*)keystream);

	//Xor the keystream with the ciphertext
	CheckedMemset(ciphertext, 0x00, 16);
	memcpy(ciphertext, data, dataLength - MESH_ACCESS_MIC_LENGTH);
	Utility::XorBytes(ciphertext, keystream, 16, cleartext);
	//Encrypt the resulting cleartext
	Utility::Aes128BlockEncrypt(
			(Aes128Block*)cleartext,
			(Aes128Block*)sessionDecryptionKey,
			(Aes128Block*)keystream);

	//Check if the two MICs match
	u8* micPtr = data + (dataLength - MESH_ACCESS_MIC_LENGTH);
	u32 micCheck = memcmp(keystream, micPtr, MESH_ACCESS_MIC_LENGTH);

	//Reset decryptionNonce for decrypting the message
	decryptionNonce[1]--;

	//Generate keystream with nonce
	CheckedMemset(cleartext, 0x00, 16);
	memcpy(cleartext, decryptionNonce, MESH_ACCESS_HANDSHAKE_NONCE_LENGTH);
	Utility::Aes128BlockEncrypt(
			(Aes128Block*)cleartext,
			(Aes128Block*)sessionDecryptionKey,
			(Aes128Block*)keystream);

//	TO_HEX(keystream, 16);
//	logt("ERROR", "Keystream %s", keystreamHex);

	//Xor keystream with ciphertext to retrieve original message
	Utility::XorBytes(keystream, data, dataLength - MESH_ACCESS_MIC_LENGTH, data);

	//Increment nonce being used as a counter
	decryptionNonce[1] += 2;

//	u8 keystream2[16];
//	memcpy(keystream2, keystream, 16);
//	TO_HEX(keystream2, 16);
//	logt("ERROR", "MIC nonce %u, Keystream %s", decryptionNonce[1], keystream2Hex);


	TO_HEX_2(data, dataLength - MESH_ACCESS_MIC_LENGTH);
	logt("MACONN", "Decrypted as %s (%u) micValid %u", dataHex, dataLength - MESH_ACCESS_MIC_LENGTH, micCheck == 0);

	return micCheck == 0;
}


#define ________________________SEND________________________

//This function might modify the packet, can also split bigger packets
SizedData MeshAccessConnection::ProcessDataBeforeTransmission(BaseConnectionSendData* sendData, u8* data, u8* packetBuffer)
{
	//Use the split packet from the BaseConnection to process all packets
	SizedData splitData = GetSplitData(*sendData, data, packetBuffer);

	//We must save the message type before encrypting because we need to know if the
	//packet was queued in the softdevice for packet splitting
	lastProcessedMessageType = ((connPacketHeader*)splitData.data)->messageType;

	//Encrypt packets after splitting if necessary
	if(encryptionState == EncryptionState::ENCRYPTED){
		//We use the given packetBuffer to store the encrypted packet + its MIC
		memcpy(packetBuffer, splitData.data, splitData.length);
		EncryptPacket(packetBuffer, splitData.length);

		splitData.data = packetBuffer;
		splitData.length += MESH_ACCESS_MIC_LENGTH;
	}

	return splitData;
}

bool MeshAccessConnection::SendData(u8* data, u16 dataLength, DeliveryPriority priority, bool reliable)
{
	if (dataLength > MAX_MESH_PACKET_SIZE) {
		SIMEXCEPTION(PaketTooBigException);
		logt("ERROR", "Packet too big for sending!");
		return false;
	}

	if(meshAccessService == nullptr) return false;

	BaseConnectionSendData sendData;

	if(direction == ConnectionDirection::DIRECTION_OUT)
	{
		//The central can write the data to the rx characteristic of the peripheral
		sendData.characteristicHandle = partnerRxCharacteristicHandle;
		sendData.dataLength = (u8)dataLength;
		sendData.deliveryOption = reliable ? DeliveryOption::WRITE_REQ : DeliveryOption::WRITE_CMD;
		sendData.priority = priority;
	}
	else
	{
		//The peripheral must send data as notifications from its tx characteristic
		sendData.characteristicHandle = meshAccessService->txCharacteristicHandle.value_handle;
		sendData.dataLength = (u8)dataLength;
		sendData.deliveryOption = DeliveryOption::NOTIFICATION;
		sendData.priority = priority;
	}

	return SendData(&sendData, data);
}


//This is the generic method for sending data
bool MeshAccessConnection::SendData(BaseConnectionSendData* sendData, u8* data)
{
	connPacketHeader* packetHeader = (connPacketHeader*)data;

	logt("MACONN", "MA SendData from %u to %u", packetHeader->sender, packetHeader->receiver);

	MeshAccessAuthorization auth = meshAccessMod->CheckAuthorizationForAll(sendData, data, fmKeyId, DataDirection::DIRECTION_OUT);

	//Block other packets as long as handshake is not done
	if(
		connectionState < ConnectionState::HANDSHAKE_DONE
		&& (packetHeader->messageType < MessageType::ENCRYPT_CUSTOM_START
		|| packetHeader->messageType > MessageType::ENCRYPT_CUSTOM_DONE)
	){
		return false;
	}

	if(packetHeader->receiver == partnerId){
		logt("MACONN", "Potential wrong destination id, please send to virtualPartnerId");
	}

	//Only allow packets to the virtual partner Id or broadcast
	if(
		tunnelType == MeshAccessTunnelType::PEER_TO_PEER
		|| tunnelType == MeshAccessTunnelType::LOCAL_MESH
	){
		//Do not send packets address to nodes in our mesh, only broadcast or packets addressed to its virtual id
		if(
			packetHeader->receiver > NODE_ID_DEVICE_BASE
			&& packetHeader->receiver < NODE_ID_GROUP_BASE
			&& packetHeader->receiver != this->virtualPartnerId)
		{
			logt("MACONN", "Not sending");
			return false;
		}

		//Before sending it to our partner, we change the virtual receiver id
		//that was used in our mesh to his normal nodeId
		if(packetHeader->receiver == this->virtualPartnerId){
			packetHeader->receiver = this->partnerId; //FIXME: Must not modify id here, copy packet first to queue
		}

		//Put packet in the queue for sending
		if(auth != MeshAccessAuthorization::UNDETERMINED && auth != MeshAccessAuthorization::BLACKLIST){
			return QueueData(*sendData, data);
		} else {
			return false;
		}

	} else if (tunnelType == MeshAccessTunnelType::REMOTE_MESH)
	{
		if(packetHeader->receiver == this->virtualPartnerId){
			packetHeader->receiver = this->partnerId; //FIXME: Must not modify id here, copy packet first to queue
		}

		//Put packet in the queue for sending
		if(auth != MeshAccessAuthorization::UNDETERMINED && auth != MeshAccessAuthorization::BLACKLIST){
			return QueueData(*sendData, data);
		} else {
			return false;
		}
	//We must allow handshake packets
	} else if (packetHeader->messageType >= MessageType::ENCRYPT_CUSTOM_START && packetHeader->messageType <= MessageType::ENCRYPT_CUSTOM_DONE)
	{
		//Put packet in the queue for sending
		if(auth != MeshAccessAuthorization::UNDETERMINED && auth != MeshAccessAuthorization::BLACKLIST){
			return QueueData(*sendData, data);
		} else {
			return false;
		}
	}

	return false;
}

//Because we are using packet splitting, we must handle packetSendPosition and Discarding here
void MeshAccessConnection::PacketSuccessfullyQueuedWithSoftdevice(PacketQueue* queue, BaseConnectionSendDataPacked* sendDataPacked, u8* data, SizedData* sentData)
{
	//The queued packet might be encrypted, so we must rely on the saved messageType that is saved
	//by the ProcessDataBeforeTransmission method

	if(encryptionState == EncryptionState::ENCRYPTED){
		encryptionNonce[1] += 2;
	}

	//If this was an intermediate split packet
	if (lastProcessedMessageType == MessageType::SPLIT_WRITE_CMD) {
		queue->packetSendPosition++;
		packetSendQueue.packetSentRemaining++;
	}
	//The end of a split packet
	else if (lastProcessedMessageType == MessageType::SPLIT_WRITE_CMD_END) {
		queue->packetSendPosition = 0;
		packetSendQueue.packetSentRemaining++;

		//Save a queue handle for that packet
		HandlePacketQueued(queue, sendDataPacked);
	}
	//If this was a normal packet
	else {
		queue->packetSendPosition = 0;

		//Discard the last packet because it was now successfully sent
		HandlePacketQueued(queue, sendDataPacked);
	}
}

#define ________________________RECEIVE________________________

//Check if encryption was started, and if yes, decrypt all packets before passing them to
//other functions, deal with the handshake packets as well
void MeshAccessConnection::ReceiveDataHandler(BaseConnectionSendData* sendData, u8* data)
{
	if(
		meshAccessMod == nullptr
		|| meshAccessService == nullptr
		|| (direction == ConnectionDirection::DIRECTION_OUT && partnerTxCharacteristicHandle != sendData->characteristicHandle)
		|| (direction == ConnectionDirection::DIRECTION_IN && meshAccessService->rxCharacteristicHandle.value_handle != sendData->characteristicHandle)
	){
		return;
	}

	//Check if packet must be decrypted first
	if(encryptionState == EncryptionState::ENCRYPTED){
		bool valid = DecryptPacket(data, sendData->dataLength);
		sendData->dataLength -= MESH_ACCESS_MIC_LENGTH;

		if(!valid){
			//Disconnect connection if a packet was received that is not valid
			logt("ERROR", "Invalid packet");
			DisconnectAndRemove(AppDisconnectReason::INVALID_PACKET);
			return;
		}
	}

	connPacketHeader* packetHeader = (connPacketHeader*)data;

	if(connectionState == ConnectionState::CONNECTED)
	{
		if(sendData->dataLength == SIZEOF_CONN_PACKET_ENCRYPT_CUSTOM_START && packetHeader->messageType == MessageType::ENCRYPT_CUSTOM_START){
			HandshakeANonce((connPacketEncryptCustomStart*) data);
		} else {
			logt("ERROR", "Wrong handshake packet");
			DisconnectAndRemove(AppDisconnectReason::INVALID_PACKET);
		}
	}
	else if(connectionState == ConnectionState::HANDSHAKING)
	{
		if(sendData->dataLength == SIZEOF_CONN_PACKET_ENCRYPT_CUSTOM_ANONCE && packetHeader->messageType == MessageType::ENCRYPT_CUSTOM_ANONCE){
			HandshakeSNonce((connPacketEncryptCustomANonce*) data);
		}
		else if(sendData->dataLength == SIZEOF_CONN_PACKET_ENCRYPT_CUSTOM_SNONCE && packetHeader->messageType == MessageType::ENCRYPT_CUSTOM_SNONCE){
			HandshakeDone((connPacketEncryptCustomSNonce*) data);
		} else {
			logt("ERROR", "Wrong handshake packet");
			DisconnectAndRemove(AppDisconnectReason::INVALID_PACKET);
		}
	}
	else if(connectionState == ConnectionState::HANDSHAKE_DONE){
		//This will reassemble the data for us
		data = ReassembleData(sendData, data);

		//If the data is null, the packet has not been fully reassembled
		if(data != nullptr){
			//Call our message received handler
			ReceiveMeshAccessMessageHandler(sendData, data);
		}
	}
}

void MeshAccessConnection::ReceiveMeshAccessMessageHandler(BaseConnectionSendData* sendData, u8* data)
{
	//We must change the sender because our partner might have a nodeId clash within our network
	connPacketHeader* packetHeader = (connPacketHeader*)data;

	//Some special handling for timestamp updates
	if(packetHeader->messageType == MessageType::UPDATE_TIMESTAMP)
	{
		//Set our time to the received timestamp
		connPacketUpdateTimestamp* packet = (connPacketUpdateTimestamp*)data;
		if (sendData->dataLength >= offsetof(connPacketUpdateTimestamp, offset) + sizeof(packet->offset))
		{
			GS->timeManager.SetTime(packet->timestampSec, 0, packet->offset);
		}
		else
		{
			GS->timeManager.SetTime(packet->timestampSec, 0, 0);
		}
	}

	//Replace the sender id with our virtual partner id
	if(packetHeader->sender == partnerId){
		packetHeader->sender = virtualPartnerId;
	}

	MeshAccessAuthorization auth = meshAccessMod->CheckAuthorizationForAll(sendData, data, fmKeyId, DataDirection::DIRECTION_IN);

	//Block unauthorized packets
	if(
		auth == MeshAccessAuthorization::UNDETERMINED 
		|| auth == MeshAccessAuthorization::BLACKLIST
	){
		logt("ERROR", "Packet unauthorized");
		return;
	}

	if(
		tunnelType == MeshAccessTunnelType::PEER_TO_PEER
		|| tunnelType == MeshAccessTunnelType::REMOTE_MESH
	){
		TO_HEX(data, sendData->dataLength);
		logt("MACONN", "Received remote mesh data %s (%u) from %u", dataHex, sendData->dataLength, packetHeader->sender);

		//Only dispatch to the local node, virtualPartnerId and remote nodeIds are kept in tact
		if(auth <= MeshAccessAuthorization::LOCAL_ONLY) GS->cm.DispatchMeshMessage(this, sendData, packetHeader, true);
	}
	else if(tunnelType == MeshAccessTunnelType::LOCAL_MESH)
	{
		TO_HEX(data, sendData->dataLength);
		logt("MACONN", "Received data for local mesh %s (%u) from %u aka %u", dataHex, sendData->dataLength, packetHeader->sender, virtualPartnerId);

		//Send to other Mesh-like Connections
		if(auth <= MeshAccessAuthorization::WHITELIST) GS->cm.RouteMeshData(this, sendData, data);

		//Dispatch Message throughout the implementation to all modules
		if(auth <= MeshAccessAuthorization::LOCAL_ONLY) GS->cm.DispatchMeshMessage(this, sendData, packetHeader, true);

	//We must allow handshake packets
	} else if (packetHeader->messageType >= MessageType::ENCRYPT_CUSTOM_START && packetHeader->messageType <= MessageType::ENCRYPT_CUSTOM_DONE)
	{
		if(auth <= MeshAccessAuthorization::LOCAL_ONLY) GS->cm.DispatchMeshMessage(this, sendData, packetHeader, true);
	}

#ifdef SIM_ENABLED
	if (packetHeader->messageType == MessageType::CLUSTER_INFO_UPDATE
		&& sendData->dataLength >= sizeof(connPacketClusterInfoUpdate)
	) {
		connPacketClusterInfoUpdate* data = (connPacketClusterInfoUpdate*)packetHeader;
		logt("MACONN", "Received ClusterInfoUpdate over MACONN with size:%u and hops:%d", data->payload.clusterSizeChange, data->payload.hopsToSink);
	}
#endif
}


#define ________________________HANDLER________________________

//After connection, both sides must do a service and characteristic discovery for the other rx and tx handle
//Then, they must activate notifications on the tx handle
//After the partner has activated notifications on ones own tx handle, it is possible to transmit data
void MeshAccessConnection::ConnectionSuccessfulHandler(u16 connectionHandle)
{
	//Call super method
	BaseConnection::ConnectionSuccessfulHandler(connectionHandle);

	if(direction == ConnectionDirection::DIRECTION_OUT)
	{
		//First, we need to discover the remote service
		GS->gattController.DiscoverService(connectionHandle, meshAccessService->serviceUuid);
	}
}

bool MeshAccessConnection::GapDisconnectionHandler(u8 hciDisconnectReason)
{
	bool result = AppConnection::GapDisconnectionHandler(hciDisconnectReason);

	NotifyConnectionStateSubscriber(ConnectionState::DISCONNECTED);

	return result;
}

void MeshAccessConnection::GATTServiceDiscoveredHandler(ble_db_discovery_evt_t& evt)
{
	logt("MACONN", "Service discovered %x", evt.params.discovered_db.srv_uuid.uuid);


	//Once the remote service was discovered, we must register for notifications
	if(evt.params.discovered_db.srv_uuid.uuid == meshAccessService->serviceUuid.uuid
		&& evt.params.discovered_db.srv_uuid.type == meshAccessService->serviceUuid.type){
		for(u32 j=0; j<evt.params.discovered_db.char_count; j++)
		{
			logt("MACONN", "Found service");
			//Save a reference to the rx handle of our partner
			if(evt.params.discovered_db.charateristics[j].characteristic.uuid.uuid == MA_SERVICE_RX_CHARACTERISTIC_UUID)
			{
				partnerRxCharacteristicHandle = evt.params.discovered_db.charateristics[j].characteristic.handle_value;
				logt("MACONN", "Found rx char %u", partnerRxCharacteristicHandle);
			}
			//Save a reference to the rx handle of our partner and its CCCD Handle which is needed to enable notifications
			if(evt.params.discovered_db.charateristics[j].characteristic.uuid.uuid == MA_SERVICE_TX_CHARACTERISTIC_UUID)
			{
				partnerTxCharacteristicHandle = evt.params.discovered_db.charateristics[j].characteristic.handle_value;
				partnerTxCharacteristicCccdHandle = evt.params.discovered_db.charateristics[j].cccd_handle;
				logt("MACONN", "Found tx char %u with cccd %u", partnerTxCharacteristicHandle, partnerTxCharacteristicCccdHandle);
			}
		}
	}

	if(partnerTxCharacteristicCccdHandle != 0){
		RegisterForNotifications();
	}
}


#define ________________________OTHER________________________

void MeshAccessConnection::PrintStatus()
{
	const char* directionString = (direction == ConnectionDirection::DIRECTION_IN) ? "IN " : "OUT";

	trace("%s MA state:%u, Queue:%u-%u(%u), Buf%u/%u, hnd:%u, partnerId/virtual:%u/%u, tunnel %u" EOL, 
		directionString, 
		(u32)this->connectionState, 
		(packetSendQueue.readPointer - packetSendQueue.bufferStart), 
		(packetSendQueue.writePointer - packetSendQueue.bufferStart), 
		packetSendQueue._numElements, 
		reliableBuffersFree, 
		unreliableBuffersFree, 
		connectionHandle, 
		partnerId, 
		virtualPartnerId, 
		(u32)tunnelType);
}
