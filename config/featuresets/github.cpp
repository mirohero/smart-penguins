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
#include "Config.h"
#include "Node.h"
#include "Utility.h"
#include "DebugModule.h"
#include "StatusReporterModule.h"
#include "AdvertisingModule.h"
#include "ScanningModule.h"
#include "EnrollmentModule.h"
#include "IoModule.h"
#include "MeshAccessModule.h"
#include "GlobalState.h"
#include "AlarmModule.h"
#include "AssetModule.h"


void setFeaturesetConfiguration_github(ModuleConfiguration *config, void *module)
{
	if (config->moduleId == ModuleId::BOARD_CONFIG)
	{
	}
	else if (config->moduleId == ModuleId::CONFIG)
	{
		Conf::getInstance().defaultLedMode = LedMode::CONNECTIONS;
		Conf::getInstance().terminalMode = TerminalMode::PROMPT;
	}
	else if (config->moduleId == ModuleId::NODE)
	{
		//Specifies a default enrollment for the github configuration
		//This enrollment will be overwritten as soon as the node is either enrolled or the enrollment removed
		NodeConfiguration *c = (NodeConfiguration *)config;
		c->enrollmentState = EnrollmentState::ENROLLED;
		// network id has to be the same for all devices
		c->networkId = 11;
		// nodeId to use for the devices to flash
		c->nodeId = 17;
		c->direction = 8;
		c->boardType = 1;
		c->checkDirection = true;
		CheckedMemset(c->networkKey, 0x00, 16);
	}
}

u32 initializeModules_github(bool createModule)
{
	u32 size = 0;
	size += GS->InitializeModule<DebugModule>(createModule);
	size += GS->InitializeModule<StatusReporterModule>(createModule);
	size += GS->InitializeModule<AdvertisingModule>(createModule);
	size += GS->InitializeModule<ScanningModule>(createModule);
	size += GS->InitializeModule<EnrollmentModule>(createModule);
	size += GS->InitializeModule<IoModule>(createModule);
	size += GS->InitializeModule<MeshAccessModule>(createModule);
	size += GS->InitializeModule<AssetModule>(createModule);
	size += GS->InitializeModule<AlarmModule>(createModule);

	return size;
}

DeviceType getDeviceType_github()
{
	return DeviceType::STATIC;
}
