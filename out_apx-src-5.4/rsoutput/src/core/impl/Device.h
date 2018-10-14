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

#ifndef Device_h
#define Device_h


#include "OutputMetadata.h"
#include "Platform.h"
#include <string>
#include <utility>
#include <Poco/SharedPtr.h>
#include <Poco/Net/StreamSocket.h>


enum AudioJackStatus
{
	AUDIO_JACK_DISCONNECTED = 0,
	AUDIO_JACK_CONNECTED = 1
};


typedef std::pair<time_t,time_t> OutputInterval;


//------------------------------------------------------------------------------


class Device
{
public:
	typedef Poco::SharedPtr<Device> SharedPtr;

	virtual ~Device();

	virtual int test(Poco::Net::StreamSocket&, bool firstTime) = 0;
	virtual int open(Poco::Net::StreamSocket&, AudioJackStatus&, bool authRequest) = 0;
	virtual bool isOpen(bool pollConnection = true) const = 0;
	virtual void close() = 0;

	virtual float getVolume() = 0;
	virtual void putVolume(float) = 0;
	virtual void setVolume(float abs, float rel) = 0;
	virtual void setPassword(const std::string&) = 0;

	virtual void updateMetadata(const OutputMetadata&) = 0;
	virtual void updateProgress(const OutputInterval&) = 0;

	virtual uint32_t remoteControlId() const = 0;
};


inline Device::~Device()
{
}


#endif // Device_h
