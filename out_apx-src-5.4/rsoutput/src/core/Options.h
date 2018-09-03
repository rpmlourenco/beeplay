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

#ifndef Options_h
#define Options_h


#include "DeviceInfo.h"
#include <map>
#include <set>
#include <string>
#include <utility>
#include <Poco/AbstractObserver.h>
#include <Poco/Notification.h>
#include <Poco/SharedPtr.h>


class Options
{
public:
	typedef Poco::SharedPtr<Options> SharedPtr;

	static SharedPtr getOptions();
	static void setOptions(SharedPtr);

	static void addObserver(const Poco::AbstractObserver&);
	static void removeObserver(const Poco::AbstractObserver&);
	static void postNotification(Poco::Notification::Ptr);

public:
	bool getVolumeControl() const;
	void setVolumeControl(bool);
	bool getPlayerControl() const;
	void setPlayerControl(bool);
	bool getResetOnPause() const;
	void setResetOnPause(bool);

	const DeviceInfoSet& devices() const;
	DeviceInfoSet& devices();

	bool isActivated(const std::string& deviceName) const;
	void setActivated(const std::string& deviceName, bool);
	const std::string& getPassword(const std::string& deviceName) const;
	bool getRememberPassword(const std::string& deviceName) const;
	void setPassword(const std::string& deviceName, const std::string& password,
		bool rememberPassword);
	void clearPassword(const std::string& deviceName);

private:
	bool _volumeControl;
	bool _playerControl;
	bool _resetOnPause;

	DeviceInfoSet _devices;
	std::set<const std::string> _activatedDevices;
	std::map<const std::string,const std::pair<const std::string,bool>> _devicePasswords;

	friend bool operator ==(const Options&, const Options&);
};


bool operator ==(const Options&, const Options&);


#endif // Options_h
