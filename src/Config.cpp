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

#include <Config.h>
#include <Boardconfig.h>
#include <GlobalState.h>
#include <Logger.h>
#include <RecordStorage.h>
#include <Utility.h>

#ifdef SIM_ENABLED
#include "CherrySim.h"
#endif

#define CONFIG_CONFIG_VERSION 2

//Config.cpp initializes variables defined in Config.h with values from UICR

//Put the firmware version in a special section right after the initialization vector
#ifndef SIM_ENABLED
u32 fruityMeshVersion __attribute__((section(".Version"), used)) = FM_VERSION;
u32 appMagicNumber __attribute__((section(".AppMagicNumber"), used)) = APP_ID_MAGIC_NUMBER;
#else
u32 fruityMeshVersion = FM_VERSION;
#endif

Conf::Conf()
{
	//If firmware groupids are defined, we save them in our config
	CheckedMemset(fwGroupIds, 0x00, sizeof(fwGroupIds));
#ifdef SET_FW_GROUPID_CHIPSET
	fwGroupIds[0] = SET_FW_GROUPID_CHIPSET;
#endif
#ifdef SET_FW_GROUPID_FEATURESET
	fwGroupIds[1] = SET_FW_GROUPID_FEATURESET;
#endif

}



#define _____________INITIALIZING_______________

void Conf::Initialize(bool safeBootEnabled)
{
	this->safeBootEnabled = safeBootEnabled;

	//First, fill with default Settings from the codebase
	LoadDefaults();

	//If there is UICR data available, we use it to fill uninitialized parts of the config
	LoadUicr();

	//Overwrite with settings from the settings page if they exist
	if (!safeBootEnabled) {
		LoadSettingsFromFlashWithId(ModuleId::CONFIG, (ModuleConfiguration*)&configuration, sizeof(ConfigConfiguration));
	}

	SET_FEATURESET_CONFIGURATION(&configuration, this);
}

void Conf::LoadDefaults(){
	configuration.moduleId = ModuleId::CONFIG;
	configuration.moduleVersion = 4;
	configuration.moduleActive = true;
	configuration.reserved = sizeof(ConfigConfiguration);
	configuration.isSerialNumberIndexOverwritten = false;
	configuration.overwrittenSerialNumberIndex = 0;

	CheckedMemset(configuration.preferredPartnerIds, 0, sizeof(configuration.preferredPartnerIds));
	configuration.preferredConnectionMode = PreferredConnectionMode::PENALTY;
	configuration.amountOfPreferredPartnerIds = 0;

	if (Boardconfig->boardType == 7) {	//jstodo remove if build bug is fixed. (Linker gives random errors if removed currently, see 7d0838770043e1d8d00c9ec8bdd149c93810828c)
		terminalMode = TerminalMode::DISABLED;
	}
	else {
		terminalMode = TerminalMode::JSON;
	}

	defaultLedMode = LedMode::CONNECTIONS;

	enableSinkRouting = false;
	//Check if the BLE stack supports the number of connections and correct if not
#ifdef SIM_ENABLED
	BleStackType stackType = FruityHal::GetBleStackType();
	if (stackType == BleStackType::NRF_SD_130_ANY) {
		//S130 only supports 1 peripheral connection
		totalInConnections = 1;
		meshMaxInConnections = 1;
	}
#endif

	meshMinConnectionInterval = (u16)MSEC_TO_UNITS(10, UNIT_1_25_MS);
	meshMaxConnectionInterval = (u16)MSEC_TO_UNITS(10, UNIT_1_25_MS);

	meshScanIntervalHigh = (u16)MSEC_TO_UNITS(20, UNIT_0_625_MS);
	meshScanWindowHigh = (u16)MSEC_TO_UNITS(3, UNIT_0_625_MS);

	meshScanIntervalLow = (u16)MSEC_TO_UNITS(250, UNIT_0_625_MS);
	meshScanWindowLow = (u16)MSEC_TO_UNITS(3, UNIT_0_625_MS);

	//Set defaults for stuff that is loaded from UICR in case that no UICR data is present
	manufacturerId = MANUFACTURER_ID;
	Conf::generateRandomSerialAndNodeId();
	CheckedMemset(configuration.nodeKey, 0x11, 16);
	defaultNetworkId = 0;
	CheckedMemset(defaultNetworkKey, 0xFF, 16);
	CheckedMemset(defaultUserBaseKey, 0xFF, 16);
	CheckedMemset(&staticAccessAddress.addr, 0xFF, 6);
	staticAccessAddress.addr_type = 0xFF;
	highToLowDiscoveryTimeSec = 0;
}

