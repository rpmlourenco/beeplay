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

#ifndef DeviceInfo_h
#define DeviceInfo_h


#include <set>
#include <string>
#include <utility>


class DeviceInfo
{
public:
	enum DeviceType
	{
		APX = 1, // AirPort Express
		ATV = 2, // Apple TV
		AVR = 3, // Audio-Video Receiver
		AFS = 4, // Airfoil Speakers
		AS3 = 5, // AirServer Classic
		AS4 = 6, // AirServer
		ANY = 99 // Other
	};

	typedef std::string DeviceName;
	typedef std::pair<std::string,std::string> DeviceAddr;

	DeviceInfo(
		const DeviceType& type,
		const DeviceName& name,
		const DeviceAddr& addr,
		bool zeroConf);

	const DeviceType& type() const { return _type; }
	const DeviceName& name() const { return _name; }
	const DeviceAddr& addr() const { return _addr; }
	bool isZeroConf() const { return _zeroConf; }

private:
	DeviceType _type;
	DeviceName _name;
	DeviceAddr _addr;
	bool _zeroConf;
};


bool operator ==(const DeviceInfo&, const DeviceInfo&);


//------------------------------------------------------------------------------


struct DeviceInfoNameLess
{
	bool operator ()(const DeviceInfo& lhs, const DeviceInfo& rhs) const
	{
		return (lhs.name() < rhs.name());
	}
};

typedef std::set<const DeviceInfo,DeviceInfoNameLess> DeviceInfoSet;


#endif // DeviceInfo_h
