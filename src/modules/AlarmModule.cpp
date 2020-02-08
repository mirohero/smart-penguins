#include <AlarmModule.h>

#include <Logger.h>
#include <algorithm>
#include <vector>
#include <map>
#include <Node.h>
#include <Utility.h>
#include <math.h>
#include <algorithm>
#include <GlobalState.h>

#include <stdbool.h>

extern "C"
{
#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "nrf.h"
#include "nrf_drv_gpiote.h"
#include "app_error.h"
}

#define PIN_IN 4
#define PIN_OUT 31

using namespace std;

AlarmModule::AlarmModule() : Module(ModuleId::ALARM_MODULE, "alarm")
{
	//Start module configuration loading
	configurationPointer = &configuration;
	configurationLength = sizeof(AlarmModuleConfiguration);

	alarmJobHandle = NULL;

	//CONFIG
	lastClusterSize = GS->node.clusterSize;
	nearestTrafficJamNodeId = 0;
	nearestBlackIceNodeId = 0;
	nearestRescueLaneNodeId = 0;
	nearestTrafficJamOppositeLaneNodeId = 0;
	nearestBlackIceOppositeLaneNodeId = 0;
	nearestRescueLaneOppositeLaneNodeId = 0;

	trafficJamAtMyNode = false;
	blackIceAtMyNode = false;
	rescueLaneAtMyNode = false;
	rescueTimer = 0;

	trafficJamInterval = 0;
	trafficJamPool1.setAllBytesTo(0);
	trafficJamPool2.setAllBytesTo(0);

	GpioInit();

	//Start Broadcasting the informations
	UpdateGpioState();
	RequestAlarmUpdatePacket();
	BroadcastPenguinAdvertisingPacket();
	logt("NODE", "Started MIRO");

	ResetToDefaultConfiguration();
}

void AlarmModule::ButtonHandler(u8 buttonId, u32 holdTimeDs)
{
	//Send alarm update message
	logt("ALARMMOD", "Button pressed %u. Pressed time: %u", buttonId, holdTimeDs);

	BlinkGreenLed();
	UpdateGpioState();

	// Broadcast a rescue lane alarm
	if(!blackIceAtMyNode) {
		blackIceAtMyNode = true;
		BroadcastAlarmUpdatePacket(GS->node.configuration.nodeId, SERVICE_INCIDENT_TYPE::BLACK_ICE, SERVICE_ACTION_TYPE::SAVE);
		logt("BROADCAST", "BroadcastAlarmUpdatePacket(%u, SERVICE_INCIDENT_TYPE::BLACK_ICE, SERVICE_ACTION_TYPE::SAVE);", GS->node.configuration.nodeId);
	} else {
		blackIceAtMyNode = false;
		BroadcastAlarmUpdatePacket(GS->node.configuration.nodeId, SERVICE_INCIDENT_TYPE::BLACK_ICE, SERVICE_ACTION_TYPE::DELETE);
		logt("BROADCAST", "BroadcastAlarmUpdatePacket(%u, SERVICE_INCIDENT_TYPE::BLACK_ICE, SERVICE_ACTION_TYPE::DELETE);", GS->node.configuration.nodeId);
	}
}

void AlarmModule::BlinkGreenLed()
{
	GS->ledGreen.On();
	nrf_delay_ms(1000);
	GS->ledGreen.Off();
}

void AlarmModule::ConfigurationLoadedHandler()
{
	//Does basic testing on the loaded configuration
#if IS_INACTIVE(GW_SAVE_SPACE)

#endif
	logt("ALARMMOD", "AlarmModule Config Loaded");
}

/*
 *	RequestAlarmUpdatePacket
 *
 *	sends a broadcast message, requesting an update from other nodes
 *
 */
void AlarmModule::RequestAlarmUpdatePacket()
{
	SendModuleActionMessage(MessageType::MODULE_TRIGGER_ACTION,
							0,
							AlarmModuleTriggerActionMessages::GET_ALARM_SYSTEM_UPDATE,
							0,
							NULL,
							0,
							false);
}

void AlarmModule::UpdateGpioState()
{
	nrf_gpio_pin_set(PIN_OUT);
	gpioState = nrf_gpio_pin_read(PIN_IN);
	nrf_gpio_pin_clear(PIN_OUT);
}

