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

#include "Debugger.h"
#include "Platform.h"
//#include "Random.h"
#include "WASAPIDevice.h"
#include "..\raop\RAOPEngine.h"
#include "..\raop\RTSPClient.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <string>
#include <Poco/ByteOrder.h>
#include <Poco/Format.h>


using Poco::ByteOrder;
using Poco::Net::IPAddress;
using Poco::Net::SocketAddress;
using Poco::Net::StreamSocket;


static void dmapAppend(buffer_t& buf, const char* key, const void* val, const uint32_t len)
{
	assert(std::strlen(key) == 4);

	buffer_t::size_type idx = buf.size();

	// increase capacity of buffer to accommodate key, value length and value
	buf.resize(idx + 8 + len);

	std::memcpy(&buf[idx], key, 4);
	idx += 4;

	const uint32_t length = ByteOrder::toNetwork(len);
	std::memcpy(&buf[idx], &length, 4);
	idx += 4;

	if (len > 0)
	{
		std::memcpy(&buf[idx], val, len);
		idx += len;
	}

	assert(idx == buf.size());
}

template <typename integer_t>
inline void dmapAppend(buffer_t& buf, const char* key, integer_t val)
{
	val = ByteOrder::toNetwork(val);
	dmapAppend(buf, key, &val, sizeof(integer_t));
}

template <>
inline void dmapAppend(buffer_t& buf, const char* key, const int8_t val)
{
	dmapAppend(buf, key, &val, 1);
}

template <>
inline void dmapAppend(buffer_t& buf, const char* key, const uint8_t val)
{
	dmapAppend(buf, key, &val, 1);
}


//------------------------------------------------------------------------------


WASAPIDevice::WASAPIDevice(RAOPEngine& raopEngine,
	const int encryptionType, const byte_t metadataFlags)
:
	_encryptionType(encryptionType),
	_metadataFlags(metadataFlags),
	_raopEngine(raopEngine),
	_deviceVolume(0),
	_audioLatency(0),
	amiopen(false)
{
	// generate DACP remote control identifier
	// Random::fill(&_remoteControlId, sizeof(uint32_t));
}


WASAPIDevice::~WASAPIDevice()
{
	try
	{
		close();
	}
	CATCH_ALL
}


/**
 * Tests connection with remote speakers on specified socket.
 *
 * @param firstTime <code>true</code> if first initiation for remote speakers
 * @return zero on success; non-zero on failure
 */
int WASAPIDevice::test(StreamSocket& socket, const bool firstTime)
{
	/*
	if (_rtspClient.get() == NULL || !_rtspClient->isReady())
	{
		_rtspClient.reset(new RTSPClient(socket, _remoteControlId));
	}

	// send options message to remote speakers
	int returnCode = _rtspClient->doOptions(
		secureDataStream() && firstTime ? _raopEngine.rsaKey() : NULL);
	if (returnCode != RTSP_STATUS_CODE_OK)
	{
		return returnCode;
	}
	*/

	return 0;
}

/**
 * Opens session with remote speakers on specified socket.
 *
 * @param audioJackStatus [out] status of remote speakers audio jack
 * @return zero on success; non-zero on failure
 */
