/* Copyright (c) 2014  Eric Milles <eric.milles@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "NTPTimestamp.h"
#include "Platform.h"
#include <cassert>
#include <limits>


using Poco::Timestamp;


static const int64_t MICROSECONDS_PER_SECOND = 1000000;
static const int64_t SECONDS_FROM_1900_TO_1970 = 0x83AA7E80;
//RML 2017 migration
//static const int64_t UINT32_MAX = std::numeric_limits<uint32_t>::max();


NTPTimestamp& NTPTimestamp::operator =(const Timestamp& timestamp)
{
	int64_t microseconds = timestamp.epochMicroseconds();

	// calculate NTP timestamp seconds
	int64_t seconds = microseconds / MICROSECONDS_PER_SECOND;
	// NTP epoch is Jan 1, 1900; Timestamp epoch is Jan 1, 1970
	seconds += SECONDS_FROM_1900_TO_1970;
	assert(seconds <= UINT32_MAX);

	// calculate NTP timestamp fractional seconds
	microseconds %= MICROSECONDS_PER_SECOND;
	int64_t fractionalSeconds =
		(microseconds * (UINT32_MAX + 1)) / MICROSECONDS_PER_SECOND;
	assert(fractionalSeconds <= UINT32_MAX);

	this->seconds = static_cast<uint32_t>(seconds);
	this->fractionalSeconds = static_cast<uint32_t>(fractionalSeconds);

	return *this;
}


NTPTimestamp::operator Timestamp() const
{
	// NTP epoch is Jan 1, 1900; Timestamp epoch is Jan 1, 1970
	const int64_t seconds = this->seconds - SECONDS_FROM_1900_TO_1970;

	int64_t microseconds = seconds * MICROSECONDS_PER_SECOND;
	microseconds += (fractionalSeconds * MICROSECONDS_PER_SECOND) / (UINT32_MAX + 1);

	return Timestamp(microseconds);
}