/*
 *	BroadcastAlarmUpdatePacket
 *
 *	sends a broadcast alarm message with the specified incident nodeId, type and action
 *
 *	u8 incidentNodeId
 *	SERVICE_INCIDENT_TYPE incidentType
 *	SERVICE_ACTION_TYPE incidentAction

 */

void AlarmModule::BroadcastAlarmUpdatePacket(u8 incidentNodeId, SERVICE_INCIDENT_TYPE incidentType, SERVICE_ACTION_TYPE incidentAction, NodeId targetNodeId)
{
	AlarmModuleUpdateMessage data;
	data.meshDeviceId = incidentNodeId;
	data.meshIncidentType = incidentType;
	data.meshActionType = incidentAction;

	SendModuleActionMessage(MessageType::MODULE_TRIGGER_ACTION,
							targetNodeId,
							(u8)AlarmModuleTriggerActionMessages::SET_ALARM_SYSTEM_UPDATE,
							0,
							(u8 *)&data,
							SIZEOF_ALARM_MODULE_UPDATE_MESSAGE,
							false);
}

/*
 *	BroadcastPenguinAdvertisingPacket
 *
 *	sends a broadcast message with the current node informations
 *
 */
void AlarmModule::BroadcastPenguinAdvertisingPacket()
{
	logt("ALARM_SYSTEM", "Starting Broadcasting Penguin Packet");

	currentAdvChannel = Utility::GetRandomInteger() % 3;

	//build alarm advertisement packet
	AdvJob job = {
		AdvJobTypes::SCHEDULED,			   //JobType
		5,								   //Slots
		0,								   //Delay
		MSEC_TO_UNITS(200, UNIT_0_625_MS), //AdvInterval
		0,								   //AdvChannel
		0,								   //CurrentSlots
		0,								   //CurrentDelay
		GapAdvType::ADV_IND,			   //Advertising Mode
		{0},							   //AdvData
		0,								   //AdvDataLength
		{0},							   //ScanData
		0								   //ScanDataLength
	};

	//Select either the new advertising job or the already existing
	AdvJob *currentJob;
	if (alarmJobHandle == NULL)
	{
		currentJob = &job;
	}
	else
	{
		currentJob = alarmJobHandle;
	}
	u8 *bufferPointer = currentJob->advData;

	advStructureFlags *flags = (advStructureFlags *)bufferPointer;
	flags->len = SIZEOF_ADV_STRUCTURE_FLAGS - 1;
	flags->type = BLE_GAP_AD_TYPE_FLAGS;
	flags->flags = BLE_GAP_ADV_FLAG_LE_GENERAL_DISC_MODE | BLE_GAP_ADV_FLAG_BR_EDR_NOT_SUPPORTED;

	advStructureUUID16 *serviceUuidList = (advStructureUUID16 *)(bufferPointer + SIZEOF_ADV_STRUCTURE_FLAGS);
	serviceUuidList->len = SIZEOF_ADV_STRUCTURE_UUID16 - 1;
	serviceUuidList->type = BLE_GAP_AD_TYPE_16BIT_SERVICE_UUID_COMPLETE;
	serviceUuidList->uuid = SERVICE_DATA_SERVICE_UUID16;

	AdvPacketPenguinData *alarmData = (AdvPacketPenguinData *)(bufferPointer + SIZEOF_ADV_STRUCTURE_FLAGS + SIZEOF_ADV_STRUCTURE_UUID16);
	alarmData->len = SIZEOF_ADV_STRUCTURE_ALARM_SERVICE_DATA - 1;
	alarmData->type = SERVICE_TYPE_ALARM_UPDATE;

	alarmData->uuid = SERVICE_DATA_SERVICE_UUID16;
	alarmData->messageType = SERVICE_DATA_MESSAGE_TYPE_ALARM;
	alarmData->clusterSize = GS->node.clusterSize;
	alarmData->networkId = GS->node.configuration.networkId;

	// Incident data, only send if there actually is an incident (but if there is, need to send all to keep structure)
	if (nearestRescueLaneNodeId != 0 || nearestRescueLaneOppositeLaneNodeId != 0 ||
		nearestTrafficJamNodeId != 0 || nearestTrafficJamOppositeLaneNodeId != 0 ||
		nearestBlackIceNodeId != 0 || nearestBlackIceOppositeLaneNodeId != 0 ||
		rescueLaneAtMyNode || trafficJamAtMyNode || blackIceAtMyNode || !rescueLaneAtMyNode || !trafficJamAtMyNode || !blackIceAtMyNode)
	{
		alarmData->nearestRescueLaneNodeId = rescueLaneAtMyNode ? GS->node.configuration.nodeId : nearestRescueLaneNodeId;
		alarmData->nearestTrafficJamNodeId = trafficJamAtMyNode ? GS->node.configuration.nodeId : nearestTrafficJamNodeId;
		alarmData->nearestBlackIceNodeId = blackIceAtMyNode ? GS->node.configuration.nodeId : nearestBlackIceNodeId;

		alarmData->nearestRescueLaneOppositeLaneNodeId = nearestRescueLaneOppositeLaneNodeId;
		alarmData->nearestTrafficJamOppositeLaneNodeId = nearestTrafficJamOppositeLaneNodeId;
		alarmData->nearestBlackIceOppositeLaneNodeId = nearestBlackIceOppositeLaneNodeId;
	}

	alarmData->direction =GS->node.configuration.direction;

	//logt("ALARM_SYSTEM", "unsecureCount: %u", meshDeviceIdArray.size());

	alarmData->nodeId = GS->node.configuration.nodeId;

	//logt("ALARM_SYSTEM", "txPower: %u", Boardconfig->calibratedTX);

	u32 length = SIZEOF_ADV_STRUCTURE_FLAGS + SIZEOF_ADV_STRUCTURE_UUID16 + SIZEOF_ADV_STRUCTURE_ALARM_SERVICE_DATA;
	job.advDataLength = length;

	// Log AdvPacketPenguinData
	// logt("BROADCAST", "len: %u", alarmData->len);
	// logt("BROADCAST", "type: %u", alarmData->type);
	// logt("BROADCAST", "uuid: %u", alarmData->uuid);
	// logt("BROADCAST", "messageType: %u", alarmData->messageType);
	// logt("BROADCAST", "nodeId: %u", alarmData->nodeId);
	// logt("BROADCAST", "boardType: %u", alarmData->boardType);
	// logt("BROADCAST", "currentClusterSize: %u", alarmData->currentClusterSize);
	// logt("BROADCAST", "clusterSize: %u", alarmData->clusterSize);
	// logt("BROADCAST", "networkId: %u", alarmData->networkId);
	logt("BROADCAST", "nearestRescueLaneNodeId: %u", alarmData->nearestRescueLaneNodeId);
	logt("BROADCAST", "nearestTrafficJamNodeId: %u", alarmData->nearestTrafficJamNodeId);
	logt("BROADCAST", "nearestBlackIceNodeId: %u", alarmData->nearestBlackIceNodeId);
	logt("BROADCAST", "nearestRescueLaneOppositeLaneNodeId: %u", alarmData->nearestRescueLaneOppositeLaneNodeId);
	logt("BROADCAST", "nearestTrafficJamOppositeLaneNodeId: %u", alarmData->nearestTrafficJamOppositeLaneNodeId);
	logt("BROADCAST", "nearestBlackIceOppositeLaneNodeId: %u", alarmData->nearestBlackIceOppositeLaneNodeId);
	// logt("BROADCAST", "direction: %u", alarmData->direction);
	logt("BROADCAST", " ");

	//Either update the job or create it if not done
	if (alarmJobHandle == NULL)
	{
		alarmJobHandle = GS->advertisingController.AddJob(job);
		//logt("ALARM_SYSTEM", "NewAdvJob");
	}
	else
	{
		GS->advertisingController.RefreshJob(alarmJobHandle);
		//logt("ALARM_SYSTEM", "Updated the job");
	}
	char cbuffer[100];

	// logt("ALARMMOD", "Broadcasting asset data %s, len %u", cbuffer, length);
}