int WASAPIDevice::open(StreamSocket& socket, AudioJackStatus& audioJackStatus)
{
	int result = 0;

	Debugger::printf("Opening WASAPI device");

	HRESULT hr;
	REFERENCE_TIME hnsRequestedDuration = REFTIMES_PER_SEC;
	//REFERENCE_TIME hnsActualDuration;
	IMMDeviceEnumerator *pEnumerator = NULL;
	IMMDevice *pDevice = NULL;
	WAVEFORMATEX *pwfx = NULL;
	//UINT32 bufferFrameCount;
	//UINT32 numFramesAvailable;
	//UINT32 numFramesPadding;
	//BYTE *pData;
	//DWORD flags = 0;

	hr = CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, IID_IMMDeviceEnumerator, (void**)&pEnumerator);
	EXIT_ON_ERROR(hr)

	hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &pDevice);
	EXIT_ON_ERROR(hr)

	hr = pDevice->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&pAudioClient);
	EXIT_ON_ERROR(hr)

	hr = pAudioClient->GetMixFormat(&pwfx);
	EXIT_ON_ERROR(hr)

	hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, hnsRequestedDuration, 0, pwfx, NULL);
	EXIT_ON_ERROR(hr)

	framesize = pwfx->nBlockAlign;

	// Tell the audio source which format to use.
	//hr = pMySource->SetFormat(pwfx);
	//EXIT_ON_ERROR(hr)

	// Get the actual size of the allocated buffer.
	hr = pAudioClient->GetBufferSize(&bufferFrameSize);
	EXIT_ON_ERROR(hr)

	hr = pAudioClient->GetService(IID_IAudioRenderClient, (void**)&pRenderClient);
	EXIT_ON_ERROR(hr)

	// Grab the entire buffer for the initial fill operation.
	//hr = pRenderClient->GetBuffer(bufferFrameCount, &pData);
	//EXIT_ON_ERROR(hr)

	// Load the initial data into the shared buffer.
	//hr = pMySource->LoadData(bufferFrameCount, pData, &flags);
	//EXIT_ON_ERROR(hr)

	//hr = pRenderClient->ReleaseBuffer(bufferFrameCount, flags);
	//EXIT_ON_ERROR(hr)

	/*
	const IPAddress remoteHost = socket.peerAddress().host();

	if (_rtspClient.get() == NULL || !_rtspClient->isReady())
	{
		_rtspClient.reset(new RTSPClient(socket, _remoteControlId));
	}

	// send announce message to remote speakers
	int returnCode = _rtspClient->doAnnounce(
		secureDataStream() ? _raopEngine._encodedKey : "",
		secureDataStream() ? _raopEngine._encodedIV : "");
	if (returnCode != RTSP_STATUS_CODE_OK)
	{
		return returnCode;
	}

	uint16_t serverPort = 0;
	uint16_t controlPort = _raopEngine.controlPort();
	uint16_t timingPort = _raopEngine.timingPort();

	// send setup message to remote speakers
	returnCode = _rtspClient->doSetup(
		serverPort, controlPort, timingPort, _audioLatency, audioJackStatus);
	if (returnCode != RTSP_STATUS_CODE_OK)
	{
		return returnCode;
	}

	_audioSocketAddr = SocketAddress(remoteHost, serverPort);
	_controlSocketAddr = SocketAddress(remoteHost, controlPort);
	_timingSocketAddr = SocketAddress(remoteHost, timingPort);

	// send record message to remote speakers
	returnCode = _rtspClient->doRecord(
		_raopEngine._rtpSeqNumOutgoing, _raopEngine._rtpTimeOutgoing, _audioLatency);
	if (returnCode != RTSP_STATUS_CODE_OK)
	{
		return returnCode;
	}
	*/
	_raopEngine.attach(this);
	
	amiopen = true;
	return result;

Exit:
	CoTaskMemFree(pwfx);
	SAFE_RELEASE(pEnumerator)
	SAFE_RELEASE(pDevice)
	SAFE_RELEASE(pAudioClient)
	SAFE_RELEASE(pRenderClient)

	return hr;
}


void WASAPIDevice::close()
{
	_raopEngine.detach(this);

	amiopen = false;
	/*
	_audioLatency = 0;
	_audioSocketAddr = _controlSocketAddr = _timingSocketAddr = SocketAddress();

	// ensure RTSP client is destroyed even if detach or teardown throw
	std::auto_ptr<RTSPClient> rtspClient(_rtspClient);

	_raopEngine.detach(this);

	if (rtspClient.get() != NULL && rtspClient->isReady())
	{
		// send teardown message to remote speakers
		rtspClient->doTeardown();
	}
	*/
}


bool WASAPIDevice::isOpen(const bool pollConnection) const
{
	/*
	return (_rtspClient.get() != NULL && (!pollConnection || _rtspClient->isReady()));
	*/
	return amiopen;
}


void WASAPIDevice::setPassword(const std::string& password)
{
	/*
	_rtspClient->setPassword(password);
	*/
}


float WASAPIDevice::getVolume()
{
	Debugger::printf("[%s] device volume: %f", __FUNCTION__, _deviceVolume);

	/*
	try
	{
		assert(_rtspClient.get() != NULL && _rtspClient->isReady());  std::string vol;
		if (_rtspClient->doGetParameter("volume", vol) == RTSP_STATUS_CODE_OK)
		{
			const float volume = (float) std::atof(vol.c_str());
			_deviceVolume = std::min(std::max(volume, -100.0f), 0.0f);
		}
	}
	CATCH_ALL

	return _deviceVolume;
	*/
	return 0;
}


