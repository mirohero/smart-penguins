/**

 Copyright (c) 2014-2017 "M-Way Solutions GmbH"
 FruityMesh - Bluetooth Low Energy mesh protocol [http://mwaysolutions.com/]

 This file is part of FruityMesh

 FruityMesh is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.

 */

using namespace std;

#pragma once

#include <Module.h>


#include <AdvertisingController.h>
#include <Boardconfig.h>
#include "vector"
#include <stdbool.h>

#define SIZEOF_ALARM_MODULE_UPDATE_MESSAGE 5

#define SERVICE_DATA_MESSAGE_TYPE_ALARM 25
#define SERVICE_TYPE_ALARM_UPDATE 33
#define ALARM_MODULE_BROADCAST_TRIGGER_TIME_DS 3
#define ALARM_MODULE_TRAFFIC_JAM_DETECTION_TIME_DS 30
#define ASSET_PACKET_BUFFER_SIZE 30
#define ALARM_MODULE_TRAFFIC_JAM_WARNING_RANGE 50
#define TRAFFIC_JAM_POOL_SIZE 10
#define TRAFFIC_JAM_DETECTED 1
#define RESCUE_CAR_TIMER_INTERVAL 10

//Service Data (max. 24 byte)
#define SIZEOF_ADV_STRUCTURE_ALARM_SERVICE_DATA 19 //ToDo
// Message from Mesh to Car
typedef struct {
	//6 byte header
    u8 len;  
    u8 type; 
    u16 uuid;
    u16 messageType;
 
    //3 byte additional beacon information
    u8 nodeId;
    u8 boardType; //Nur für Debugging
 
    //3 byte cluster information
    u8 currentClusterSize;
    u8 clusterSize;
    u8 networkId; //Nur für Debugging
 
    //7 Byte Penguin Information
	u8 nearestTrafficJamNodeId;
	u8 nearestBlackIceNodeId;
	u8 nearestRescueLaneNodeId;
	u8 nearestTrafficJamOppositeLaneNodeId;
	u8 nearestBlackIceOppositeLaneNodeId;
	u8 nearestRescueLaneOppositeLaneNodeId;
	u8 direction;
}AdvPacketPenguinData;

// Message from Mesh to Mesh
typedef struct {
	u8 meshDeviceId; // node id
	u8 meshIncidentType; // type of incident, e.g traffic jam, one of SERVICE_INCIDENT_TYPE
	u8 meshActionType; // incident type action, e.g SAVE or DELETE, one of SERVICE_ACTION_TYPE
}AlarmModuleUpdateMessage;

#define SIZE_ADV_PACKET_CAR_DATA 11
// Message from Car to Mesh
typedef struct {
    //6 byte header
    u8 len;  
    u8 type; 
    u16 messageType;
 
    //5 byte car information (Können/sollten auch nur als Bits gesetzt werden)
    u8 deviceType; // Car, bicycle, pedestrian
	u8 direction; // 1 = North / 4 = East / 2-3 = NorthEast etc.
	u8 isEmergency;
    u8 isSlippery;
	u8 isJam;
	u16 deviceID;
}AdvPacketCarData;

#define SIZE_ADV_PACKET_CAR_SERVICE_AND_DATA_HEADER 9
typedef struct
{
	// 9 byte header
	u16 flags;
	u16 mway_service_uuid;
	u16 flags2;
	u16 mway_service_uuid2;
	u8 data[SIZE_ADV_PACKET_CAR_DATA];
}advPacketCarServiceAndDataHeader;

class AlarmModule: public Module {
private:
#pragma pack(push, 1)
	//Module configuration that is saved persistently (size must be multiple of 4)

	struct AlarmModuleConfiguration: ModuleConfiguration
	{
		//Insert more persistent config values here
	};

	enum AlarmModuleTriggerActionMessages {
		MA_CONNECT = 0,
		MA_DISCONNECT = 1,
		SET_ALARM_SYSTEM_UPDATE = 2,
		GET_ALARM_SYSTEM_UPDATE = 3
	};

	enum TrafficJamTriggerActionMessages {
		TRIGGER_CHECK_LEFT_NODE = 0,
		TRIGGER_CHECK_RIGHT_NODE = 1,
		TRIGGER_TRAFFIC_JAM_WARNING_NODE = 2,
		TRIGGER_CHECK_LEFT_NODE_AT_BACK = 3,
		TRIGGER_CHECK_RIGHT_NODE_AT_BACK = 4
	};

