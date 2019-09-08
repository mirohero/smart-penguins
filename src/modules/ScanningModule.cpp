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


#include <ScanningModule.h>


#include <Logger.h>
#include <ScanController.h>
#include <Utility.h>
#include <Node.h>
#include <stdlib.h>
#include <GlobalState.h>
constexpr u8 SCAN_MODULE_CONFIG_VERSION = 2;

#if IS_ACTIVE(ASSET_MODULE)
#include <AssetModule.h>
#endif


//This module scans for specific messages and reports them back
//This implementation is currently very basic and should just illustrate how
//such functionality could be implemented

ScanningModule::ScanningModule() :
		Module(ModuleId::SCANNING_MODULE, "scan")
{
	//Save configuration to base class variables
	//sizeof configuration must be a multiple of 4 bytes
	configurationPointer = &configuration;
	configurationLength = sizeof(ScanningModuleConfiguration);

	//Initialize scanFilters as empty
	for (int i = 0; i < SCAN_FILTER_NUMBER; i++)
	{
		scanFilters[i].active = 0;
	}

	//resetAssetTrackingTable();

	//Set defaults
	ResetToDefaultConfiguration();
}

void ScanningModule::ResetToDefaultConfiguration()
{
	//Set default configuration values
	configuration.moduleId = moduleId;
	configuration.moduleActive = true;
	configuration.moduleVersion = SCAN_MODULE_CONFIG_VERSION;

	//TODO: This is for testing only
	scanFilterEntry filter;

	filter.grouping = GroupingType::GROUP_BY_ADDRESS;
	filter.address.addr_type = 0xFF;
	filter.advertisingType = 0xFF;
	filter.minRSSI = -100;
	filter.maxRSSI = 100;

	SET_FEATURESET_CONFIGURATION(&configuration, this);
}

void ScanningModule::ConfigurationLoadedHandler(ModuleConfiguration* migratableConfig, u16 migratableConfigLength)
{
	//Do additional initialization upon loading the config

	// Reset address pointer to the beginning of the address table
//	resetAddressTable();
//	resetTotalRSSIsPerAddress();
//	resetTotalMessagesPerAdress();

	totalMessages = 0;
	totalRSSI = 0;

	assetPackets.zeroData();

#if IS_INACTIVE(GW_SAVE_SPACE)
	if (configuration.moduleActive && assetReportingIntervalDs != 0) {
		ScanJob scanJob = ScanJob();
		scanJob.type = ScanState::HIGH;
		scanJob.state = ScanJobState::ACTIVE;
		GS->scanController.RemoveJob(p_scanJob);
		p_scanJob = GS->scanController.AddJob(scanJob);
	}
#endif
	//Start the Module...
}

#ifdef TERMINAL_ENABLED
bool ScanningModule::TerminalCommandHandler(char* commandArgs[], u8 commandArgsSize)
{
	//Must be called to allow the module to get and set the config
	return Module::TerminalCommandHandler(commandArgs, commandArgsSize);
}
#endif

void ScanningModule::TimerEventHandler(u16 passedTimeDs)
{
	if(SHOULD_IV_TRIGGER(GS->appTimerDs, passedTimeDs, groupedReportingIntervalDs))
	{

		//Send grouped packets
//		SendReport();
//
//		resetAddressTable();
//		resetTotalRSSIsPerAddress();
//		resetTotalMessagesPerAdress();

		totalMessages = 0;
		totalRSSI = 0;
	}

	if(SHOULD_IV_TRIGGER(GS->appTimerDs, passedTimeDs, assetReportingIntervalDs)){
		//Send asset tracking packets
		SendTrackedAssets();
//		resetAssetTrackingTable();
	}
}

void ScanningModule::MeshMessageReceivedHandler(BaseConnection* connection, BaseConnectionSendData* sendData, connPacketHeader* packetHeader)
{
	//Must call superclass for handling
	Module::MeshMessageReceivedHandler(connection, sendData, packetHeader);

	if(packetHeader->messageType == MessageType::ASSET_V2)
	{
		ScanModuleTrackedAssetsV2Message* packet = (ScanModuleTrackedAssetsV2Message*) packetHeader;

		ReceiveTrackedAssets(sendData, packet);
	}
}