void AlarmModule::ResetToDefaultConfiguration()
{
	//Set default configuration values
	configuration.moduleId = moduleId;
	configuration.moduleActive = true;
	configuration.moduleVersion = 1;
	SET_FEATURESET_CONFIGURATION(&configuration, this);
}

void AlarmModule::MeshMessageReceivedHandler(BaseConnection *connection, BaseConnectionSendData *sendData, connPacketHeader *packetHeader)
{
	//Must call superclass for handling
	Module::MeshMessageReceivedHandler(connection, sendData, packetHeader);

	//Check if this request is meant for modules in general
	if (packetHeader->messageType == MessageType::MODULE_TRIGGER_ACTION && packetHeader->sender != GS->node.configuration.nodeId)
	{
		logt("ALARMMOD", "Received Alarm Update Request");
		connPacketModule *packet = (connPacketModule *)packetHeader;
		AlarmModuleUpdateMessage *data = (AlarmModuleUpdateMessage *)packet->data;

		//Check if our module is meant and we should trigger an action
		if (packet->moduleId == moduleId)
		{
			if (packet->actionType == AlarmModuleTriggerActionMessages::GET_ALARM_SYSTEM_UPDATE)
			{
				logt("ALARMMOD", "Received Alarm Update GET Request");
				// if there is an incident at my node, broadcast it out
				if (trafficJamAtMyNode)
				{
					BroadcastAlarmUpdatePacket(nearestTrafficJamNodeId, SERVICE_INCIDENT_TYPE::TRAFFIC_JAM, SERVICE_ACTION_TYPE::SAVE);
					logt("BROADCAST", "BroadcastAlarmUpdatePacket(%u, SERVICE_INCIDENT_TYPE::TRAFFIC_JAM, SERVICE_ACTION_TYPE::SAVE); (MeshMessageReceivedHandler)", GS->node.configuration.nodeId);
				}
				if (blackIceAtMyNode)
				{
					BroadcastAlarmUpdatePacket(nearestBlackIceNodeId, SERVICE_INCIDENT_TYPE::BLACK_ICE, SERVICE_ACTION_TYPE::SAVE);
					logt("BROADCAST", "BroadcastAlarmUpdatePacket(%u, SERVICE_INCIDENT_TYPE::BLACK_ICE, SERVICE_ACTION_TYPE::SAVE); (MeshMessageReceivedHandler)", GS->node.configuration.nodeId);
					
				}
				if (rescueLaneAtMyNode)
				{
					BroadcastAlarmUpdatePacket(nearestRescueLaneNodeId, SERVICE_INCIDENT_TYPE::RESCUE_LANE, SERVICE_ACTION_TYPE::SAVE);
					logt("BROADCAST", "BroadcastAlarmUpdatePacket(%u, SERVICE_INCIDENT_TYPE::RESCUE_LANE, SERVICE_ACTION_TYPE::SAVE); (MeshMessageReceivedHandler)", GS->node.configuration.nodeId);
				}
			}
			if (packet->actionType == AlarmModuleTriggerActionMessages::SET_ALARM_SYSTEM_UPDATE)
			{
				logt("ALARMMOD", "Received Alarm Update SET Request");

				// If incident got updated, broadcast to mobile devices
				if (UpdateSavedIncident(data->meshDeviceId, data->meshIncidentType, data->meshActionType))
				{
					BroadcastPenguinAdvertisingPacket();
				}
			}
		}
	}
}
/* UpdateSavedIncident, updates a saved incident, if it is relevant
 *
 * @param u8 incidentNodeId, the id of the node where an incident happened / vanished
 * @param u8 incidentType, the type of incident, one of SERVICE_INCIDENT_TYPE
 * @param u8 actionType, the action type, one of SERVICE_ACTION_TYPE
 *
 * returns bool changed, true if saved incident got updated, false if not
 */
