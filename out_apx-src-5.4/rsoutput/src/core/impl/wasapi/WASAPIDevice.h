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

#ifndef WASAPIDevice_h
#define WASAPIDevice_h


#include "Platform.h"
#include "Uncopyable.h"
#include "impl/Device.h"
#include <memory>

#include "stdafx.h"

#include <functiondiscoverykeys.h>
#include "WASAPIRenderer.h"

//bool DisableMMCSS;

class WASAPIDevice
:
	public Device,
	private Uncopyable
{
public:
	enum {
		ET_NONE    = 0,
		ET_SECURED = 1, // RSA for handshake, AES for streaming
	};

	enum {
		MD_NONE     = 0x00,
		MD_TEXT     = 0x01,
		MD_IMAGE    = 0x02,
		MD_PROGRESS = 0x04,
	};

public:
	 WASAPIDevice(class RAOPEngine&, int encryptionType, byte_t metadataFlags);
	~WASAPIDevice();

	int test(Poco::Net::StreamSocket&, bool firstTime);
	int open(Poco::Net::StreamSocket&, AudioJackStatus&);
	bool isOpen(bool pollConnection = true) const;
	void close();
	void flush();

	float getVolume();
	void putVolume(float);
	void setVolume(float abs, float rel);
	void setPassword(const std::string&);
	void updateMetadata(const OutputMetadata&);
	void updateProgress(const OutputInterval&);

	unsigned int audioLatency() const;
	uint32_t remoteControlId() const;
	bool secureDataStream() const;

	const Poco::Net::SocketAddress& audioSocketAddr() const;
	const Poco::Net::SocketAddress& controlSocketAddr() const;
	const Poco::Net::SocketAddress& timingSocketAddr() const;

private:

	bool amiopen;

	IMMDevice *device = NULL;
	CWASAPIRenderer *renderer = NULL;
	bool isDefaultDevice;
	ERole role;

	int TargetFrequency = 440;
	int TargetLatency = 50;
	int TargetDurationInSec = 10;
	bool UseConsoleDevice = false;
	bool UseCommunicationsDevice = false;
	bool UseMultimediaDevice = true;	
	wchar_t *OutputEndpoint = NULL;

	bool WASAPIDevice::PickDevice(IMMDevice **DeviceToUse, bool *IsDefaultDevice, ERole *DefaultDeviceRole);

	              class RAOPEngine& _raopEngine;
	std::auto_ptr<class RTSPClient> _rtspClient;

	volatile float _deviceVolume;

	/** type of encryption to use on device's audio data stream */
	int _encryptionType;

	/** type(s) of playback metadata the device accepts */
	byte_t _metadataFlags;

	/** device's audio playback latency (in number of samples) */
	unsigned int _audioLatency;

	/** device's DACP remote control identifier */
	uint32_t _remoteControlId;

	/** device's RTP communication endpoints */
	Poco::Net::SocketAddress _audioSocketAddr;
	Poco::Net::SocketAddress _controlSocketAddr;
	Poco::Net::SocketAddress _timingSocketAddr;
};


inline unsigned int WASAPIDevice::audioLatency() const
{
	return _audioLatency;
}


inline uint32_t WASAPIDevice::remoteControlId() const
{
	return _remoteControlId;
}


inline bool WASAPIDevice::secureDataStream() const
{
	return (_encryptionType == ET_SECURED);
}


inline const Poco::Net::SocketAddress& WASAPIDevice::audioSocketAddr() const
{
	return _audioSocketAddr;
}


inline const Poco::Net::SocketAddress& WASAPIDevice::controlSocketAddr() const
{
	return _controlSocketAddr;
}


inline const Poco::Net::SocketAddress& WASAPIDevice::timingSocketAddr() const
{
	return _timingSocketAddr;
}


#endif // WASAPIDevice_h