void ScanningModule::GapAdvertisementReportEventHandler(const GapAdvertisementReportEvent& advertisementReportEvent)
{
	if (!configuration.moduleActive) return;

#if IS_INACTIVE(GW_SAVE_SPACE)
	HandleAssetV2Packets(advertisementReportEvent);
#endif
}

#define _______________________ASSET_V2______________________

#if IS_INACTIVE(GW_SAVE_SPACE)
//This function checks whether we received an assetV2 packet
void ScanningModule::HandleAssetV2Packets(const GapAdvertisementReportEvent& advertisementReportEvent)
{
	const advPacketServiceAndDataHeader* packet = (const advPacketServiceAndDataHeader*)advertisementReportEvent.getData();
	const advPacketAssetServiceData* assetPacket = (const advPacketAssetServiceData*)&packet->data;

	//Check if the advertising packet is an asset packet
	if (
			advertisementReportEvent.getDataLength() >= SIZEOF_ADV_STRUCTURE_ASSET_SERVICE_DATA
			&& packet->flags.len == SIZEOF_ADV_STRUCTURE_FLAGS-1
			&& packet->uuid.len == SIZEOF_ADV_STRUCTURE_UUID16-1
			&& packet->data.type == BLE_GAP_AD_TYPE_SERVICE_DATA
			&& packet->data.uuid == SERVICE_DATA_SERVICE_UUID16
			&& packet->data.messageType == SERVICE_DATA_MESSAGE_TYPE_ASSET
	){
		char serial[6];
		Utility::GenerateBeaconSerialForIndex(assetPacket->serialNumberIndex, serial);
		logt("SCANMOD", "RX ASSETV2 ADV: serial %s, pressure %u, speed %u, temp %u, humid %u, cn %u, rssi %d",
				serial,
				assetPacket->pressure,
				assetPacket->speed,
				assetPacket->temperature,
				assetPacket->humidity,
				assetPacket->advertisingChannel,
				advertisementReportEvent.getRssi());


		//Adds the asset packet to our buffer
		addTrackedAsset(assetPacket, advertisementReportEvent.getRssi());

	}
}

/**
 * Finds a free slot in our buffer of asset packets and adds the packet
 */
bool ScanningModule::addTrackedAsset(const advPacketAssetServiceData* packet, i8 rssi){
	if(packet->serialNumberIndex == 0) return false;

	rssi = -rssi; //Make rssi positive

	if(rssi < 10 || rssi > 90) return false; //filter out wrong rssis

	scannedAssetTrackingPacket* slot = nullptr;

	//Look for an old entry of this asset or a free space
	//Because we fill this buffer from the beginning, we can use the first slot that is empty
	for(int i = 0; i<ASSET_PACKET_BUFFER_SIZE; i++){
		if(assetPackets[i].serialNumberIndex == packet->serialNumberIndex || assetPackets[i].serialNumberIndex == 0){
			slot = &assetPackets[i];
			break;
		}
	}

	//If a slot was found, add the packet
	if(slot != nullptr){
		u16 slotNum = ((u32)slot - (u32)assetPackets.getRaw()) / sizeof(scannedAssetTrackingPacket);
		logt("SCANMOD", "Tracked packet %u in slot %d", packet->serialNumberIndex, slotNum);

		//Clean up first, if we overwrite another assetId
		if(slot->serialNumberIndex != packet->serialNumberIndex){
			slot->serialNumberIndex = packet->serialNumberIndex;
			slot->count = 0;
			slot->rssi37 = slot->rssi38 = slot->rssi39 = UINT8_MAX;
		}
		//If the count is at its max, we reset the rssi
		if(slot->count == UINT8_MAX){
			slot->count = 0;
			slot->rssi37 = slot->rssi38 = slot->rssi39 = UINT8_MAX;
		}

		slot->serialNumberIndex = packet->serialNumberIndex;
		slot->count++;
		//Channel 0 means that we have no channel data, add it to all rssi channels
		if(packet->advertisingChannel == 0 && rssi < slot->rssi37){
			slot->rssi37 = (u16) rssi;
			slot->rssi38 = (u16) rssi;
			slot->rssi39 = (u16) rssi;
		}
		if(packet->advertisingChannel == 1 && rssi < slot->rssi37) slot->rssi37 = (u16) rssi;
		if(packet->advertisingChannel == 2 && rssi < slot->rssi38) slot->rssi38 = (u16) rssi;
		if(packet->advertisingChannel == 3 && rssi < slot->rssi39) slot->rssi39 = (u16) rssi;
		slot->direction = packet->direction;
		slot->pressure = packet->pressure;
		slot->speed = packet->speed;

		return true;
	}
	return false;
}
#endif