bool AlarmModule::UpdateSavedIncident(u8 incidentNodeId, u8 incidentType, u8 actionType)
{
	bool changed = false;
	SERVICE_INCIDENT_TYPE incType = (SERVICE_INCIDENT_TYPE)incidentType;
	SERVICE_ACTION_TYPE actType = (SERVICE_ACTION_TYPE)actionType;

	// if (abs(incidentNodeId - GS->node.configuration.nodeId) > ALARM_MODULE_TRAFFIC_JAM_WARNING_RANGE)
	// {
	// 	return changed;
	// }

	// create a generic pointer to the incidentId
	u8 *savedIncidentNodeId = 0;
	if (incType == TRAFFIC_JAM)
	{
		// check if incident id is on same road side
		if ((incidentNodeId - GS->node.configuration.nodeId) % 2 == 0)
		{
			savedIncidentNodeId = &nearestTrafficJamNodeId;
		}
		else
		{
			savedIncidentNodeId = &nearestTrafficJamOppositeLaneNodeId;
		}
	}
	else if (incType == BLACK_ICE)
	{
		if ((incidentNodeId - GS->node.configuration.nodeId) % 2 == 0)
		{
			savedIncidentNodeId = &nearestBlackIceNodeId;
		}
		else
		{
			savedIncidentNodeId = &nearestBlackIceOppositeLaneNodeId;
		}
	}
	else if (incType == RESCUE_LANE)
	{
		if ((incidentNodeId - GS->node.configuration.nodeId) % 2 == 0)
		{
			savedIncidentNodeId = &nearestRescueLaneNodeId;
		}
		else
		{
			savedIncidentNodeId = &nearestRescueLaneOppositeLaneNodeId;
		}
	}

	if (*savedIncidentNodeId == incidentNodeId && actType == DELETE)
	{
		*savedIncidentNodeId = 0;
		changed = true;
		// lane with uneven numbers -> driving direction is 1 -> 3 -> 5 | lane with even numbers, driving direction is 6 -> 4 -> 2 ...
	}
	else if (*savedIncidentNodeId != incidentNodeId && actType == SAVE)
	{
		// incident happened on lane with uneven numbers and is traffic jam or black ice, or happened on even side and is rescue lane
		// results in the same logic, because of the driving directions (one up and one down)
		// and the fact that rescue lane is relevant behind us, while the other are relevant ahead of us
		if ((incidentNodeId % 2 != 0 && (incType == TRAFFIC_JAM || incType == BLACK_ICE)) || (incidentNodeId % 2 == 0 && incType == RESCUE_LANE))
		{
			// ... only new incident with an id smaller than the current saved incident id, but not smaller than our id - 1
			// ( +1 so beacon on same position on other lane is included) are relevant
			// if savedIncidentNodeId is 0, there is no saved incident yet, which means we only have to check if it is ahead of us
			if ((incidentNodeId < *savedIncidentNodeId || *savedIncidentNodeId == 0) && incidentNodeId >= GS->node.configuration.nodeId - 1)
			{
				*savedIncidentNodeId = incidentNodeId;
				changed = true;
			}
		}
		else
		{
			// ... only new incident with an id bigger than the current saved incident id, but not bigger than our id + 1
			// ( +1 so beacon on same position on other lane is included) are relevant
			//  *savedIncidentNodeId == 0 check is not needed here because > comparison always overwrites the 0 case (no saved incident yet)
			if (incidentNodeId > *savedIncidentNodeId && incidentNodeId <= GS->node.configuration.nodeId + 1)
			{
				*savedIncidentNodeId = incidentNodeId;
				changed = true;
			}
		}
	}

	return changed;
}

