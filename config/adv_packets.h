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
 * This file holds some structures that are used to create and parse advertising
 * packets used by the mesh.
 * */
#pragma once

#include <types.h>

#define ADV_PACKET_MAX_SIZE 31

//Start packing all these structures
//These are packed so that they can be transmitted savely over the air
//Smaller datatypes could be implemented with bitfields?
//Sizeof operator is not to be trusted because of padding
// Pay attention to http://www.keil.com/support/man/docs/armccref/armccref_Babjddhe.htm

#pragma pack(push)
#pragma pack(1)

//###### AD structures for advertising messages ###############################

//BLE AD Type FLAGS
#define SIZEOF_ADV_STRUCTURE_FLAGS 3
typedef struct
{
	u8 len;
	u8 type;
	u8 flags;
}advStructureFlags;
STATIC_ASSERT_SIZE(advStructureFlags, 3);

//BLE AD Type full local name
#define SIZEOF_ADV_STRUCTURE_NAME 4
typedef struct
{
	u8 len;
	u8 type;
	u8 name[2];
}advStructureName;
STATIC_ASSERT_SIZE(advStructureName, 4);

//BLE AD Type full list of services
#define SIZEOF_ADV_STRUCTURE_UUID128 18
typedef struct
{
	u8 len;
	u8 type;
	u8 uuid[16];
}advStructureUUID128;
STATIC_ASSERT_SIZE(advStructureUUID128, 18);


//BLE AD Type list of 16-bit service UUIDs
#define SIZEOF_ADV_STRUCTURE_UUID16 4
typedef struct
{
	u8 len;
	u8 type;
	u16 uuid;
}advStructureUUID16;
STATIC_ASSERT_SIZE(advStructureUUID16, 4);

//Header of service data + our custom messageType
#define SIZEOF_ADV_STRUCTURE_SERVICE_DATA_AND_TYPE 6
typedef struct
{
	u8 len;
	u8 type;
	u16 uuid;
	u16 messageType; //Message type depending on our custom service
}advStructureServiceDataAndType;
STATIC_ASSERT_SIZE(advStructureServiceDataAndType, 6);

//BLE AD Type Manufacturer specific data
#define SIZEOF_ADV_STRUCTURE_MANUFACTURER 4
typedef struct
{
	u8 len;
	u8 type;
	u16 companyIdentifier;
}advStructureManufacturer;
STATIC_ASSERT_SIZE(advStructureManufacturer, 4);


#define SIZEOF_ADV_PACKET_SERVICE_AND_DATA_HEADER (SIZEOF_ADV_STRUCTURE_FLAGS+SIZEOF_ADV_STRUCTURE_UUID16+SIZEOF_ADV_STRUCTURE_SERVICE_DATA_AND_TYPE)
typedef struct
{
	advStructureFlags flags;
	advStructureUUID16 uuid;
	advStructureServiceDataAndType data;
}advPacketServiceAndDataHeader;
STATIC_ASSERT_SIZE(advPacketServiceAndDataHeader, 13);

//####### Advertising packets => Message Types #################################################

//Message types: Protocol defined, up to 19 because we want to have a unified
//type across advertising and connection packets if we need to unify these.
#define MESSAGE_TYPE_JOIN_ME_V0 1


//####### Advertising packets => Structs #################################################

// Header that is used for custom data using the manufacturer specific ad type
#define SIZEOF_ADV_PACKET_HEADER_MANUFACTURER (SIZEOF_ADV_STRUCTURE_FLAGS + SIZEOF_ADV_STRUCTURE_MANUFACTURER + 1)
typedef struct
{
	advStructureFlags flags;
	advStructureManufacturer manufacturer;
	u8 messageType;
}advPacketHeaderManufacturer;
STATIC_ASSERT_SIZE(advPacketHeaderManufacturer, 8);

//Header that is common to all mesh advertising messages
#define SIZEOF_ADV_PACKET_STUFF_AFTER_MANUFACTURER 4 //1byte mesh identifier + 2 byte networkid + 1 byte message type
#define SIZEOF_ADV_PACKET_HEADER (SIZEOF_ADV_STRUCTURE_FLAGS + SIZEOF_ADV_STRUCTURE_MANUFACTURER + SIZEOF_ADV_PACKET_STUFF_AFTER_MANUFACTURER) //11 byte
typedef struct
{
	advStructureFlags flags;
	advStructureManufacturer manufacturer;
	u8 meshIdentifier;
	NetworkId networkId;
	u8 messageType;
}advPacketHeader;
STATIC_ASSERT_SIZE(advPacketHeader, 11);

// ==> This leaves us with 20 bytes payload that are saved in the manufacturer specific data field


