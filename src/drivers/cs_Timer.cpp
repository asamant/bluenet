/**
 * Author: Crownstone Team
 * Copyright: Crownstone (https://crownstone.rocks)
 * Date: 20 Aug., 2015
 * License: LGPLv3+, Apache License 2.0, and/or MIT (triple-licensed)
 */

#include <drivers/cs_Timer.h>
#include <drivers/cs_Serial.h>
#include <cfg/cs_Strings.h>
#include <util/cs_BleError.h>
#include "nrfx_clock.h"

Timer& Timer::getInstance() {
	static Timer instance;
	return instance;
}

void Timer::init() {
	LOGi(FMT_INIT, "timer");
	uint32_t err_code = app_timer_init();
	APP_ERROR_CHECK(err_code);
	APP_SCHED_INIT(SCHED_MAX_EVENT_DATA_SIZE, SCHED_QUEUE_SIZE);
	LOGi("Scheduler requires %uB ram. Evt size=%u", (SCHED_MAX_EVENT_DATA_SIZE + APP_SCHED_EVENT_HEADER_SIZE) * (SCHED_QUEUE_SIZE + 1), SCHED_MAX_EVENT_DATA_SIZE);
}

void Timer::createSingleShot(app_timer_id_t& timer_handle, app_timer_timeout_handler_t func) {
	BLE_CALL(app_timer_create, (&timer_handle, APP_TIMER_MODE_SINGLE_SHOT, func));
}

void Timer::start(app_timer_id_t& timer_handle, uint32_t ticks, void* obj) {
	if (ticks < APP_TIMER_MIN_TIMEOUT_TICKS) {
		LOGe("Tried to start a timer with %d ticks");
		return;
	}
	BLE_CALL(app_timer_start, (timer_handle, ticks, obj));
}

void Timer::stop(app_timer_id_t& timer_handle) {
	BLE_CALL(app_timer_stop, (timer_handle));
}

void Timer::reset(app_timer_id_t& timer_handle, uint32_t ticks, void* obj) {
	BLE_CALL(app_timer_stop, (timer_handle));
	BLE_CALL(app_timer_start, (timer_handle, ticks, obj));
}

void activate_hfclk(){
	// Perhaps no the most appropriate place to start the HF timer
	// sd_clock_hfclk_request();
	// nrfx_clock_hfclk_start();

	// Start the external high frequency crystal
	NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
	NRF_CLOCK->TASKS_HFCLKSTART = 1;

	// Wait for the external oscillator to start up
	while (NRF_CLOCK->EVENTS_HFCLKSTARTED == 0) {}

 	// while(!nrfx_clock_hfclk_is_running());
}