u8 AlarmModule::intersection(SimpleArray<u16, TRAFFIC_JAM_POOL_SIZE> a, SimpleArray<u16, TRAFFIC_JAM_POOL_SIZE> b)
{
	u8 m = a.size();
	u8 n = b.size();
	u8 count = 0;
	SimpleArray<u16, TRAFFIC_JAM_POOL_SIZE> ab;
	SimpleArray<u16, TRAFFIC_JAM_POOL_SIZE> abc;
	ab.setAllBytesTo(0);
	abc.setAllBytesTo(0);

	for (int i = 0; i < m; i++)
	{
		for (int j = 0; j < n; j++)
		{
			if (a[i] == b[j])
			{
				ab[count] = a[i];
				count++;
				return 1;
			}
		}
	}
	return (u8)ab.size();
}

void AlarmModule::GpioInit()
{
	nrf_gpio_pin_dir_set(PIN_OUT, NRF_GPIO_PIN_DIR_OUTPUT);
	nrf_gpio_cfg_output(PIN_OUT);
	nrf_gpio_pin_set(PIN_OUT);
	nrf_gpio_cfg_input(PIN_IN, NRF_GPIO_PIN_NOPULL);
}

bool AlarmModule::isMyDirection(u8 direction)
{
	if (!GS->node.configuration.checkDirection)
	{
		return true;
	}
	if (abs(direction - GS->node.configuration.direction) <= 3 || abs(direction - GS->node.configuration.direction) >= 9)
	{
		return true;
	}
	return false;
}