//JOIN_ME packet that is used for cluster discovery
//TODO: Add  the current discovery mode/length,... which would allow other nodes to determine
//		How long they need to wait until this node scans or advertises again?

//This is v0 of the packet, other versions will have different values in the packet,
//Future research must show which values are the most interesting to determine the
//best connection partner.
#define SIZEOF_ADV_PACKET_PAYLOAD_JOIN_ME_V0 20
typedef struct
{
	NodeId sender;
	ClusterId clusterId; //Consists of the founding node's id and the connection loss / restart counter
	ClusterSize clusterSize;
	u8 freeMeshInConnections : 3; //Up to 8 in-connections
	u8 freeMeshOutConnections : 5; //Up to 32 out-connections

	u8 batteryRuntime; //batteryRuntime. Contains the expected runtime of the device (1-59=minutes, 60-83=1-23hours, 84-113=1-29days, 114-233=1-119months, 234-254=10-29year, 255=infinite)
	i8 txPower; //txPower. Send power in two's complement dbm
	DeviceType deviceType; //Type of device
	u16 hopsToSink; //Number of hops to the shortest sink
	u16 meshWriteHandle; //The GATT handle for the mesh communication characteristic
	ClusterId ackField;//Contains the acknowledgement from another node for the slave connection procedure
}advPacketPayloadJoinMeV0;
STATIC_ASSERT_SIZE(advPacketPayloadJoinMeV0, 20);

//####### Flooding packet #################################################
/*
 * This packet is used to send information over the advertising channels in
 * a flooding manner. This is very inefficient and only one packet can be sent at once
 */

#define SIZEOF_ADV_PACKET_FLOOD (SIZEOF_ADV_PACKET_HEADER + 5) //Data region is variable, add its size in bytes
typedef struct
{
	advPacketHeader header;
	NodeId senderId;
	NodeId receiverId;
	u8 packetId; //A packet id in combination with the senderId is unique for a long time period
	u8 data[ADV_PACKET_MAX_SIZE - SIZEOF_ADV_PACKET_FLOOD]; //Data can be larger and will be transmitted in subsequent packets

}advPacketFlood;
STATIC_ASSERT_SIZE(advPacketFlood, 31);

//####### Asset Tracking #################################################

#define SERVICE_DATA_MESSAGE_TYPE_ASSET 0x02

#pragma pack(push)
#pragma pack(1)

//Service Data (max. 24 byte)
#define SIZEOF_ADV_STRUCTURE_ASSET_SERVICE_DATA 24
typedef struct
{
	//6 byte header
	u8 len;
	u8 type;
	u16 uuid;
	u16 messageType; //0x02 for Asset Service

	//1 byte capabilities
	u8 advertisingChannel : 2; // 0 = not available, 1=37, 2=38, 3=39
	u8 gyroscopeAvailable : 1;
	u8 magnetometerAvailable : 1;
	u8 reservedBit : 4;

	//11 byte assetData
	u32 serialNumberIndex;
	u8 batteryPower; //0xFF = not available
	u8 speed; //0xFF = not available
	u8 direction; //0xFF = not available
	u16 pressure; //0xFFFF = not available
	i8 temperature; //0xFF = not available
	u8 humidity; //0xFF = not available

	u16 reserved;

	u32 encryptionMIC;

}advPacketAssetServiceData;
STATIC_ASSERT_SIZE(advPacketAssetServiceData, SIZEOF_ADV_STRUCTURE_ASSET_SERVICE_DATA);

#pragma pack(pop)

//####### Further definitions #################################################

/*
 * Explanation:
The JOIN_ME packet can have a number of different fields that contain different information.
The version number indicates the JOIN_ME packet type
This information can then be used in the clusterScore function to build clusters based
on different criteria
 * */

#define SIZEOF_ADV_PACKET_JOIN_ME (SIZEOF_ADV_PACKET_HEADER + SIZEOF_ADV_PACKET_PAYLOAD_JOIN_ME_V0)
typedef struct
{
	advPacketHeader header;
	advPacketPayloadJoinMeV0 payload;
}advPacketJoinMeV0;
STATIC_ASSERT_SIZE(advPacketJoinMeV0, 31);


//######## Scan Response packets ##############################################

//A header that is used for scan response packets
#define SIZEOF_SCAN_PACKET_HEADER (SIZEOF_ADV_STRUCTURE_NAME + SIZEOF_ADV_STRUCTURE_MANUFACTURER)
typedef struct
{
	advStructureName name;
	advStructureManufacturer manufacturer;
}scanPacketHeader;
STATIC_ASSERT_SIZE(scanPacketHeader, 8);


//End Packing
#pragma pack(pop)
