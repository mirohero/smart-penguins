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

#pragma once

#include <Module.h>

#define SERVICE_DATA_MESSAGE_TYPE_ASSET 0x02

#pragma pack(push)
#pragma pack(1)

#pragma pack(pop)


#include <AdvertisingController.h>

extern "C"{
#include <nrf_drv_gpiote.h>
}


typedef struct
{
	i16 x;
	i16 y;
	i16 z;
} ThreeDimStruct;

typedef struct
{
	ThreeDimStruct acc;
	ThreeDimStruct vel;
	u32 bar;
	u32 timeStamp;
} AssetModuleMeshMessage;


class AssetModule: public Module
{
private:
	//Module configuration that is saved persistently (size must be multiple of 4)
	struct AssetModuleConfiguration: ModuleConfiguration
	{
		//Insert more persistent config values here
		float wakeupThreshold;
		u16 wakeupDuration;
		u16 movementEndThresholdMilliG; //If below this threshold for a number of steps, movement will end
		u16 movementEndDelayDs; // Number of steps to wait after no movement was detected
		u8 enableAccelerometer;
		u8 enableBarometer;
		u16 advIntervalMovingMs; //Advertising interval during movement
		u16 advIntervalSleepMs; //Advertising interval during standstill

	};

	enum AssetModuleActionResponseMessages
	{

	};

	AdvJob* assetJobHandle;

	//Use for movement end detection
	u32 lastMovementTimeDs;

	u8 currentAdvChannel;

	ThreeDimStruct prev_acc;
	ThreeDimStruct currentAcc;
	ThreeDimStruct vel;
	bool moving = false;

	//barometer
	u32 lastBarometerReadTimeDs;
	u32 lastPressureReading;
	i32 lastTemperatureReading;
	u32 lastHumidityReading;




	void SendAssetDataToMesh(ThreeDimStruct * acc, ThreeDimStruct * vel, u32 appTimerDs,u32 bar);

	void printArray(const char * preamble, u8 * ptr, u8 len);

public:


	DECLARE_CONFIG_AND_PACKED_STRUCT (AssetModuleConfiguration);


	AssetModule();

	void ConfigurationLoadedHandler(ModuleConfiguration* migratableConfig, u16 migratableConfigLength) override;
	void ResetToDefaultConfiguration();

	void BroadcastAssetAdvertisingPacket(u16 advIntervalMs);

	void MeshMessageReceivedHandler(BaseConnection* connection, BaseConnectionSendData* sendData, connPacketHeader* packetHeader);

	void TimerEventHandler(u16 passedTimeDs, u32 appTimerDs);

	bool TerminalCommandHandler(char* commandArgs[], u8 commandArgsSize);

	void UpdateAssetDataAdvPacket(u16 advertisingIntervalinMs, u8 accelerometerData, u8 barometerData);

};

