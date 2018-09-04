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

#ifndef DeviceUtils_h
#define DeviceUtils_h


#include "Uncopyable.h"
#include <memory>
#include <string>
#include <vector>


class RSOUTPUT_API DeviceUtils
:
	private Uncopyable
{
public:
	struct Info {
		std::string display_name;
		bool activated; bool available;
	};
	typedef std::vector<Info> InfoList;
	//RML 2017 migration
	typedef std::shared_ptr<InfoList> InfoListPtr;

public:
	DeviceUtils();
	~DeviceUtils();

	InfoListPtr enumerate() const;
	void toggle(const std::string& which);

private:
	class DeviceUtilsImpl* const _impl;
};


#endif // DeviceUtils_h
