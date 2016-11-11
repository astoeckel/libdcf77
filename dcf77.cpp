/**
 *  libdcf77 -- Cross Platform C++ DCF77 decoder
 *  Copyright (C) 2016  Andreas Stöckel
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Affero General Public License as
 *  published by the Free Software Foundation, either version 3 of the
 *  License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Affero General Public License for more details.
 *
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "dcf77.hpp"

namespace dcf77 {

/******************************************************************************
 * Class "debounce"                                                           *
 ******************************************************************************/

static constexpr uint8_t FIXED_POINT_LOG2_BASE = 7;
static constexpr uint8_t FIXED_POINT_BASE = (1 << FIXED_POINT_LOG2_BASE);
static constexpr uint8_t FLT_F1 = FIXED_POINT_BASE * 0.97;
static constexpr uint8_t FLT_F2 = FIXED_POINT_BASE - FLT_F1;

static constexpr uint8_t filter(bool ctrl, uint8_t x)
{
	return (uint16_t(x) * uint16_t(FLT_F1) +
	        (ctrl ? uint16_t(FLT_F2) << FIXED_POINT_LOG2_BASE : 0)) >>
	       FIXED_POINT_LOG2_BASE;
}

static constexpr uint8_t filter_convergence(bool ctrl,
                                            uint8_t x = FIXED_POINT_BASE / 2)
{
	return filter(ctrl, x) == x ? x : filter_convergence(ctrl, filter(ctrl, x));
}

static constexpr uint8_t FLT_MAX = filter_convergence(true);
static constexpr uint8_t FLT_MIN = filter_convergence(false);

debounce::debounce(uint8_t hysteresis)
    : m_low_pass(FIXED_POINT_BASE / 2), m_last_t(0), m_last_state_change(0),
      m_hysteresis((uint16_t(hysteresis) * (FLT_MAX - FLT_MIN)) >> 8),
      m_last_input_value(false)
{
}

const debounce::result &debounce::sample(bool value, uint16_t t)
{
	// Apply a low-pass filter to the input signal
	const uint16_t dt = t - m_last_t;
	uint8_t lv = m_low_pass;
	for (uint16_t i = 0; i < dt; i++) {
		m_low_pass = filter(value, m_low_pass);
		if (m_low_pass == lv) {
			break;
		}
		lv = m_low_pass;
	}

	// Remember the time of the last state change
	if (value != m_last_input_value) {
		m_last_state_change = t;
	}

	// Assemble the result structure, apply the hysteresis
	if (m_low_pass > FLT_MAX - m_hysteresis && m_result.value == false) {
		m_result.t = m_last_state_change;
		m_result.edge = true;
		m_result.value = true;
	} else if (m_low_pass < FLT_MIN + m_hysteresis && m_result.value == true) {
		m_result.t = m_last_state_change;
		m_result.edge = true;
		m_result.value = false;
	} else {
		m_result.edge = false;
	}

	// Remember time and input/output values
	m_last_t = t;
	m_last_input_value = value;

	return m_result;
}

/******************************************************************************
 * Union "data"                                                               *
 ******************************************************************************/

template <typename T>
uint8_t parity(T x)
{
	return __builtin_popcount(x) & 1;
}

template <uint8_t max_hi, uint8_t max_lo, typename T>
static bool valid_bcd(T x)
{
	const uint8_t hi = (x & 0xF0) >> 4;
	const uint8_t lo = (x & 0x0F) >> 0;

	// The individual digits must not be larger than 9!
	if (hi > 9 || lo > 9) {
		return false;
	}

	// The first digit must not be larger than the maximum first digit
	if (hi > max_hi) {
		return false;
	}

	// If the first digit is equal to the maximum first digit, the second digit
	// must not be larger than the maximum second digit
	if (hi == max_hi && lo > max_lo) {
		return false;
	}

	return true;
};

bool data::valid(bool time_and_date_only) const
{
	return (time_and_date_only || raw.minute_start == 0) &&
	       (raw.time_start == 1) // Constant flags
	       &&
	       (time_and_date_only || raw.cest != raw.cet) // There can be only one!
	       && (raw.parity_minute == parity(raw.minute)) && // Check parity
	       (raw.parity_hour == parity(raw.hour)) &&
	       (raw.parity_date ==
	        parity(uint32_t((bitstream & 0x3FFFFF000000000LL) >> 32))) &&
	       valid_bcd<5, 9>(raw.minute) && // Check BCD values
	       valid_bcd<2, 3>(raw.hour) && valid_bcd<3, 1>(raw.day) &&
	       (raw.day > 0) && (raw.day_of_week > 0) &&
	       valid_bcd<1, 2>(raw.month) && (raw.month > 0) &&
	       valid_bcd<9, 9>(raw.year);
}

/******************************************************************************
 * Class "decoder"                                                            *
 ******************************************************************************/

decoder::state decoder::sample(bool value, uint16_t t)
{
	auto event = m_debouncer.sample(value, t);
	state res = state::no_result;
	if (event.edge) {
		uint16_t dt = event.t - m_last_t;
		if (!event.value) {
			// Falling edge
			if (dt > SYNC_HIGH_TIME - SLACK) {
				// Handle a sync event
				if (m_state < 59) {
					m_data_new.bitstream = m_data_new.bitstream
					                       << (59 - m_state);
					if (m_data_new.valid(true)) {
						res = state::has_time_and_date;
					}
				} else if (m_data_new.valid(false)) {
					res = state::has_complete;
				}
				if (res >= state::has_time_and_date) {
					m_data_current = m_data_new;
					m_phase = event.t;
				}
				m_state = 0;
				m_data_new.bitstream = 0;
			}
		}
		if (event.value) {
			// Rising edge
			if (dt > LOW_ZERO_TIME - SLACK) {
				// We received a "one" or a "zero"
				if (dt > LOW_ONE_TIME - SLACK) {
					// It's a "one"
					m_data_new.bitstream |= uint64_t(1) << m_state;
				}
				m_state++;
			}
		}
		m_last_t = event.t;
	}
	return res;
}
}
