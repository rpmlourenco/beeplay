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

#ifndef NTPTimestamp_h
#define NTPTimestamp_h


#include "Platform.h"
#include <Poco/ByteOrder.h>
#include <Poco/Timestamp.h>


struct NTPTimestamp
{
	uint32_t seconds;
	uint32_t fractionalSeconds;

	NTPTimestamp();
	NTPTimestamp(uint32_t secs, uint32_t frac);
	//NTPTimestamp(const NTPTimestamp&); auto-generated
	//NTPTimestamp& operator =(const NTPTimestamp&); auto-generated

	// provide for easy conversion between NTPTimestamp and Poco::Timestamp
	NTPTimestamp(const Poco::Timestamp&);
	NTPTimestamp& operator =(const Poco::Timestamp&);
	operator Poco::Timestamp() const;
};


inline NTPTimestamp::NTPTimestamp()
:
	seconds(0),
	fractionalSeconds(0)
{
}


inline NTPTimestamp::NTPTimestamp(const uint32_t secs, const uint32_t frac)
:
	seconds(secs),
	fractionalSeconds(frac)
{
}


inline NTPTimestamp::NTPTimestamp(const Poco::Timestamp& timestamp)
{
	*this = timestamp;
}


inline NTPTimestamp ByteOrder_fromNetwork(const NTPTimestamp& timestamp)
{
	return NTPTimestamp(
		Poco::ByteOrder::fromNetwork(timestamp.seconds),
		Poco::ByteOrder::fromNetwork(timestamp.fractionalSeconds));
}


inline NTPTimestamp ByteOrder_toNetwork(const NTPTimestamp& timestamp)
{
	return NTPTimestamp(
		Poco::ByteOrder::toNetwork(timestamp.seconds),
		Poco::ByteOrder::toNetwork(timestamp.fractionalSeconds));
}


#endif // NTPTimestamp_h
