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
#pragma once

#include "MeshConnection.h"
#include "ResolverConnection.h"
#include "MeshAccessConnection.h"

#if IS_ACTIVE(CLC_CONN)
#include "ClcAppConnection.h"
#endif
#include "Utility.h"
#include "SimpleArray.h"

/*
* The ConnectionAllocator is an implementation of a PoolAllocator, specialized on
* Connections. It is able to allocate and deallocate any Connection.
*/

class ConnectionAllocator {
private:
	union AnyConnection
	{
		AnyConnection* nextConnection;

		MeshConnection meshConnection;
		ResolverConnection resolverConnection;
		MeshAccessConnection meshAccessConnection;
#if IS_ACTIVE(CLC_CONN)
		ClcAppConnection clcAppConnection;
#endif
		AnyConnection(){ /*do nothing*/ }
		~AnyConnection(){/*do nothing*/ } //LCOV_EXCL_LINE C++ deletes a destructor by default. MSVC issues a warning for it.
		                                  //This surpresses it. However, it is never executed.
	};

	static constexpr AnyConnection* NO_NEXT_CONNECTION = nullptr;
	SimpleArray<AnyConnection, TOTAL_NUM_CONNECTIONS + 1> data;	//Max + one resolver connection.
	AnyConnection* dataHead = NO_NEXT_CONNECTION;

	AnyConnection* allocateMemory();


public:
	ConnectionAllocator();
	static ConnectionAllocator& getInstance();


	MeshConnection*       allocateMeshConnection(u8 id, ConnectionDirection direction, fh_ble_gap_addr_t* partnerAddress, u16 partnerWriteCharacteristicHandle);
	ResolverConnection*   allocateResolverConnection(u8 id, ConnectionDirection direction, fh_ble_gap_addr_t* partnerAddress);
	MeshAccessConnection* allocateMeshAccessConnection(u8 id, ConnectionDirection direction, fh_ble_gap_addr_t* partnerAddress, u32 fmKeyId, MeshAccessTunnelType tunnelType);
#if IS_ACTIVE(CLC_CONN)
	ClcAppConnection*     allocateClcAppConnection(u8 id, ConnectionDirection direction, fh_ble_gap_addr_t* partnerAddress);
#endif

	void deallocate(BaseConnection* bc);
};