void AlarmModule::GapAdvertisementReportEventHandler(const GapAdvertisementReportEvent &advertisementReportEvent)
{
	if (!configuration.moduleActive)
		return;

	const advPacketCarServiceAndDataHeader *packetHeader = (const advPacketCarServiceAndDataHeader *)advertisementReportEvent.getData();

	if (packetHeader->mway_service_uuid == 0xFE12 && packetHeader->mway_service_uuid2 == 0xFE12)
	{
		const AdvPacketCarData *packetData = (const AdvPacketCarData *)&packetHeader->data;

		// // Logging hex values of packetHeader
		// const advPacketCarServiceAndDataHeader header = *packetHeader;
		// unsigned char *rawDataPtr1 = (unsigned char *)&header;
		// u16 size1 = sizeof(header);
		// logt("ALARMMOD", "raw data (advPacketCarServiceAndDataHeader):\n");
		// while (size1--)
		// {
		// 	logt("ALARMMOD", "0x%02X", *rawDataPtr1++);
		// }
		// // Logging hex values of packetData
		// const AdvPacketCarData data = *packetData;
		// unsigned char *rawDataPtr2 = (unsigned char *)&data;
		// u16 size2 = sizeof(data);
		// logt("ALARMMOD", "raw data (AdvPacketCarData):\n");
		// while (size2--)
		// {
		// 	logt("ALARMMOD", "0x%02X", *rawDataPtr2++);
		// }
		// // Logging values of packetHeader
		// logt("ALARMMOD", "advPacketCarServiceAndDataHeader:\n");
		// logt("ALARMMOD", "flags: 0x%X,\nmway_service_uuid: 0x%X,\nflags2: 0x%X\nmway_service_uuid2: 0x%X\n",
		// 	 packetHeader->flags,
		// 	 packetHeader->mway_service_uuid,
		// 	 packetHeader->flags2,
		// 	 packetHeader->mway_service_uuid2);
		// // Logging values of packetData
		// logt("ALARMMOD", "advPacketAssetServiceData:\nlen: 0x%X,\ntype: 0x%X,\nmessageType: 0x%X,\ndeviceID: 0x%X,\ndeviceType: 0x%X,\ndirection: 0x%X,\nisEmergency: 0x%X,\nisSlippery: 0x%X,\nisJam: 0x%X",
		// 	 packetData->len,
		// 	 packetData->type,
		// 	 packetData->messageType,
		// 	 packetData->deviceID,
		// 	 packetData->deviceType,
		// 	 packetData->direction,
		// 	 packetData->isEmergency,
		// 	 packetData->isSlippery,
		// 	 packetData->isJam);

		if (packetData->deviceType == DeviceType::EMERGENCY && !rescueLaneAtMyNode) {
			if (isMyDirection(packetData->direction)) {
				rescueLaneAtMyNode = true;
				rescueTimer = 10;	
				BroadcastAlarmUpdatePacket(GS->node.configuration.nodeId, SERVICE_INCIDENT_TYPE::RESCUE_LANE, SERVICE_ACTION_TYPE::SAVE);
				logt("BROADCAST", "BroadcastAlarmUpdatePacket(%u, SERVICE_INCIDENT_TYPE::RESCUE_LANE, SERVICE_ACTION_TYPE::SAVE);", GS->node.configuration.nodeId);
			} else {
				if (GS->node.configuration.nodeId % 2 != 0 && nearestRescueLaneOppositeLaneNodeId != GS->node.configuration.nodeId + 1) {
					nearestRescueLaneOppositeLaneNodeId = GS->node.configuration.nodeId + 1;
					BroadcastAlarmUpdatePacket(GS->node.configuration.nodeId + 1, SERVICE_INCIDENT_TYPE::RESCUE_LANE, SERVICE_ACTION_TYPE::SAVE);
					logt("BROADCAST", "BroadcastAlarmUpdatePacket(%u, SERVICE_INCIDENT_TYPE::RESCUE_LANE, SERVICE_ACTION_TYPE::SAVE);", GS->node.configuration.nodeId);
				} else if (GS->node.configuration.nodeId % 2 == 0 && nearestRescueLaneOppositeLaneNodeId != GS->node.configuration.nodeId - 1) {
					nearestRescueLaneOppositeLaneNodeId = GS->node.configuration.nodeId - 1;
					BroadcastAlarmUpdatePacket(GS->node.configuration.nodeId - 1, SERVICE_INCIDENT_TYPE::RESCUE_LANE, SERVICE_ACTION_TYPE::SAVE);
					logt("BROADCAST", "BroadcastAlarmUpdatePacket(%u, SERVICE_INCIDENT_TYPE::RESCUE_LANE, SERVICE_ACTION_TYPE::SAVE);", GS->node.configuration.nodeId);
				}
			}
		}

		if (packetData->deviceType == DeviceType::VEHICLE && isMyDirection(packetData->direction))
		{
			if (trafficJamInterval == 0)
			{
				if (trafficJamPool1.size() >= TRAFFIC_JAM_POOL_SIZE)
					trafficJamPool1.pop_front();

				if (!trafficJamPool1.has(packetData->deviceID))
				{
					trafficJamPool1[trafficJamPool1.size()] = packetData->deviceID;
				}
			}
			if (trafficJamInterval == 1)
			{
				if (trafficJamPool2.size() >= TRAFFIC_JAM_POOL_SIZE)
					trafficJamPool2.pop_front();

				if (!trafficJamPool2.has(packetData->deviceID))
				{
					trafficJamPool2[trafficJamPool2.size()] = packetData->deviceID;
				}
			}
		}
	}
}

