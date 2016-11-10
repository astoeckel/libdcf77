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

/**
 * @file dcf77.hpp
 *
 * Implementation of a decoder of the time signal sent by the DCF77 station in
 * Mainflingen, Germany near Frankfurt on 77.5kHz.
 *
 * @author Andreas Stöckel
 */

#include <stdint.h>

/**
 * Namespace encompassing all types used in the DCF77 decoder.
 */
namespace dcf77 {

/**
 * A digital low-pass filter with Schmitt-Trigger with user-definable
 * hysteresis. The filter is a simple finite impulse response filter with a
 * single coefficient. The debounce filter allows to recover the input signal
 * phase.
 */
class debounce {
public:
	/**
	 * Structure describing the result of the debounce filter.
	 */
	struct result {
		/**
		 * Timestamp at which the state transition occured.
		 */
		uint16_t t;

		/**
		 * Current output state.
		 */
		bool value : 1;

		/**
		 * True if a state transition occured just now.
		 */
		bool edge : 1;

		result() : t(0), value(false), edge(false) {}
	};

private:
	/**
	 * Low-pass filtered input value.
	 */
	uint8_t m_low_pass;

	/**
	 * Last timestamp passed to the sample() function.
	 */
	uint16_t m_last_t;

	/**
	 * Time of the last raw state change.
	 */
	uint16_t m_last_state_change;

	/**
	 * User-supplied hysteresis.
	 */
	uint8_t m_hysteresis;

	/**
	 * Last input value received by the sample function.
	 */
	bool m_last_input_value;

	/**
	 * Current/last result.
	 */
	result m_result;

public:
	/**
	 * Constructor of the debounce class, allows the user to define the
	 * hysteresis.
	 *
	 * @param hysteresis is a value between zero and 255, which is mapped to a
	 * value between zero and one hundred percent (for example, the default
	 * value of 64 corresponds to 25 percent). This percentage p is then used to
	 * derive the values at which the output is switched: if the current output
	 * of the filter is zero, and the low-pass filtered value reaches 1 - p, the
	 * filter output is set to one, otherwise, if the current filter output is
	 * one and the low-pass filtered values reaches p, the output is set to
	 * zero.
	 */
	debounce(uint8_t hysteresis = 64);

	/**
	 * Processes a new sample.
	 *
	 * @param value is the input bit.
	 * @param t is a monotonous timestamp in milliseconds. Used by the sample
	 * to determine the number of filter steps.
	 */
	const result &sample(bool value, uint16_t t);
};

#pragma pack(push, 1)
/**
 * The data union stores the data received from the DCF77 radio station. It
 * provides a view on both the incomming bitstream as a 64-bit integer, as well
 * as the corresponding raw fields. The provided access methods can be used to
 * validate and read the decoded data in a convenient manner. Data should only
 * be used if the valid() method evaluates to true. When using the decoder
 * class, this check is already performed.
 */
union data {
	/**
	 * Contains the raw fields as received from the DCF77 station. Except for
	 * special applications, use the access methods below to read the data.
	 */
	struct {
		/**
		 * First minute bit -- should always be set to zero.
		 */
		uint8_t minute_start : 1;

		/**
		 * Auxiliary data -- used to tranmit alarms and weather data. The latter
		 * is encrypted.
		 */
		uint16_t aux_data : 14;

		/**
		 * Used to inform about an irregularity at the station.
		 */
		uint8_t call_bit : 1;

		/**
		 * If true, there's a swtich from CET to CEST or vice versa at the end
		 * of this hour.
		 */
		uint8_t dst_leap_hour : 1;

		/**
		 * The time described by this transmission is central european summer
		 * time (CEST).
		 */
		uint8_t cest : 1;

		/**
		 * The time described by this transmission is central european time
		 * (CET).
		 */
		uint8_t cet : 1;

		/**
		 * This hour will end with a leap second.
		 */
		uint8_t leap_second : 1;

		/**
		 * Time start indicator. Always set to one.
		 */
		uint8_t time_start : 1;

		/**
		 * The minute as 2-digit binary coded decimal (BCD).
		 */
		uint8_t minute : 7;

		/**
		 * Even parity of the minute bits.
		 */
		uint8_t parity_minute : 1;

		/**
		 * The current hour as 2-digit binary coded decimal (BCD).
		 */
		uint8_t hour : 6;

		/**
		 * Even parity of the minute bits.
		 */
		uint8_t parity_hour : 1;

		/**
		 * The current day of the month as 2-digit binary coded decimal (BCD).
		 */
		uint8_t day : 6;

		/**
		 * The current day of the week from one to seven.
		 */
		uint8_t day_of_week : 3;

		/**
		 * The current month as a value from one to twelve.
		 */
		uint8_t month : 5;

		/**
		 * The current year as a two digit BCD.
		 */
		uint8_t year : 8;

		/**
		 * Even parity of the day, day_of_week, month and year bits.
		 */
		uint8_t parity_date : 1;

		/**
		 * Additional bit used for leap seconds. Is always set to zero.
		 */
		uint8_t leap_second_bit : 1;
	} raw;

	/**
	 * Integer representing the DCF77 data. The decoder directly sets the bits
	 * in this integer. The data can then be read using the bitfields in the
	 * "raw" structure.
	 */
	uint64_t bitstream;

