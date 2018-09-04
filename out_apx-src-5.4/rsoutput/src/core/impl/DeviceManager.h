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

#ifndef DeviceManager_h
#define DeviceManager_h


#include "Device.h"
#include "DeviceInfo.h"
#include "DeviceNotification.h"
#include "OutputFormat.h"
#include "OutputMetadata.h"
#include "OutputObserver.h"
#include "OutputSink.h"
#include "Platform.h"
#include "Player.h"
#include "Uncopyable.h"
#include <map>
#include <cfloat>
#include <string>
#include <Poco/Mutex.h>
#include <Poco/Observer.h>


class DeviceManager
:
	private Uncopyable
{
public:
	DeviceManager(Player&, OutputObserver&);
	~DeviceManager();

	void openDevices();
	void closeDevices();
	bool isAnyDeviceOpen(bool ping = true) const;

	float getVolume() const;
	void  setVolume(float);

	void clearMetadata();
	void setMetadata(const OutputMetadata&);
	void setOffset(time_t); // current offset within chapter/track (in milliseconds); requires metadata.length to be set

	const OutputFormat& outputFormat() const;
	OutputSink::SharedPtr outputSinkForDevices();

	Device::SharedPtr lookupDevice(uint32_t remoteControlId) const;
private:
	Device::SharedPtr createDevice(const DeviceInfo&);
	void destroyDevice(const DeviceInfo&);
	void openDevice(const DeviceInfo&);
	bool volumeSet() const;

	void onDeviceChanged(DeviceNotification*);

private:
	OutputSink::SharedPtr _deviceOutputSink;

	typedef std::map< std::string,Device::SharedPtr> DeviceMap;
	DeviceMap _devices; // must be declared after device output sink

	typedef Poco::Observer<DeviceManager,DeviceNotification> DeviceObserver;
	DeviceObserver _deviceObserver;

	OutputObserver& _outputObserver;

	// playing chapter/track info
	OutputInterval _outputInterval;
	OutputMetadata _outputMetadata;

	Player& _player;
	float _volume;

	mutable Poco::FastMutex _mutex;
	typedef const Poco::FastMutex::ScopedLock ScopedLock;
};


inline float DeviceManager::getVolume() const
{
	return _volume;
}


inline bool DeviceManager::volumeSet() const
{
	return _volume != FLT_MIN;
}


#endif // DeviceManager_h
