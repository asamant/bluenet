/**
 * Author: Crownstone Team
 * Copyright: Crownstone (https://crownstone.rocks)
 * Date: Jun 3, 2016
 * License: LGPLv3+, Apache License 2.0, and/or MIT (triple-licensed)
 */
#pragma once


//TODO: make a enum class of these
enum CommandHandlerTypes {
	CTRL_CMD_SETUP                       = 0,
	CTRL_CMD_FACTORY_RESET               = 1,
	CTRL_CMD_STATE_GET                   = 2,
	CTRL_CMD_STATE_SET                   = 3,

	CTRL_CMD_RESET                       = 10,
	CTRL_CMD_GOTO_DFU                    = 11,
	CTRL_CMD_NOP                         = 12,
	CTRL_CMD_DISCONNECT                  = 13,

	CTRL_CMD_SWITCH                      = 20,
	CTRL_CMD_MULTI_SWITCH                = 21,
	CTRL_CMD_PWM                         = 22,
	CTRL_CMD_RELAY                       = 23,

	CTRL_CMD_SET_TIME                    = 30,
	CTRL_CMD_INCREASE_TX                 = 31,
	CTRL_CMD_RESET_ERRORS                = 32,
	CTRL_CMD_MESH_COMMAND                = 33,

	CTRL_CMD_ALLOW_DIMMING               = 40,
	CTRL_CMD_LOCK_SWITCH                 = 41,
	CTRL_CMD_ENABLE_SWITCHCRAFT          = 42,

	CTRL_CMD_UART_MSG                    = 50,
	CTRL_CMD_UART_ENABLE                 = 51,

	CTRL_CMD_UNKNOWN                     = 0xFFFF
};