	/**
	 * Initialises an empty DCF77 data object.
	 */
	data() : bitstream(0) {}

	/**
	 * Validates the data contained in this object. Checks the constant flags,
	 * the parity and the numerical values for validity. If incomplete data
	 * has been read, the "time_and_date_only" flag can be used to skip
	 * validation of the parts which are not relevant for time and date.
	 *
	 * @param time_and_date_only if true, does not check the validity of the
	 * first 19 bits of the data stream.
	 */
	bool valid(bool time_and_date_only = false) const;

	/**
	 * Used to decode two-digit bcd values to bits.
	 */
	static uint8_t decode_bcd(uint8_t v)
	{
		uint8_t res = v & 0x0F;
		if (v & 0x10) {
			res += 10;
		}
		if (v & 0x20) {
			res += 20;
		}
		if (v & 0x40) {
			res += 40;
		}
		if (v & 0x80) {
			res += 80;
		}
		return res;
	}

	/**
	 * If true, daylight_saving is currently active.
	 */
	bool daylight_saving() const { return raw.cest; }

	/**
	 * If true, the timestamps will swtich from CEST to CET or vice versa in the
	 * next hour.
	 */
	bool daylight_saving_leap_hour() const { return raw.dst_leap_hour; }

	/**
	 * If true, the current hour ends with a leap second.
	 */
	bool leap_second() const { return raw.leap_second; }

	/**
	 * Minute encoded in the transmission (0 to 59).
	 */
	uint8_t minute() const { return decode_bcd(raw.minute); }

	/**
	 * Hour encoded in the transmission (0 to 23).
	 */
	uint8_t hour() const { return decode_bcd(raw.hour); }

	/**
	 * Day encoded in the transmission (1 to 31).
	 */
	uint8_t day() const { return decode_bcd(raw.day); }

	/**
	 * Day of the week (1 to 7), a value of one corresponds to Monday.
	 */
	uint8_t day_of_week() const { return raw.day_of_week; }

	/**
	 * Current month (1 to 12), one is January.
	 */
	uint8_t month() const { return decode_bcd(raw.month); }

	/**
	 * Current year. Assumes we are in the 21th century.
	 */
	uint16_t year() const { return decode_bcd(raw.year) + 2000; }
};
#pragma pack(pop)

/**
 * The DCF77 decoder class allows to decode the DCF77 signal. It performs phase
 * recovery, input signal low-pass filtering with hysteresis and data
 * validation.
 */
class decoder {
public:
	/**
	 * Enum describing the state of the decoder.
	 */
	enum class state : int8_t {
		/**
	     * Currently, there is no result available.
	     */
		no_result = 0,

		/**
	     * A synchronisation bit has been received, but the data could not
	     * be validated.
	     */
		invalid_result = -1,

		/**
	     * Time and date have been received and are valid, but supplementary
	     * information is missing.
	     */
		has_time_and_date = 1,

		/**
	     * An entire dataset is available.
	     */
		has_complete = 2
	};

private:
	/**
	 * All measured time values may be smaller than the nominal value by this
	 * value (in milliseconds).
	 */
	static constexpr uint16_t SLACK = 50;

	/**
	 * Synchronisation time in milliseconds. The carrier has a high amplitude
	 * for this amount of time.
	 */
	static constexpr uint16_t SYNC_HIGH_TIME = 1800;

	/**
	 * Time for which the carrier has a low value when encoding a zero-bit.
	 */
	static constexpr uint16_t LOW_ZERO_TIME = 100;

	/**
	 * Time for which the carrier has a low value when encoding a one-bit.
	 */
	static constexpr uint16_t LOW_ONE_TIME = 200;

	/**
	 * Instance of the "debouncer" class used to software-filter the input
	 * signal.
	 */
	debounce m_debouncer;

	/**
	 * Timestamp at which the falling edge corresponding to the start of the
	 * first second in a minute was received. Only updated when valid data is
	 * received.
	 */
	uint16_t m_phase = 0;

	/**
	 * Last timestamp when the "sample" method was called.
	 */
	uint16_t m_last_t = 0;

	/**
	 * Working data register. Incomming bits are written to this variable.
	 */
	data m_data_new;

	/**
	 * Valid data store. Valid received transmissions are stored in this
	 * variable.
	 */
	data m_data_current;

	/**
	 * Current bit into which data is written.
	 */
	uint8_t m_state = 0;

public:
	/**
	 * Pushes a new input sample into the decoder.
	 *
	 * @param value is the current value of the DCF77 carrier amplitude. "True"
	 * corresponds to a high amplitude, "False" to a low amplitude. Depending
	 * on the receiving circuitry you may have to invert this signal.
	 * @param t is a monotonously increasing timestamp in milliseconds. This
	 * time stamp can for example be generated by a simple one-millisecond
	 * timer.
	 * @return the decoder state. If has_time_and_date or has_complete is
	 * returned, the time data can be read via get_data() and the information
	 * can be accessed using the get_phase() method.
	 */
	state sample(bool value, uint16_t t);

	/**
	 * Returns the timestamp at which the end of the last valid synchronisation
	 * pulse was received.
	 */
	uint16_t get_phase() const { return m_phase; }

	/**
	 * Returns a reference at the last validated time data.
	 */
	const data &get_data() const { return m_data_current; }
};
}
