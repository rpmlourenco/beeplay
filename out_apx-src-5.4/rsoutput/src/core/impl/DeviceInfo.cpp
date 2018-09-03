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

#include "DeviceInfo.h"


DeviceInfo::DeviceInfo(
	const DeviceType& type,
	const DeviceName& name,
	const DeviceAddr& addr,
	const bool zeroConf)
:
	_type(type),
	_name(name),
	_addr(addr),
	_zeroConf(zeroConf)
{
}


bool operator ==(const DeviceInfo& lhs, const DeviceInfo& rhs)
{
	return (lhs.type() == rhs.type()
		 && lhs.name() == rhs.name()
		 && lhs.addr() == rhs.addr()
		 && lhs.isZeroConf() == rhs.isZeroConf());
}
