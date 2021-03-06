/**
 * Author: Crownstone Team
 * Copyright: Crownstone (https://crownstone.rocks)
 * Date: 26 May., 2019
 * License: LGPLv3+, Apache License 2.0, and/or MIT (triple-licensed)
 */

#pragma once

#include "common/cs_Types.h"
#include "ble/cs_iBeacon.h"

extern "C" {
#include "advertiser.h"
}

/**
 * Buffer for the advertising packet.
 *
 * For some reason the buffer needs to be a bit larger, else we get stuck in an infinite loop in
 * packet_buffer.c::packet_buffer_packets_ready_to_pop()
 */
#define MESH_ADVERTISER_BUF_SIZE (ADVERTISER_PACKET_BUFFER_PACKET_MAXLEN + 4)

class MeshAdvertiser {
public:
	void init();

	/**
	 * Set the advertised MAC address.
	 */
	void setMacAddress(uint8_t* address);

	/**
	 * Set the advertisement interval.
	 */
	void setInterval(uint32_t intervalMs);

	/**
	 * Set the TX power.
	 */
	void setTxPower(int8_t power);

	/**
	 * Set iBeacon as advertised data.
	 */
	void setIbeaconData(IBeacon* ibeacon);

	/**
	 * Start advertising.
	 */
	void start();
private:
	advertiser_t* _advertiser = NULL;
	uint8_t* _buffer = NULL;
	adv_packet_t* _advPacket = NULL;
};