void Conf::LoadUicr(){
	u32* uicrData = FruityHal::getUicrDataPtr();

	//If UICR data is available, we fill various variables with the data
	if(uicrData != nullptr){
		/* If we write data to NRF_UICR->CUSTOMER, it will be used by fruitymesh
		 * [0] MAGIC_NUMBER, must be set to 0xF07700 when UICR data is available
		 * [1] BOARD_TYPE, accepts an integer that defines the hardware board that fruitymesh should be running on
		 * [2] SERIAL_NUMBER, the given serial number (2 words)
		 * [4] NODE_KEY, randomly generated (4 words)
		 * [8] MANUFACTURER_ID, set to manufacturer id according to the BLE company identifiers: https://www.bluetooth.org/en-us/specification/assigned-numbers/company-identifiers
		 * [9] DEFAULT_NETWORK_ID, network id if preenrollment should be used
		 * [10] DEFAULT_NODE_ID, node id to be used if not enrolled
		 * [11] DEVICE_TYPE, type of device (sink, mobile, etc,..)
		 * [12] SERIAL_NUMBER_INDEX, unique index that represents the serial number
		 * [13] NETWORK_KEY, default network key if preenrollment should be used (4 words)
		 * [17] ...
		 */

		//If magic number exists, fill Config with valid data from UICR
		deviceConfigOrigin = DeviceConfigOrigins::UICR_CONFIG;

		//=> uicrData[1] was already read in the BoardConfig class

		if(!isEmpty((u8*)(uicrData + 4), 16)){
			memcpy(configuration.nodeKey, (u8*)(uicrData + 4), 16);
		}
		if(uicrData[8] != EMPTY_WORD) manufacturerId = (u16)uicrData[8];
		if(uicrData[9] != EMPTY_WORD) defaultNetworkId = (u16)uicrData[9];
		if(uicrData[10] != EMPTY_WORD) defaultNodeId = (u16)uicrData[10];
		// if(uicrData[11] != EMPTY_WORD) deviceType = (deviceTypes)uicrData[11]; //deprectated as of 02.07.2019
		if(uicrData[12] != EMPTY_WORD) serialNumberIndex = (u32)uicrData[12];
		else if (uicrData[2] != EMPTY_WORD) {
			//Legacy uicr serial number support. Might be removed some day.
			//If you want to remove it, check if any flashed device exist 
			//and is still in use, that was not flashed with uicrData[12].
			//If AND ONLY IF this is not the case, you can savely remove it.
			char serialNumber[6];
			memcpy((u8*)serialNumber, (u8*)(uicrData + 2), 5);
			serialNumber[5] = '\0';
			serialNumberIndex = Utility::GetIndexForSerial(serialNumber);
		}

		//If no network key is present in UICR but a node key is present, use the node key for both (to migrate settings for old nodes)
		if(isEmpty((u8*)(uicrData + 13), 16) && !isEmpty(configuration.nodeKey, 16)){
			memcpy(defaultNetworkKey, configuration.nodeKey, 16);
		} else {
			//Otherwise, we use the default network key
			memcpy(defaultNetworkKey, (u8*)(uicrData + 13), 16);
		}
	}
}

u32 Conf::getFruityMeshVersion() const
{
#ifdef SIM_ENABLED
	if (cherrySimInstance->currentNode->fakeDfuVersion != 0 && cherrySimInstance->currentNode->fakeDfuVersionArmed == true) {
		return cherrySimInstance->currentNode->fakeDfuVersion;
	}
#endif
	return fruityMeshVersion;
}


#define _____________HELPERS_______________

void Conf::LoadSettingsFromFlashWithId(ModuleId moduleId, ModuleConfiguration* configurationPointer, u16 configurationLength)
{
	Conf::LoadSettingsFromFlash(nullptr, moduleId, configurationPointer, configurationLength);
}

Conf & Conf::getInstance()
{
	return GS->config;
}