	enum AlarmModuleActionResponseMessages {
		ALARM_SYSTEM_UPDATE = 1
	};

	enum TrafficJamActionResponseMessages {
		RESPONSE_FROM_LEFT_NODE = 0,
		RESPONSE_FROM_RIGHT_NODE = 1,
		RESPONSE_FROM_TRAFFIC_JAM_WARNING_NODE = 2,
		RESPONSE_FROM_LEFT_NODE_AT_BACK = 2,
		RESPONSE_FROM_RIGHT_NODE_AT_BACK = 3
	};

	enum BoardType {
		DEV_BOARD = 1,
		RUUVI_TAG = 3
	};

	//Storage for advertising packets
	typedef struct
	{
		u32 serialNumberIndex;
		u8 rssi37;
		u8 rssi38;
		u8 rssi39;
		u8 count;
		u8 speed;
		u8 direction;
		u16 pressure;
	} scannedAssetTrackingPacket;

	SimpleArray<scannedAssetTrackingPacket, ASSET_PACKET_BUFFER_SIZE> assetPackets;

#define SIZEOF_MA_MODULE_DISCONNECT_MESSAGE 7
	typedef struct
	{
		fh_ble_gap_addr_t targetAddress;

	} MeshAccessModuleDisconnectMessage;

	enum SERVICE_INCIDENT_TYPE {
		RESCUE_LANE = 0,
		BLACK_ICE = 1,
		TRAFFIC_JAM = 2,
		// TODO: Implement BREAK_DOWN use case if neccessary
		BREAK_DOWN = 3
	};
	enum SERVICE_ACTION_TYPE {
		DELETE = 0,
		SAVE = 1,
	};
	enum DeviceType {
		VEHICLE = 1,
		BICYCLE = 2,
		EMERGENCY = 3,
		PEDESTRIAN = 4
	};

	u8 nearestTrafficJamNodeId;
	u8 nearestBlackIceNodeId;
	u8 nearestRescueLaneNodeId;
	u8 nearestTrafficJamOppositeLaneNodeId;
	u8 nearestBlackIceOppositeLaneNodeId;
	u8 nearestRescueLaneOppositeLaneNodeId;

	bool trafficJamAtMyNode;
	bool blackIceAtMyNode;
	bool rescueLaneAtMyNode;

	u8 rescueTimer;

	AlarmModuleConfiguration configuration;
	AdvJob* alarmJobHandle;
	u8 currentAdvChannel;
	u8 index;

	u8 lastClusterSize;
	u8 gpioState;

	u8 trafficJamInterval;
	SimpleArray<u16, TRAFFIC_JAM_POOL_SIZE> trafficJamPool1;
	SimpleArray<u16, TRAFFIC_JAM_POOL_SIZE> trafficJamPool2;

#pragma pack(pop)

public:
	AlarmModule();
	void MeshMessageReceivedHandler(BaseConnection* connection, BaseConnectionSendData* sendData, connPacketHeader* packetHeader);

	void ButtonHandler(u8 buttonId, u32 holdTimeDs);

	void ConfigurationLoadedHandler();

	void ResetToDefaultConfiguration();

	void BroadcastPenguinAdvertisingPacket();

	void BroadcastAlarmUpdatePacket(u8 incidentNodeId, SERVICE_INCIDENT_TYPE incidentType, SERVICE_ACTION_TYPE incidentAction, NodeId targetNodeId = 0);

	void RequestAlarmUpdatePacket();

	bool UpdateSavedIncident(u8 incidentNodeId, u8 incidentType, u8 actionType);

	void TimerEventHandler(u16 passedTimeDs) override;

	void ReceivedMeshAccessDisconnectMessage(connPacketModule* packet, u16 packetLength);

	void GpioInit();

	void BlinkRedLed();

	void BlinkGreenLed();

	void UpdateGpioState();
	
	u8 intersection(SimpleArray<u16, TRAFFIC_JAM_POOL_SIZE> a, SimpleArray<u16, TRAFFIC_JAM_POOL_SIZE> b);

	virtual void GapAdvertisementReportEventHandler(const GapAdvertisementReportEvent& advertisementReportEvent) override;

	bool isMyDirection(u8 direction);
};


