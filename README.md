# libdcf77

Cross-platform C++ decoder for the German DCF77 longwave time signal radio station.
The library is intended to be used on a 8-bit microcontroller ‒ however, the code has
no dependency on any execution environment whatsoever. It can be used with any low cost
DCF77 receiver circuit, such as one of the following:

* [Pollin Module](http://www.pollin.de/shop/dt/NTQ5OTgxOTk-/)
* [Reichelt Module](https://www.reichelt.de/Bausaetze/DCF77-MODUL/3/index.html?ACTION=3&GROUPID=7836&ARTICLE=57772&OFFSET=16&)
* [Conrad Module](http://www.conrad.com/ce/en/product/641138/DCF-receiver-module-Compatible-with-C-Control)

Interfacing with the hardware is left as an exercise to the reader. Note that some of the
modules might require an external transistor to drive the microcontroller port pin.
Consult the datasheet of your receiver module for more info.

## Features

* Written in C++14
* Software low-pass filter and Schmitt-Trigger to denoise the signal from the receiver
* Signal validation
* Phase compensation
* Requires about 2kB program memory and 40 bytes of RAM

## Example

The following example implements a DCF77 receiver on an AVR microcontroller (here an ATMega32).
The reciever is connected to pin PD3. In this example case, the receiver input signal is inverted.
It outputs a "1" when the DCF77 carrier is dampened.

```cpp
#include <dcf77.hpp>

#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/sleep.h>

static volatile uint16_t t = 0;

ISR(TIMER1_COMPA_vect) { t++; }

int main()
{
	// Use Port D as input
	DDRD = 0x00;
	PORTD = 0x00;

	// Configure a millisecond timeer
	TCCR1B = (1 << WGM12) | (1 << CS10);
	OCR1A = F_CPU / 1000;
	TIMSK = (1 << OCIE1A);
	sei();

	// Receive and decode the data
	dcf77::decoder dcf77_dec;
	while (true) {
		const bool dcf77_signal = (PIND & (1 << 3));

		if (dcf77_dec.sample(!dcf77_signal, t) >=
		    dcf77::decoder::state::has_time_and_date) {
			auto &d = dcf77_dec.get_data();
			// Valid data has been received, do something with it
		}

		// Wait for the next millisecond
		set_sleep_mode(SLEEP_MODE_IDLE);
		sleep_mode();
	}
}
```

## License

libdcf77 — Cross Platform C++ DCF77 decoder

Copyright (C) 2016  Andreas Stöckel

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