void Conf::LoadSettingsFromFlash(Module* module, ModuleId moduleId, ModuleConfiguration* configurationPointer, u16 configurationLength)
{
	if (!safeBootEnabled) {
		SizedData configData = GS->recordStorage.GetRecordData((u16)moduleId);

		//Check if configuration exists and has the correct version, if yes, copy to module configuration struct
		if (configData.length > SIZEOF_MODULE_CONFIGURATION_HEADER && ((ModuleConfiguration*)configData.data)->moduleVersion == configurationPointer->moduleVersion) {
			memcpy((u8*)configurationPointer, configData.data, configData.length);

			logt("CONFIG", "Config for module %u loaded", (u32)moduleId);

			if(module != nullptr) module->ConfigurationLoadedHandler(nullptr, 0);
		}
		//If the configuration has a different version, we call the migration if it exists
		else if(configData.length > SIZEOF_MODULE_CONFIGURATION_HEADER){
			logt("CONFIG", "Flash config for module %u has mismatching version", (u32)moduleId);

			if(module != nullptr) module->ConfigurationLoadedHandler((ModuleConfiguration*)configData.data, configData.length);
		}
		else {
			logt("CONFIG", "No flash config for module %u found, using defaults", (u32)moduleId);

			if(module != nullptr) module->ConfigurationLoadedHandler(nullptr, 0);
		}
	} else {
		if(module != nullptr) module->ConfigurationLoadedHandler(nullptr, 0);
	}
}

uint32_t uint_pow(uint32_t base, uint32_t exponent){
	uint32_t result = 1;
    while (exponent){
        if (exponent & 1) result *= base;
        exponent /= 2;
        base *= base;
    }
    return result;
}

void Conf::generateRandomSerialAndNodeId(){
	//Generate a random serial number
	//This takes 5bit wide chunks from the device id to generate a serial number
	//in tests, 10k serial numbers had 4 duplicates
	u32 index = 0;
	for(int i=0; i<NODE_SERIAL_NUMBER_LENGTH; i++){
		u8 fiveBitChunk = (NRF_FICR->DEVICEID[0] & (0x1F << (i*5))) >> (i*5);
		index += uint_pow(30, i)*(fiveBitChunk % 30);
	}
	serialNumberIndex = index;
	defaultNodeId = (index + 50) % (NODE_ID_GROUP_BASE-1); //nodeId must stay within valid range
}

//Tests if a memory region in flash storage is empty (0xFF)
bool Conf::isEmpty(const u8* mem, u16 numBytes) const{
	for(u32 i=0; i<numBytes; i++){
		if(mem[i] != 0xFF) return false;
	}
	return true;
}

u32 Conf::GetSerialNumberIndex() const
{
	if (configuration.isSerialNumberIndexOverwritten) {
		return configuration.overwrittenSerialNumberIndex;
	}
	else {
		return serialNumberIndex;
	}
}

const char * Conf::GetSerialNumber() const
{
	Utility::GenerateBeaconSerialForIndex(GetSerialNumberIndex(), _serialNumber);
	return _serialNumber;
}

void Conf::SetSerialNumberIndex(u32 serialNumber)
{
	if (serialNumber == INVALID_SERIAL_NUMBER)
	{
		SIMEXCEPTION(IllegalArgumentException); //LCOV_EXCL_LINE assertion
	}

	configuration.overwrittenSerialNumberIndex = serialNumber;
	configuration.isSerialNumberIndexOverwritten = true;

	Utility::SaveModuleSettingsToFlashWithId(ModuleId::CONFIG, &configuration, sizeof(ConfigConfiguration), nullptr, 0, nullptr, 0);
}

const u8 * Conf::GetNodeKey() const
{
	return configuration.nodeKey;
}

void Conf::GetRestrainedKey(u8* buffer) const
{
	Aes128Block key;
	memcpy(key.data, GetNodeKey(), 16);

	Aes128Block messageBlock;
	memcpy(messageBlock.data, RESTRAINED_KEY_CLEAR_TEXT, 16);

	Aes128Block restrainedKeyBlock;
	Utility::Aes128BlockEncrypt(&messageBlock, &key, &restrainedKeyBlock);

	memcpy(buffer, restrainedKeyBlock.data, 16);
}

void Conf::SetNodeKey(const u8 * key)
{
	memcpy(configuration.nodeKey, key, 16);

	Utility::SaveModuleSettingsToFlashWithId(ModuleId::CONFIG, &configuration, sizeof(ConfigConfiguration), nullptr, 0, nullptr, 0);
}