void AlarmModule::TimerEventHandler(u16 passedTimeDs)
{
	if (!configuration.moduleActive)
		return;

	if (SHOULD_IV_TRIGGER(GS->appTimerDs + GS->appTimerRandomOffsetDs, passedTimeDs, ALARM_MODULE_BROADCAST_TRIGGER_TIME_DS))
	{
		BroadcastPenguinAdvertisingPacket();
	}

	if (SHOULD_IV_TRIGGER(GS->appTimerDs + GS->appTimerRandomOffsetDs, passedTimeDs, RESCUE_CAR_TIMER_INTERVAL))
	{
		if(rescueTimer == 0 && rescueLaneAtMyNode) {
			rescueLaneAtMyNode = false;
			BroadcastAlarmUpdatePacket(GS->node.configuration.nodeId, SERVICE_INCIDENT_TYPE::RESCUE_LANE, SERVICE_ACTION_TYPE::DELETE);
			logt("BROADCAST", "BroadcastAlarmUpdatePacket(%u, SERVICE_INCIDENT_TYPE::RESCUE_LANE, SERVICE_ACTION_TYPE::DELETE);", GS->node.configuration.nodeId);
		} else if (rescueTimer > 0){
			rescueTimer--;
			logt("BROADCAST", "rescueTimer: %u", rescueTimer);
		}
	}

	// Traffic jam timer
	if (SHOULD_IV_TRIGGER(GS->appTimerDs + GS->appTimerRandomOffsetDs, passedTimeDs, ALARM_MODULE_TRAFFIC_JAM_DETECTION_TIME_DS))
	{
		// Log trafficJamPools
		// for (int i = 0; i < TRAFFIC_JAM_POOL_SIZE; i++)
		// {
		// 	logt("ALARMMOD", "trafficJamPool1[%d] = 0x%X", i, trafficJamPool1[i]);
		// }
		// logt("ALARMMOD", " ");
		// for (int i = 0; i < TRAFFIC_JAM_POOL_SIZE; i++)
		// {
		// 	logt("ALARMMOD", "trafficJamPool2[%d] = 0x%X", i, trafficJamPool2[i]);
		// }
		// logt("ALARMMOD", " ");

		u8 intersections = intersection(trafficJamPool1, trafficJamPool2);

		if (!trafficJamAtMyNode && intersections == TRAFFIC_JAM_DETECTED)
		{
			trafficJamAtMyNode = true;
			BroadcastAlarmUpdatePacket(GS->node.configuration.nodeId, SERVICE_INCIDENT_TYPE::TRAFFIC_JAM, SERVICE_ACTION_TYPE::SAVE);
			logt("BROADCAST", "BroadcastAlarmUpdatePacket(%u, SERVICE_INCIDENT_TYPE::TRAFFIC_JAM, SERVICE_ACTION_TYPE::SAVE);", GS->node.configuration.nodeId);
		}
		else if (trafficJamAtMyNode && intersections != TRAFFIC_JAM_DETECTED)
		{
			trafficJamAtMyNode = false;
			BroadcastAlarmUpdatePacket(GS->node.configuration.nodeId, SERVICE_INCIDENT_TYPE::TRAFFIC_JAM, SERVICE_ACTION_TYPE::DELETE);
			logt("BROADCAST", "BroadcastAlarmUpdatePacket(%u, SERVICE_INCIDENT_TYPE::TRAFFIC_JAM, SERVICE_ACTION_TYPE::DELETE);", GS->node.configuration.nodeId);
		}

		if (trafficJamInterval == 0)
			trafficJamPool2.setAllBytesTo(0);
			
		if (trafficJamInterval == 1)
			trafficJamPool1.setAllBytesTo(0);

		trafficJamInterval++;
		if (trafficJamInterval > 1)
			trafficJamInterval = 0;
	}
}