void WASAPIDevice::putVolume(const float volume)
{
	Debugger::printf("[%s] old device volume: %f, new device volume: %f", __FUNCTION__, _deviceVolume, volume);

	/*
	_deviceVolume = std::min(std::max(volume, -100.0f), 0.0f);

	assert(_rtspClient.get() != NULL && _rtspClient->isReady());
	// reiterate volume to device; some wait for echo before changing levels
	_rtspClient->doSetParameter("volume", Poco::format("%hf", _deviceVolume));
	*/
}


void WASAPIDevice::setVolume(const float absolute, const float relative)
{
	/*
	Debugger::printf("[%s] device volume: %f, old master volume: %f, new master volume: %f", __FUNCTION__, _deviceVolume, absolute - relative, absolute);

	if ((_deviceVolume == 0.0f && relative == 0.0f)
		|| std::fabs(_deviceVolume - (absolute - relative)) < 0.1f)
	{
		_deviceVolume = absolute;
		assert(-100.0f <= _deviceVolume && _deviceVolume <= 0.0f);
	}
	else
	{
		_deviceVolume = std::min(std::max(float(_deviceVolume + relative), -100.0f), -9.0f);
	}

	float decibels(_deviceVolume);
	if (decibels > 0.0f) decibels = 0.0f;
	if (decibels <= -100.0f) decibels = -144.0f;

	assert(_rtspClient.get() != NULL && _rtspClient->isReady());
	_rtspClient->doSetParameter("volume", Poco::format("%hf", decibels));
	*/
}


void WASAPIDevice::flush()
{
	HRESULT hr;
	hr = pAudioClient->Reset();
	if (hr < 0) { Debugger::print("failed wasapi reset"); }
	/*
	assert(_rtspClient.get() != NULL && _rtspClient->isReady());
	_rtspClient->doFlush(_raopEngine._rtpSeqNumOutgoing, _raopEngine._rtpTimeOutgoing);
	*/
}


void WASAPIDevice::updateMetadata(const OutputMetadata& metadata)
{
	/*
	const uint32_t rtpTime = _raopEngine._rtpTimeIncoming;

	if (_metadataFlags & MD_TEXT)
	{
		const std::string& title = metadata.title();
		const std::string& album = metadata.album();
		const std::string& artist = metadata.artist();

		// append metadata strings to DMAP tag buffer
		buffer_t tags;
		dmapAppend(tags, "mikd", int8_t(2));
	//	dmapAppend(tags, "miid", uint32_t(0));
		dmapAppend(tags, "minm", &title[0], title.length());
		dmapAppend(tags, "asal", &album[0], album.length());
		dmapAppend(tags, "asar", &artist[0], artist.length());
		dmapAppend(tags, "asdk", int8_t(metadata.length() > 0 ? 0 : 1));
		dmapAppend(tags, "astn", metadata.playlistPos().first);
		dmapAppend(tags, "astc", metadata.playlistPos().second);

		// wrap tags in metadata list container
		buffer_t mlit;
		dmapAppend(mlit, "mlit", &tags[0], tags.size());

		assert(_rtspClient.get() != NULL && _rtspClient->isReady());
		_rtspClient->doSetParameter("application/x-dmap-tagged", mlit, rtpTime);
	}

	if (_metadataFlags & MD_IMAGE)
	{
		// check size characteristics to prevent transmitting a dangerous image
		bool tooBig = (metadata.artworkData().size() > 262144); // 256 KB
		if (!tooBig)
		{
			const shorts_t dims = metadata.artworkDims();
			if (dims.first > 1000 || dims.second > 1000) tooBig = true;
		}

		const buffer_t& imageData = (!tooBig ? metadata.artworkData() : buffer_t());
		const std::string& imageType = (!tooBig ? metadata.artworkType() : "image/none");

		assert(_rtspClient.get() != NULL && _rtspClient->isReady());
		_rtspClient->doSetParameter(imageType, imageData, rtpTime);
	}
	*/
}


void WASAPIDevice::updateProgress(const OutputInterval& interval)
{
	/*
	if (_metadataFlags & MD_PROGRESS)
	{
		const uint32_t beg = static_cast<uint32_t>(interval.first);
		const uint32_t end = static_cast<uint32_t>(interval.second);
		const uint32_t pos = _raopEngine._rtpTimeIncoming;

		assert(_rtspClient.get() != NULL && _rtspClient->isReady());
		_rtspClient->doSetParameter("progress", Poco::format("%u/%u/%u", beg, pos, end));
	}
	*/
}