/**
 * Sends out all tracked assets from our buffer and resets the buffer
 */

//FIXME: rssi threshold must be used somewhere, apply when receiving packet?
//FIXME: do we average packets or do we just take the best rssi

void ScanningModule::SendTrackedAssets()
{
#if IS_INACTIVE(GW_SAVE_SPACE)
	//Find out how many assets were tracked
	u8 count = 0;
	for(int i=0; i<ASSET_PACKET_BUFFER_SIZE; i++){
		if(assetPackets[i].serialNumberIndex == 0) break;
		count++;
	}

	if(count == 0) return;

	//jstodo add test for tracked assets
	u16 messageLength = SIZEOF_CONN_PACKET_HEADER + SIZEOF_SCAN_MODULE_TRACKED_ASSET_V2 * count;

	//Allocate a buffer big enough and fill the packet
	DYNAMIC_ARRAY(buffer, messageLength);
	ScanModuleTrackedAssetsV2Message* message = (ScanModuleTrackedAssetsV2Message*) buffer;

	message->header.messageType = MessageType::ASSET_V2;
	message->header.sender = GS->node.configuration.nodeId;
	message->header.receiver = NODE_ID_SHORTEST_SINK;

	for(int i=0; i<count; i++){
		message->trackedAssets[i].assetId = assetPackets[i].serialNumberIndex;
		message->trackedAssets[i].rssi37 = assetPackets[i].rssi37;
		message->trackedAssets[i].rssi38 = assetPackets[i].rssi38;
		message->trackedAssets[i].rssi39 = assetPackets[i].rssi39;

		if(assetPackets[i].speed == 0xFF) message->trackedAssets[i].speed = 0xF;
		else if(assetPackets[i].speed > 140) message->trackedAssets[i].speed = 14;
		else if(assetPackets[i].speed == 1) message->trackedAssets[i].speed = 1;
		else message->trackedAssets[i].speed = assetPackets[i].speed / 10;

		message->trackedAssets[i].direction = assetPackets[i].direction / 16; //TODO: convert meaningful

		if(assetPackets[i].pressure == 0xFFFF) message->trackedAssets[i].pressure = 0xFF;
		else message->trackedAssets[i].pressure = (u8)(assetPackets[i].pressure % 250); //Will wrap, which is ok (we still have a relative pressure, but not the absolute one, mod 250 to reserve 0xFF for not available)
	}

	//Send the packet as a non-module message to save some bytes in the header
	GS->cm.SendMeshMessage(
			buffer,
			(u8)messageLength,
			DeliveryPriority::LOW
			);

	//Clear the buffer
	assetPackets.zeroData();
#endif
}

void ScanningModule::ReceiveTrackedAssets(BaseConnectionSendData* sendData, ScanModuleTrackedAssetsV2Message* packet) const
{
	u8 count = (sendData->dataLength - SIZEOF_CONN_PACKET_HEADER)  / SIZEOF_SCAN_MODULE_TRACKED_ASSET_V2;

	logjson("SCANMOD", "{\"nodeId\":%d,\"type\":\"tracked_assets\",\"assets\":[", packet->header.sender);

	for(int i=0; i<count; i++){
		trackedAssetV2* assetData = packet->trackedAssets + i;

		i8 speed = assetData->speed == 0xF ? -1 : assetData->speed;
		i8 direction = assetData->direction == 0xF ? -1 : assetData->direction;
		i16 pressure = assetData->pressure == 0xFF ? -1 : assetData->pressure; //(taken %250 to exclude 0xFF)

		if(i != 0) logjson("SCANMOD", ",");
		logjson("SCANMOD", "{\"id\":%u,\"rssi1\":%d,\"rssi2\":%d,\"rssi3\":%d,\"speed\":%d,\"direction\":%d,\"pressure\":%d}",
				assetData->assetId,
				assetData->rssi37,
				assetData->rssi38,
				assetData->rssi39,
				speed,
				direction,
				pressure);

		//logt("SCANMOD", "MESH RX id: %u, rssi %u, speed %u", assetData->assetId, assetData->rssi, assetData->speed);
	}

	logjson("SCANMOD", "]}" SEP);
}
