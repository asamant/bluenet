/*
 * Author: Crownstone Team
 * Copyright: Crownstone (https://crownstone.rocks)
 * Date: Oct 23, 2019
 * License: LGPLv3+, Apache License 2.0, and/or MIT (triple-licensed)
 */
#pragma once

#include "structs/buffer/cs_CharacteristicBuffer.h"

class CharacteristicWriteBuffer : public CharacteristicBuffer {
public:
	static CharacteristicWriteBuffer& getInstance() {
		static CharacteristicWriteBuffer instance;
		return instance;
	};
};
