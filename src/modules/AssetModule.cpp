
#include <AssetModule.h>

#include <Logger.h>
#include <StatusReporterModule.h>
#include <MeshAccessModule.h>
#include <Utility.h>
#include <Node.h>
#include <GlobalState.h>

extern "C" {

#include "nrf_drv_gpiote.h"
#include "nrf_delay.h"
#include "math.h"
}

#define ASSET_MODULE_BAROMETER_SLEEP_DS 50
#define ASSET_MODULE_ENCRYPT_ADV_DATA false
#define ASSET_MODULE_SLEEP_ADV_UPDATE_TIME_DS 50

AssetModule::AssetModule() :
		Module(ModuleId::ASSET_MODULE, "asset") {
	//Register callbacks n' stuff
	//Save configuration to base class variables
	//sizeof configuration must be a multiple of 4 bytes

	configurationPointer = &configuration;
	configurationLength = sizeof(AssetModuleConfiguration);

	assetJobHandle = NULL;

	lastPressureReading = 0;
	lastTemperatureReading = 0;
	lastHumidityReading = 0;
	lastBarometerReadTimeDs = 0;

	lastMovementTimeDs = 0;

	moving = true;
	currentAdvChannel = 0;

	//Set defaults
	ResetToDefaultConfiguration();
}

void AssetModule::ResetToDefaultConfiguration() {
	//Set default configuration values
	configuration.moduleId = moduleId;
	configuration.moduleActive = false;
	configuration.moduleVersion = 1;

	//Set additional config values...
	configuration.wakeupThreshold = 0.1;
	configuration.wakeupDuration = 500;
	configuration.movementEndThresholdMilliG = 15;
	configuration.movementEndDelayDs = 450;
	configuration.enableAccelerometer = true;
	configuration.enableBarometer = true;
	configuration.advIntervalMovingMs = 100;
	configuration.advIntervalSleepMs = 1000;
}

void AssetModule::ConfigurationLoadedHandler(
		ModuleConfiguration* migratableConfig, u16 migratableConfigLength) {

#if IS_INACTIVE(GW_SAVE_SPACE)
	logt("ASSET_MODULE", "INITIATION");

	//Start broadcasting at high interval, will be disabled if accelerometer detects no movement
	BroadcastAssetAdvertisingPacket(configuration.advIntervalMovingMs);

	//Disable MeshAccessModule Broadcasting job to spend all resources on asset advertising
	MeshAccessModule* maModule = (MeshAccessModule*) GS->node.GetModuleById(
			ModuleId::MESH_ACCESS_MODULE);
	if (maModule != nullptr) {
		maModule->DisableBroadcast();
	}

#endif
	logt("ASSET_MODULE", "ConfigHandler");
}

void AssetModule::TimerEventHandler(u16 passedTimeDs, u32 appTimerDs) {
#if IS_INACTIVE(GW_SAVE_SPACE)
	//Updates the asset packet all the time if no accelerometer is present
	//FIXME: only needed for different channel mask currently, otherwise no need to update
	BroadcastAssetAdvertisingPacket(1000);
#endif
}

void AssetModule::BroadcastAssetAdvertisingPacket(u16 advIntervalMs) {
	logt("ASSET_MODULE", "BROADCAST");

	currentAdvChannel = Utility::GetRandomInteger() % 3;

//build advertising packet
	AdvJob job = { AdvJobTypes::SCHEDULED, 3, //Slots
			0, //Delay
			MSEC_TO_UNITS(100, UNIT_0_625_MS), //AdvInterval
			0, //AdvChannel
			0, //CurrentSlots
			0, //CurrentDelay
			GapAdvType::ADV_IND, //Advertising Mode
			{ 0 }, //AdvData
			0, //AdvDataLength
			{ 0 }, //ScanData
			0 //ScanDataLength
			};

//Select either the new advertising job or the already existing
	AdvJob* currentJob;
	if (assetJobHandle == NULL) {
		currentJob = &job;
	} else {
		currentJob = assetJobHandle;
	}
	u8* bufferPointer = currentJob->advData;

//Update the advertising interval for our job
	currentJob->advertisingInterval = MSEC_TO_UNITS(advIntervalMs,
			UNIT_0_625_MS);
	currentJob->advertisingChannelMask = 0x07 ^ (1 << currentAdvChannel); //Rotate adv channel

	advStructureFlags* flags = (advStructureFlags*) bufferPointer;
	flags->len = SIZEOF_ADV_STRUCTURE_FLAGS - 1; //minus length field itself
	flags->type = BLE_GAP_AD_TYPE_FLAGS;
	flags->flags = BLE_GAP_ADV_FLAG_LE_GENERAL_DISC_MODE
			| BLE_GAP_ADV_FLAG_BR_EDR_NOT_SUPPORTED;

	advStructureUUID16* serviceUuidList = (advStructureUUID16*) (bufferPointer
			+ SIZEOF_ADV_STRUCTURE_FLAGS);
	serviceUuidList->len = SIZEOF_ADV_STRUCTURE_UUID16 - 1;
	serviceUuidList->type = BLE_GAP_AD_TYPE_16BIT_SERVICE_UUID_COMPLETE;
	serviceUuidList->uuid = SERVICE_DATA_SERVICE_UUID16;

	advPacketAssetServiceData* serviceData =
			(advPacketAssetServiceData*) (bufferPointer
					+ SIZEOF_ADV_STRUCTURE_FLAGS + SIZEOF_ADV_STRUCTURE_UUID16);
	serviceData->len = SIZEOF_ADV_STRUCTURE_ASSET_SERVICE_DATA - 1;
	serviceData->type = BLE_GAP_AD_TYPE_SERVICE_DATA;
	serviceData->messageType = SERVICE_DATA_MESSAGE_TYPE_ASSET;

	serviceData->direction = 0xFF; //TODO: Fill with other values

	if (configuration.enableBarometer && Boardconfig->spiM0SSBmePin != -1) {
#ifdef NRF52
#endif
	}
	if (ASSET_MODULE_ENCRYPT_ADV_DATA) {
		//TODO: Generate keystream from asset key and current timer value (divided so it changes all 10 seconds e.g.)

		//TODO: Calculate MIC
	}

	u32 length = SIZEOF_ADV_STRUCTURE_FLAGS + SIZEOF_ADV_STRUCTURE_UUID16
			+ SIZEOF_ADV_STRUCTURE_ASSET_SERVICE_DATA;
	job.advDataLength = length;

	//Either update the job or create it if not done
	if (assetJobHandle == NULL) {
		assetJobHandle = GS->advertisingController.AddJob(job);
	} else {
		GS->advertisingController.RefreshJob(assetJobHandle);
	}

	char cbuffer[100];
	logt("ASMOD", "Broadcasting asset data %s, len %u", cbuffer, length);

}

void AssetModule::MeshMessageReceivedHandler(BaseConnection* connection,
		BaseConnectionSendData* sendData, connPacketHeader* packetHeader) {

}

#ifdef TERMINAL_ENABLED
bool AssetModule::TerminalCommandHandler(char* commandArgs[],
		u8 commandArgsSize) {
	//Must be called to allow the module to get and set the config
	return Module::TerminalCommandHandler(commandArgs, commandArgsSize);
}
#endif

