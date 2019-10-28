/**
 * Author: Crownstone Team
 * Copyright: Crownstone (https://crownstone.rocks)
 * Date: Jun 17, 2016
 * License: LGPLv3+, Apache License 2.0, and/or MIT (triple-licensed)
 */
#pragma once

enum ErrorCodesGeneral {
	ERR_SUCCESS                     = 0x00,
	ERR_WAIT_FOR_SUCCESS            = 0x01,

	ERR_BUFFER_UNASSIGNED           = 0x10,
	ERR_BUFFER_LOCKED               = 0x11,
	ERR_BUFFER_TOO_SMALL            = 0x12,

	ERR_WRONG_PAYLOAD_LENGTH        = 0x20,
	ERR_WRONG_PARAMETER             = 0x21,
	ERR_INVALID_MESSAGE             = 0x22,
	ERR_UNKNOWN_OP_CODE             = 0x23,
	ERR_UNKNOWN_TYPE                = 0x24,
	ERR_NOT_FOUND                   = 0x25,
	ERR_NO_SPACE                    = 0x26,
	ERR_BUSY                        = 0x27,

	ERR_NO_ACCESS                   = 0x30,

	ERR_NOT_AVAILABLE               = 0x40,
	ERR_NOT_IMPLEMENTED             = 0x41,
//	ERR_WRONG_SETTING               = 0x42,
	ERR_NOT_INITIALIZED             = 0x43,

	ERR_WRITE_DISABLED              = 0x50,
	ERR_WRITE_NOT_ALLOWED           = 0x51,

	ERR_ADC_INVALID_CHANNEL         = 0x60,

	ERR_UNSPECIFIED                 = 0xFFFF
};

#define SUCCESS(code) code == ERR_SUCCESS
#define FAILURE(code) code != ERR_SUCCESS
