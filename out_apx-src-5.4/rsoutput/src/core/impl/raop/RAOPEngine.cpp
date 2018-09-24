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
#include "Random.h"
#include "RAOPDefs.h"
#include "RAOPDevice.h"
#include "RAOPEngine.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <ctime>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include <mswsock.h>
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <Poco/ByteOrder.h>
#include <Poco/Format.h>
#include <Poco/Net/IPAddress.h>


#define TARGET_OS_WIN32
#pragma warning(push)
#pragma warning(disable:4146)
#pragma warning(disable:4244)
#pragma warning(disable:4805)
#include <ag_dec.c>
#include <ag_enc.c>
#include <dp_enc.c>
#include <matrix_enc.c>
#include <ALACBitUtilities.c>
#include <EndianPortable.c>
#include <ALACEncoder.cpp>
#pragma warning(pop)
#undef TARGET_OS_WIN32


using Poco::ByteOrder;
using Poco::Thread;
using Poco::Timestamp;
using Poco::Net::DatagramSocket;
using Poco::Net::IPAddress;
using Poco::Net::SocketAddress;
using Poco::Net::ReadableNotification;


// default ports to use for RTP control and timing
static const uint16_t LOCAL_CONTROL_PORT = 6001;
static const uint16_t LOCAL_TIMING_PORT = 6002;

// buffer approximately 2 seconds of unsent and 4 seconds of sent packets
static const uint16_t PACKET_BUFFER_COUNT = 250;
static const uint16_t PACKET_MEMORY_COUNT = 500;

static const uint16_t WASAPI_BUFFER_COUNT = 2000;
static const uint16_t WASAPI_MEMORY_COUNT = 4000;


const unsigned int RAOP_PACKET_MAX_SAMPLES_PER_CHANNEL = 352;
const unsigned int RAOP_SAMPLES_PER_SECOND = 44100;
const unsigned int RAOP_BITS_PER_SAMPLE = 16;
const unsigned int RAOP_CHANNEL_COUNT = 2;

static const size_t RAOP_PACKET_MAX_DATA_SIZE =
	(RAOP_PACKET_MAX_SAMPLES_PER_CHANNEL * (RAOP_BITS_PER_SAMPLE / 8) * RAOP_CHANNEL_COUNT);

static const size_t RAOP_PACKET_MAX_SIZE =
	(RTP_DATA_HEADER_SIZE + RAOP_PACKET_MAX_DATA_SIZE + 80 /* <-- ALAC encoder headroom */);

const unsigned int WASAPI_BITS_PER_SAMPLE = 32;
static const size_t WASAPI_PACKET_MAX_DATA_SIZE =
	(RAOP_PACKET_MAX_SAMPLES_PER_CHANNEL * (WASAPI_BITS_PER_SAMPLE / 8) * RAOP_CHANNEL_COUNT);		

static inline AudioFormatDescription alac_in_format()
{
	AudioFormatDescription afd;

	afd.mFormatID = kALACFormatLinearPCM;
	afd.mFormatFlags = kALACFormatFlagIsSignedInteger | kALACFormatFlagIsPacked;
	afd.mSampleRate = RAOP_SAMPLES_PER_SECOND;
	afd.mBitsPerChannel = RAOP_BITS_PER_SAMPLE;
	afd.mChannelsPerFrame = RAOP_CHANNEL_COUNT;
	afd.mFramesPerPacket = 1;
	afd.mReserved = 0;

	afd.mBytesPerFrame = (afd.mBitsPerChannel / 8) * afd.mChannelsPerFrame;
	afd.mBytesPerPacket = afd.mBytesPerFrame * afd.mFramesPerPacket;

	return afd;
}
static inline AudioFormatDescription alac_out_format()
{
	AudioFormatDescription afd;

	afd.mFormatID = kALACFormatAppleLossless;
	afd.mFormatFlags = 1;
	afd.mSampleRate = RAOP_SAMPLES_PER_SECOND;
	afd.mBitsPerChannel = 0;
	afd.mChannelsPerFrame = RAOP_CHANNEL_COUNT;
	afd.mFramesPerPacket = RAOP_PACKET_MAX_SAMPLES_PER_CHANNEL;
	afd.mReserved = 0;

	afd.mBytesPerFrame = 0;
	afd.mBytesPerPacket = 0;

	return afd;
}
static const AudioFormatDescription ALAC_IN_FORMAT(alac_in_format());
static const AudioFormatDescription ALAC_OUT_FORMAT(alac_out_format());


//------------------------------------------------------------------------------


static void bindToNextAvailablePort(DatagramSocket& socket, uint16_t port)
{
	assert(port > 0);

	// ensure bind will provide exclusive access to the port when successful
	socket.setOption(SOL_SOCKET, SO_EXCLUSIVEADDRUSE, TRUE);

	bool done = false;
	do
	{
		try
		{
			socket.bind(SocketAddress(IPAddress::wildcard(), port));
			done = true;
		}
		catch (...)
		{
			port += 1;
		}
	}
	while (!done && port > 0);

	if (port == 0)
	{
		throw std::runtime_error("socket.bind failed");
	}
}


static void sendTo(DatagramSocket& socket, const SocketAddress& address,
	const void* const buffer, const size_t length)
{
	assert(buffer != NULL && length > 0);

	const int returnCode = socket.sendTo(buffer, length, address);

	if (returnCode < 0 || static_cast<size_t>(returnCode) != length)
	{
		throw std::runtime_error("socket.sendTo failed");
	}
}


//------------------------------------------------------------------------------


const OutputFormat& RAOPEngine::outputFormat()
{
	static const OutputFormat format(SampleRate(RAOP_SAMPLES_PER_SECOND),
		SampleSize(RAOP_BITS_PER_SAMPLE / 8), ChannelCount(RAOP_CHANNEL_COUNT));
	return format;
}


int64_t RAOPEngine::samplesToMicroseconds(const int64_t samples)
{
	static const int64_t MICROSECONDS_PER_SECOND = 1000000;

	return ((samples * MICROSECONDS_PER_SECOND) / RAOP_SAMPLES_PER_SECOND);
}


int32_t RAOPEngine::samplesToMilliseconds(const int64_t samples)
{
	static const int64_t MILLISECONDS_PER_SECOND = 1000;

	return static_cast<int32_t>((samples * MILLISECONDS_PER_SECOND) / RAOP_SAMPLES_PER_SECOND);
}


//------------------------------------------------------------------------------


RAOPEngine::RAOPEngine(OutputObserver& outputObserver)
:
	_aesIV(16),
	_audioLatency(11025),
	_outputObserver(outputObserver),
	_rtpDataSecured(RAOP_PACKET_MAX_SIZE, PACKET_BUFFER_COUNT, PACKET_MEMORY_COUNT),
	_rtpDataUnsecured(RAOP_PACKET_MAX_SIZE, PACKET_BUFFER_COUNT, PACKET_MEMORY_COUNT),
	_wasapiData(WASAPI_PACKET_MAX_DATA_SIZE, WASAPI_BUFFER_COUNT, WASAPI_MEMORY_COUNT),
	_controlRequestHandler(*this, &RAOPEngine::handleControlRequest),
	_timingRequestHandler(*this, &RAOPEngine::handleTimingRequest),
	_reactorThread("RAOPEngine.SocketReactor::run"),
	_senderThread("RAOPEngine::run")
{
	// seed random number generator
	Random::seed(static_cast<unsigned int>(std::time(NULL)));

	// Apple AirPort Express RSA public key modulus
	const std::string modulus(
		"59dE8qLieItsH1WgjrcFRKj6eUWqi+bGLOX1HL3U3GhC/j0Qg90u3sG/1CUtwC"
		"5vOYvfDmFI6oSFXi5ELabWJmT2dKHzBJKa3k9ok+8t9ucRqMd6DZHJ2YCCLlDR"
		"KSKv6kDqnw4UwPdpOMXziC/AMj3Z/lUVX1G7WSHCAWKf1zNS1eLvqr+boEjXuB"
		"OitnZ/bDzPHrTOZz0Dew0uowxf/+sG+NCK3eQJVxqcaJ/vEHKIVd2M+5qL71yJ"
		"Q+87X6oV3eaYvt3zWZYD6z5vYTcrtij2VZ9Zmni/UAaHqn9JdsBWLUEpVviYnh"
		"imNVvYFZeCXg/IdTQ+x4IRdiXNv5hEew==");

	// Apple AirPort Express RSA public key exponent
	const std::string exponent("AQAB");

	// base64 decode AirPort Express public key modulus and exponent
	buffer_t n(258);
	const int nLength = EVP_DecodeBlock(
		(unsigned char*) &n[0],
		(unsigned char*) modulus.c_str(),
		(int) modulus.length());
	if (nLength <= 0 || nLength > (int) n.size())
	{
		throw std::runtime_error("EVP_DecodeBlock failed");
	}
	while (n.size() > 0 && n.back() == 0)
	{
		n.pop_back();
	}

	buffer_t e(3);
	const int eLength = EVP_DecodeBlock(
		(unsigned char*) &e[0],
		(unsigned char*) exponent.c_str(),
		(int) exponent.length());
	if (eLength <= 0 || eLength > (int) e.size())
	{
		throw std::runtime_error("EVP_DecodeBlock failed");
	}
	while (e.size() > 0 && e.back() == 0)
	{
		e.pop_back();
	}

	// initialize RAOP RSA public key
	_rsaKey.reset(RSA_new(), RSA_free);
	if (_rsaKey.get() == NULL)
	{
		throw std::runtime_error("RSA_new failed");
	}

	// fill public key components
	_rsaKey->n = BN_bin2bn(&n[0], n.size(), _rsaKey->n);
	_rsaKey->e = BN_bin2bn(&e[0], e.size(), _rsaKey->e);

	// clear private key components
	_rsaKey->d = NULL;
	_rsaKey->p = NULL;
	_rsaKey->q = NULL;
	_rsaKey->dmp1 = NULL;
	_rsaKey->dmq1 = NULL;
	_rsaKey->iqmp = NULL;

	// reduce time to send by disabling blocking
	_controlSocket.setBlocking(false);
	_timingSocket.setBlocking(false);
	_dataSocket.setBlocking(false);

	// disable ICMP Port Unreachable error processing
	BOOL flg = FALSE;
	_controlSocket.impl()->ioctl(SIO_UDP_CONNRESET, &flg);
	_timingSocket.impl()->ioctl(SIO_UDP_CONNRESET, &flg);
	_dataSocket.impl()->ioctl(SIO_UDP_CONNRESET, &flg);

	// reduce packet loss by increasing send buffer sizes
	_controlSocket.setSendBufferSize(2 *_controlSocket.getSendBufferSize());
	_dataSocket.setSendBufferSize(8 *_dataSocket.getSendBufferSize());

	// enable processing of incoming control and timing messages
	bindToNextAvailablePort(_controlSocket, LOCAL_CONTROL_PORT);
	bindToNextAvailablePort(_timingSocket, LOCAL_TIMING_PORT);
	_socketReactor.addEventHandler(_controlSocket, _controlRequestHandler);
	_socketReactor.addEventHandler(_timingSocket, _timingRequestHandler);
	_reactorThread.start(_socketReactor);
}


RAOPEngine::~RAOPEngine()
{
	try
	{
		// stop sender thread
		stop();
	}
	CATCH_ALL

	try
	{
		// stop receiver thread
		_socketReactor.stop();
		_reactorThread.join();
	}
	CATCH_ALL
}


void RAOPEngine::reinit(OutputInterval& outputInterval)
{
	// stop sending data and sync packets
	stop();

	// test thread states
	assert(_reactorThread.isRunning());
	assert(!_senderThread.isRunning());

	// generate new AES encryption key
	buffer_t key(16);
	Random::fill(&key[0], key.size());

	if (AES_set_encrypt_key(&key[0], key.size() * 8, &_aesKey))
	{
		throw std::runtime_error("AES_set_encrypt_key failed");
	}

	// RSA encrypt AES key
	buffer_t encryptedKey(RSA_size(rsaKey()));
	const int encryptedKeyLength = RSA_public_encrypt(
		(int) key.size(),
		(unsigned char*) &key[0],
		(unsigned char*) &encryptedKey[0],
		rsaKey(), RSA_PKCS1_OAEP_PADDING);
	if (encryptedKeyLength <= 0 || encryptedKeyLength > (int) encryptedKey.size())
	{
		throw std::runtime_error("RSA_public_encrypt failed");
	}

	// base64 encode RSA-encrypted AES key
	std::vector<char> encodedKey(384);
	const int encodedKeyLength = EVP_EncodeBlock(
		(unsigned char*) &encodedKey[0],
		(unsigned char*) &encryptedKey[0],
		encryptedKeyLength);
	if (encodedKeyLength <= 0 || encodedKeyLength > (int) encodedKey.size())
	{
		throw std::runtime_error("EVP_EncodeBlock failed");
	}
	encodedKey.resize(encodedKeyLength);

	// remove padding from base64-encoded, RSA-encrypted AES key
	while (encodedKey.size() > 0 && encodedKey.back() == '=')
	{
		encodedKey.pop_back();
	}
	_encodedKey.assign(&encodedKey[0], encodedKey.size());

	// generate new AES initialization vector
	Random::fill(&_aesIV[0], _aesIV.size());

	// base64 encode AES initialization vector
	std::vector<char> encodedIV(32);
	const int encodedIVLength = EVP_EncodeBlock(
		(unsigned char*) &encodedIV[0],
		(unsigned char*) &_aesIV[0],
		(int) _aesIV.size());
	if (encodedIVLength <= 0 || encodedIVLength > (int) encodedIV.size())
	{
		throw std::runtime_error("EVP_EncodeBlock failed");
	}
	encodedIV.resize(encodedIVLength);

	// remove padding from base64-encoded AES initialization vector
	while (encodedIV.size() > 0 && encodedIV.back() == '=')
	{
		encodedIV.pop_back();
	}
	_encodedIV.assign(&encodedIV[0], encodedIV.size());

	// generate new starting RTP packet sequence number
	uint16_t rtpSeqNum;
	Random::fill(&rtpSeqNum, sizeof(uint16_t));
	_rtpSeqNumIncoming = _rtpSeqNumOutgoing = rtpSeqNum;

	// generate new starting RTP time
	uint32_t rtpTime;
	Random::fill(&rtpTime, sizeof(uint32_t));

	// incorporate time shift into RTP timestamps of output progress metadata
	if (outputInterval.first != outputInterval.second)
	{
		if (rtpTime > _rtpTimeIncoming)
		{
			const uint32_t delta = (rtpTime - _rtpTimeIncoming);
			outputInterval.first  += delta;
			outputInterval.second += delta;
		}
		else
		{
			const uint32_t delta = (_rtpTimeIncoming - rtpTime);
			outputInterval.first  -= delta;
			outputInterval.second -= delta;
		}
	}

	_rtpTimeIncoming = _rtpTimeOutgoing = rtpTime;

	// generate new RTP synchronization source identifier
	Random::fill(&_rtpSsrc, sizeof(uint32_t));

	// reinitialize remaining object state
	_firstDataTime = _lastClockSyncTime = _lastStreamSyncTime = 0;
	_isFirstDataPacket = _isFirstSyncPacket = _isFirstWasapiPacket = true;
	_rtpDataUnsecured.reset();
	_rtpDataSecured.reset();
	_wasapiData.reset();
	_raopDevices.clear();
	_samplesWritten = 0;

	_alacEncoder.reset(new ALACEncoder);
	_alacEncoder->SetFrameSize(ALAC_OUT_FORMAT.mFramesPerPacket);
	const int32_t result = _alacEncoder->InitializeEncoder(ALAC_OUT_FORMAT);
	assert(result == 0);
}


OutputInterval RAOPEngine::getOutputInterval(const time_t length, const time_t offset) const
{
	assert(length >= 0 && offset >= 0);

	// convert length and offset to RTP timestamps relative to incoming RTP time
	const uint64_t rtpTime = static_cast<uint64_t>(_rtpTimeIncoming);
	const uint64_t maxTime = static_cast<uint64_t>(std::numeric_limits<uint32_t>::max());

	const uint64_t lengthSamples =
		(length * static_cast<uint64_t>(RAOP_SAMPLES_PER_SECOND)) / 1000;
	const uint64_t offsetSamples =
		(offset * static_cast<uint64_t>(RAOP_SAMPLES_PER_SECOND)) / 1000;

	const uint32_t beg = static_cast<uint32_t>(
		(rtpTime - offsetSamples) % (maxTime + 1));
	const uint32_t end = static_cast<uint32_t>(
		(rtpTime + (lengthSamples - offsetSamples)) % (maxTime + 1));

	return OutputInterval(beg, end);
}


uint16_t RAOPEngine::controlPort() const
{
	return _controlSocket.address().port();
}


uint16_t RAOPEngine::timingPort() const
{
	return _timingSocket.address().port();
}


time_t RAOPEngine::latency(const OutputFormat& format) const
{
	if (!(format == outputFormat()))
	{
		throw std::logic_error("format != RAOPEngine::outputFormat()");
	}

	const time_t bufferLatency = samplesToMilliseconds(
		PACKET_BUFFER_COUNT * RAOP_PACKET_MAX_SAMPLES_PER_CHANNEL);
	const time_t deviceLatency = samplesToMilliseconds(_audioLatency);

	return (bufferLatency + deviceLatency);
}


size_t RAOPEngine::buffered() const
{
	return 0;
}


size_t RAOPEngine::canWrite() const
{
	ScopedLock lock(_mutex);

	return (!_raopDevices.empty() && _rtpDataSecured.canWrite() ? RAOP_PACKET_MAX_DATA_SIZE : 0);
}


void RAOPEngine::write(const byte_t* buffer, size_t length)
{
	if (buffer == NULL || length == 0 || length > RAOP_PACKET_MAX_DATA_SIZE)
	{
		throw std::invalid_argument(
			"buffer == NULL || length == 0 || length > RAOP_PACKET_MAX_DATA_SIZE");
	}

	ScopedLock lock(_mutex);

	PacketBuffer::Slot& sslotRef = _rtpDataSecured.nextAvailable();
	PacketBuffer::Slot& uslotRef = _rtpDataUnsecured.nextAvailable();
	WASAPIBuffer::Slot& wslotRef = _wasapiData.nextAvailable();
	sslotRef.originalSize = uslotRef.originalSize = wslotRef.originalSize = length;

	DataPacketHeader packetHeader;
	packetHeader.setMarker(_isFirstDataPacket);
	packetHeader.setPayloadType(PAYLOAD_TYPE_STREAM_DATA);
	packetHeader.seqNum = _rtpSeqNumIncoming;
	packetHeader.rtpTime = _rtpTimeIncoming;
	packetHeader.ssrc = _rtpSsrc;
	ByteOrder_toNetwork(packetHeader);

	std::memcpy(sslotRef.packetData, &packetHeader, RTP_DATA_HEADER_SIZE);
	std::memcpy(uslotRef.packetData, &packetHeader, RTP_DATA_HEADER_SIZE);
	byte_t* const securedPacketPtr = &sslotRef.packetData[RTP_DATA_HEADER_SIZE];
	byte_t* const unsecuredPacketPtr = &uslotRef.packetData[RTP_DATA_HEADER_SIZE];

	// fill in wasapi data
	std::vector<float> newbuffer(length);
	src_short_to_float_array((short*)buffer, newbuffer.data(), length);
	wslotRef.payloadSize = wslotRef.packetSize = length << 1;
	std::memcpy(wslotRef.packetData, newbuffer.data(), wslotRef.packetSize);


	std::tr1::shared_ptr<void> buf;
	if (length < RAOP_PACKET_MAX_DATA_SIZE)
	{
		Debugger::printf("Recovering from %i-byte audio segment by padding it with %i bytes (%.3f ms) of silence.",
			length, RAOP_PACKET_MAX_DATA_SIZE - length, samplesToMicroseconds((RAOP_PACKET_MAX_DATA_SIZE - length) / 4) * 0.001f);

		buf.reset(std::calloc(1, RAOP_PACKET_MAX_DATA_SIZE), std::free);
		std::memcpy(buf.get(), buffer, length);

		buffer = (const byte_t*) buf.get();
		length = RAOP_PACKET_MAX_DATA_SIZE;
	}

	// fill in unsecured packet payload with encoded audio data
	int32_t dataLength = length;
	_alacEncoder->Encode(ALAC_IN_FORMAT, ALAC_OUT_FORMAT, (byte_t*) buffer, unsecuredPacketPtr, &dataLength);
	assert(dataLength > 0 && dataLength <= (RAOP_PACKET_MAX_SIZE - RTP_DATA_HEADER_SIZE)); // check for overrun

	sslotRef.payloadSize = uslotRef.payloadSize = dataLength;
	sslotRef.packetSize = uslotRef.packetSize = RTP_DATA_HEADER_SIZE + dataLength;
	const size_t frameSize = (RAOP_CHANNEL_COUNT * (RAOP_BITS_PER_SAMPLE / 8));
	assert((length / frameSize) <= std::numeric_limits<uint16_t>::max());
	sslotRef.frameCount = uslotRef.frameCount = uint16_t(length / frameSize);

	// make copy of initialization vector because it gets modified
	buffer_t iv(_aesIV);

	// encrypt audio data into secured packet payload
	const size_t remainderLength = uslotRef.payloadSize % AES_BLOCK_SIZE;
	const size_t encryptLength = uslotRef.payloadSize - remainderLength;
	AES_cbc_encrypt(
		unsecuredPacketPtr,
		securedPacketPtr,
		encryptLength,
		&_aesKey, &iv[0], AES_ENCRYPT);
	std::memcpy(
		securedPacketPtr + encryptLength,
		unsecuredPacketPtr + encryptLength,
		remainderLength);

	// increment RTP packet sequence number
	_rtpSeqNumIncoming += 1;

	// increment RTP time (one tick for each frame)
	_rtpTimeIncoming += uslotRef.frameCount;

	if (_isFirstDataPacket)
	{
		_isFirstDataPacket = false;

		/*
		long _latency = 2000;
		_wasapiStarter = new Poco::Timer(_latency, 0);
		Poco::TimerCallback<RAOPEngine> callback(*this, &RAOPEngine::onTimer);
		_wasapiStarter->start(callback);
		Debugger::printf("Scheduled wasapi start with latency = %i", _latency);
		*/

		// start sending data and sync packets when first data is written
		start();
	}
}


	struct isClosedOrUnresponsive
	{
		bool operator ()(const RAOPDevice* const device)
		{
			return !device->isOpen();
		}
	};


void RAOPEngine::flush()
{
	ScopedLock lock(_mutex);

	// remove closed devices from the list
	_raopDevices.remove_if(isClosedOrUnresponsive());
}


void RAOPEngine::reset()
{
	// stop sending data and sync packets
	stop();

	// test thread states
	assert(_reactorThread.isRunning());
	assert(!_senderThread.isRunning());

	ScopedLock lock(_mutex);

	// try to flush each device's playback buffer
	for (RAOPDeviceList::const_iterator it = _raopDevices.begin();
		it != _raopDevices.end(); ++it)
	{
		RAOPDevice& raopDevice = **it;

		try
		{
			if (raopDevice.isOpen())
			{
				raopDevice.flush();
			}
		}
		CATCH_ALL
	}

	// remove closed devices from the list
	_raopDevices.remove_if(isClosedOrUnresponsive());

	if (_wasapiDevice != NULL) {
		_wasapiDevice->flush();
	}

	// reset remaining object state
	_firstDataTime = _lastClockSyncTime = _lastStreamSyncTime = 0;
	_isFirstDataPacket = _isFirstSyncPacket = _isFirstWasapiPacket = true;
	_rtpSeqNumIncoming = _rtpSeqNumOutgoing;
	_rtpTimeIncoming = _rtpTimeOutgoing;
	_rtpDataUnsecured.reset();
	_rtpDataSecured.reset();
	_wasapiData.reset();
	_samplesWritten = 0;
}


void RAOPEngine::attach(RAOPDevice* const raopDevice)
{
	ScopedLock lock(_mutex);

	RAOPDeviceList::const_iterator pos =
		std::find(_raopDevices.begin(), _raopDevices.end(), raopDevice);
	if (pos == _raopDevices.end())
	{
		_raopDevices.push_back(raopDevice);

		// force a sync packet to help synchronize devices
		_isFirstSyncPacket = true;
	}
}


void RAOPEngine::detach(RAOPDevice* const raopDevice)
{
	ScopedLockWithUnlock lock(_mutex);

	_raopDevices.remove(raopDevice);

	if (_raopDevices.empty())
	{
		lock.unlock(); // to prevent deadlock

		// stop sending data and sync packets when last device is removed
		stop();
	}
}

void RAOPEngine::attach(WASAPIDevice* const wasapiDevice)
{
	_wasapiDevice = wasapiDevice;
}


void RAOPEngine::detach(WASAPIDevice* const wasapiDevice)
{
	_wasapiDevice = NULL;

}


void RAOPEngine::start()
{
	_stopSending = false;
	_senderThread.start(*this);
	_senderThread.setOSPriority(THREAD_PRIORITY_ABOVE_NORMAL);
}


void RAOPEngine::stop()
{
	_stopSending = true;
	_senderThread.join();

	if (_wasapiDevice != NULL) {
		HRESULT hr;
		hr = _wasapiDevice->pAudioClient->Stop();
		if (hr < 0) { Debugger::print("failed stop wasapi device"); }
	}

}


void RAOPEngine::run()
{
	while (!_stopSending)
	{
		try
		{
			ScopedLockWithUnlock lock(_mutex);

			Timestamp currentTime;

			// send sync packet at start of stream and periodically afterwards
			if (_isFirstSyncPacket || (currentTime - _lastStreamSyncTime) >= 1000000L)
			{
				sendSyncPacket(currentTime);
			}

			// send data packet whenever system time meets or exceeds stream time
			if (!_raopDevices.empty() && _rtpSeqNumIncoming != _rtpSeqNumOutgoing
				&& (currentTime - _firstDataTime) >= samplesToMicroseconds(_samplesWritten))
			{
				const size_t dataLength = sendDataPacket(currentTime);

				lock.unlock();

				// notify observer of successful output
				_outputObserver.onBytesOutput(dataLength);
			}
			else
			{
				// no active devices or no data to send or it's too soon to send

				lock.unlock();

				Thread::sleep(1);
			}
		}
		CATCH_ALL
	}
}


size_t RAOPEngine::sendDataPacket(const Timestamp& currentTime)
{
	PacketBuffer::Slot& sslotRef = _rtpDataSecured.nextBuffered();
	PacketBuffer::Slot& uslotRef = _rtpDataUnsecured.nextBuffered();
	
	const DataPacketHeader& packetHeader =
		*reinterpret_cast<DataPacketHeader*>(sslotRef.packetData);

	if (_wasapiDevice != NULL) {
		HRESULT hr;
		UINT32 bufferFrameCount;
		int numFramesAvailable;
		UINT32 numFramesPadding;
		BYTE *pData;
		DWORD flags = 0;

		bufferFrameCount = uint16_t(_wasapiData.getSizeNextBuffered() / _wasapiDevice->framesize);

		// See how much buffer space is available.
		hr = _wasapiDevice->pAudioClient->GetCurrentPadding(&numFramesPadding);
		if (hr < 0) { Debugger::print("failed get wasapi current padding"); }

		numFramesAvailable = _wasapiDevice->bufferFrameSize - numFramesPadding;

		if (numFramesAvailable >= bufferFrameCount) {

			WASAPIBuffer::Slot& wslotRef = _wasapiData.nextBuffered();

			// Grab the entire buffer for the initial fill operation.
			hr = _wasapiDevice->pRenderClient->GetBuffer(bufferFrameCount, &pData);
			if (hr < 0) { Debugger::print("failed get wasapi buffer"); }

			std::memcpy(pData, wslotRef.packetData, wslotRef.packetSize);

			hr = _wasapiDevice->pRenderClient->ReleaseBuffer(bufferFrameCount, flags);
			if (hr < 0) { Debugger::print("failed release wasapi buffer"); }

		}

	}

	// send data packet to each device
	for (RAOPDeviceList::const_iterator it = _raopDevices.begin();
		it != _raopDevices.end(); ++it)
	{
		RAOPDevice& raopDevice = **it;

		try
		{
			if (raopDevice.isOpen())
			{
				sendTo(_dataSocket,
					raopDevice.audioSocketAddr(),
					raopDevice.secureDataStream()
						? sslotRef.packetData
						: uslotRef.packetData,
					sslotRef.packetSize);
			}
		}
		catch (const std::exception& ex)
		{
			Debugger::printException(ex, Poco::format(
				"Sending data packet %hu to %s",
				ByteOrder::fromNetwork(packetHeader.seqNum),
				raopDevice.audioSocketAddr().toString()));
		}
	}

	if (_wasapiDevice != NULL && _isFirstWasapiPacket)
	{
		_isFirstWasapiPacket = false;

		//REFERENCE_TIME phnsLatency;
		//_wasapiDevice->pAudioClient->GetStreamLatency(&phnsLatency);
		//Debugger::printf("buffer latency = %d", samplesToMilliseconds(PACKET_BUFFER_COUNT * RAOP_PACKET_MAX_SAMPLES_PER_CHANNEL));
		//Debugger::printf("device latency = %d", samplesToMilliseconds(_audioLatency));

		
		long _latency = 1953;
		_wasapiStarter = new Poco::Timer(_latency, 0);

		Poco::Timestamp _now;
		//const Poco::Timestamp::TimeDiff timediff = _now - currentTime;
		_measureTimestamp = _now;
		//Debugger::printf("Timing firstSend (wasapi schedule) = %8.3f ms ", static_cast<double>(timediff) / 1000.0);

		Poco::TimerCallback<RAOPEngine> callback(*this, &RAOPEngine::onTimer);
		_wasapiStarter->start(callback);
		Debugger::printf("Scheduled wasapi start with latency = %i", _latency);		

	}

	// check for indicator of first data packet in stream
	if (packetHeader.getMarker())
	{
		_firstDataTime = currentTime;
	}

	// update counters
	_rtpSeqNumOutgoing += 1;
	_rtpTimeOutgoing += sslotRef.frameCount;
	_samplesWritten += sslotRef.frameCount;

	return sslotRef.originalSize;
}

void RAOPEngine::onTimer(Poco::Timer& timer) {
	HRESULT hr;

	Poco::Timestamp _now;
	const Poco::Timestamp::TimeDiff timediff = _now - _measureTimestamp;
	_measureTimestamp = _now;
	Debugger::printf("Timing before wasapi start = %8.3f ms ", static_cast<double>(timediff) / 1000.0);

	REFERENCE_TIME phnsLatency;
	hr = _wasapiDevice->pAudioClient->GetStreamLatency(&phnsLatency);
	Debugger::printf("Wasapi latency = %i ms ", phnsLatency);

	hr = _wasapiDevice->pAudioClient->Start();
	if (hr < 0) { Debugger::print("failed start wasapi device"); }

}


void RAOPEngine::sendSyncPacket(const Timestamp& currentTime)
{
	SyncPacket syncPacket;
	syncPacket.setMarker();
	syncPacket.setExtension(_isFirstSyncPacket);
	syncPacket.setPayloadType(PAYLOAD_TYPE_STREAM_SYNC);
	syncPacket.seqNum = 7;
	syncPacket.ntpTime = currentTime;
	syncPacket.rtpTime = _rtpTimeOutgoing;
	syncPacket.rtpTimeLessLatency = (_rtpTimeOutgoing - 77175);
	ByteOrder_toNetwork(syncPacket);

	// send sync packet to each device
	for (RAOPDeviceList::const_iterator it = _raopDevices.begin();
		it != _raopDevices.end(); ++it)
	{
		RAOPDevice& raopDevice = **it;

		try
		{
			if (raopDevice.isOpen())
			{
				sendTo(_controlSocket,
					raopDevice.controlSocketAddr(),
					&syncPacket, RTP_SYNC_PACKET_SIZE);
			}
		}
		catch (const std::exception& ex)
		{
			Debugger::printException(ex, Poco::format(
				"Sending sync packet to %s",
				raopDevice.controlSocketAddr().toString()));
		}
	}

	_isFirstSyncPacket = false;
	_lastStreamSyncTime = currentTime;
}


void RAOPEngine::handleTimingRequest(ReadableNotification*)
{
	try
	{
		buffer_t buffer(64);
		SocketAddress sender;
		const int length = _timingSocket.receiveFrom(&buffer[0], buffer.size(), sender);

		if (length < RTP_BASE_HEADER_SIZE)
		{
			throw std::length_error("length < RTP_BASE_HEADER_SIZE");
		}

		RTPPacketHeader header;
		std::memcpy(&header, &buffer[0], RTP_BASE_HEADER_SIZE);

		if (header.getPayloadType() == PAYLOAD_TYPE_TIMING_REQUEST)
		{
			if (length != RTP_TIMING_PACKET_SIZE)
			{
				throw std::length_error("length != RTP_TIMING_PACKET_SIZE");
			}

			TimingPacket request;
			std::memcpy(&request, &buffer[0], RTP_TIMING_PACKET_SIZE);
			ByteOrder_fromNetwork(request);

			// capture current time for response
			Timestamp currentTime;

			TimingPacket response(request);
			response.setPayloadType(PAYLOAD_TYPE_TIMING_RESPONSE);
			response.referenceTime = request.sendTime;
			response.receivedTime = currentTime;
			response.sendTime = currentTime;
			ByteOrder_toNetwork(response);

			sendTo(_timingSocket, sender, &response, RTP_TIMING_PACKET_SIZE);

			// gather and examine timing metrics
			if (_lastClockSyncTime != 0)
			{
				const Timestamp::TimeDiff currentRecvLastRecvTimeDiff =
					currentTime - _lastClockSyncTime;
				const Timestamp::TimeDiff localRecvRemoteSendTimeDiff =
					currentTime - request.sendTime;

				Debugger::printf("!!!Timing request: "
					"time between requests = %8.3f ms; "
					"local recv time - remote send time = %7.3f ms.",
					static_cast<double>(currentRecvLastRecvTimeDiff) / 1000.0,
					static_cast<double>(localRecvRemoteSendTimeDiff) / 1000.0);

				if (_abs64(currentRecvLastRecvTimeDiff) > 3333000LL
					|| (_abs64(localRecvRemoteSendTimeDiff) > 250000LL
						&& _abs64(localRecvRemoteSendTimeDiff) < 10000000LL))
				{
					Debugger::printf("Timing request: "
						"time between requests = %8.3f ms; "
						"local recv time - remote send time = %7.3f ms.",
						static_cast<double>(currentRecvLastRecvTimeDiff) / 1000.0,
						static_cast<double>(localRecvRemoteSendTimeDiff) / 1000.0);
				}
			}
			_lastClockSyncTime = currentTime;
		}
		else
		{
			throw std::runtime_error(Poco::format(
				"Unhandled payload type: 0x%02?X", header.getPayloadType()));
		}
	}
	CATCH_ALL
}


void RAOPEngine::handleControlRequest(ReadableNotification*)
{
	try
	{
		buffer_t buffer(64);
		SocketAddress sender;
		const int length = _controlSocket.receiveFrom(&buffer[0], buffer.size(), sender);

		if (length < RTP_BASE_HEADER_SIZE)
		{
			throw std::length_error("length < RTP_BASE_HEADER_SIZE");
		}

		RTPPacketHeader header;
		std::memcpy(&header, &buffer[0], RTP_BASE_HEADER_SIZE);

		if (header.getPayloadType() == PAYLOAD_TYPE_RESEND_REQUEST)
		{
			if (length != RTP_RESEND_REQUEST_SIZE)
			{
				throw std::length_error("length != RTP_RESEND_REQUEST_SIZE");
			}

			ResendRequestPacket request;
			std::memcpy(&request, &buffer[0], RTP_RESEND_REQUEST_SIZE);
			ByteOrder_fromNetwork(request);

			handleResendRequest(request, sender);
		}
		else
		{
			throw std::runtime_error(Poco::format(
				"Unhandled payload type: 0x%02?X", header.getPayloadType()));
		}
	}
	CATCH_ALL
}


void RAOPEngine::handleResendRequest(ResendRequestPacket& request,
	const SocketAddress& requestorAddress)
{
	Debugger::printf(
		"Resend requested by %s for %hu packet(s) starting at sequence number %hu.",
		requestorAddress.toString().c_str(), request.missedPktCnt, request.missedSeqNum);

	ScopedLock lock(_mutex);

	uint16_t missedPktAge = (request.missedSeqNum <= _rtpSeqNumOutgoing
		? _rtpSeqNumOutgoing - request.missedSeqNum
		: _rtpSeqNumOutgoing + (std::numeric_limits<uint16_t>::max() - request.missedSeqNum) + 1);

	if (missedPktAge < 1 || missedPktAge > PACKET_MEMORY_COUNT)
	{
		Debugger::printf("Requested packet(s) too old to resend; "
			"only the last %hu sent packets are kept.", PACKET_MEMORY_COUNT);
		return;
	}

	// determine which device is the requestor
	RAOPDevice* requestor = NULL;
	for (RAOPDeviceList::const_iterator it = _raopDevices.begin();
		it != _raopDevices.end(); ++it)
	{
		const RAOPDevice& raopDevice = **it;

		if (raopDevice.controlSocketAddr().host() == requestorAddress.host()
			&& (raopDevice.controlSocketAddr().port() == requestorAddress.port()
				|| raopDevice.audioSocketAddr().port() == requestorAddress.port()
				|| raopDevice.audioSocketAddr().port()+1 == requestorAddress.port()))
		{
			requestor = *it;
			break;
		}
	}

	if (requestor == NULL)
	{
		Debugger::printf("Requestor %s not found in list of devices.",
			requestorAddress.toString().c_str());
		return;
	}
	else if (!requestor->isOpen())
	{
		Debugger::printf("Requestor %s no longer open for playback.",
			requestorAddress.toString().c_str());
		return;
	}

	// obtain reference to correct packet stream for requesting device
	const PacketBuffer& rtpData =
		(requestor->secureDataStream() ? _rtpDataSecured : _rtpDataUnsecured);

	RTPPacketHeader header;  header.setMarker();
	header.setPayloadType(PAYLOAD_TYPE_RESEND_RESPONSE);
	buffer_t response(RTP_BASE_HEADER_SIZE + RAOP_PACKET_MAX_SIZE);

	while (request.missedPktCnt > 0)
	{
		const PacketBuffer::Slot& slotRef = rtpData.prevBuffered(missedPktAge);

		const uint16_t dataPacketSeqNum = ByteOrder::fromNetwork(
			reinterpret_cast<const DataPacketHeader*>(slotRef.packetData)->seqNum);
		if (request.missedSeqNum != dataPacketSeqNum)
		{
			Debugger::printf("Data packet with sequence number %hu was not found"
				" at anticipated position in packet history; %hu was in its place.",
				request.missedSeqNum, dataPacketSeqNum);
			return;
		}

		// pass packet frame count, which may not be easy to determine from size
		header.seqNum = ByteOrder::toNetwork(slotRef.frameCount);

		std::memcpy(&response[0], &header, RTP_BASE_HEADER_SIZE);
		const size_t packetSize = std::min(slotRef.packetSize, RAOP_PACKET_MAX_SIZE);
		std::memcpy(&response[RTP_BASE_HEADER_SIZE], slotRef.packetData, packetSize);

		sendTo(_controlSocket, requestorAddress, &response[0], RTP_BASE_HEADER_SIZE + packetSize);

		// update loop counters
		        missedPktAge -= 1;
		request.missedPktCnt -= 1;
		request.missedSeqNum += 1;
	}
}
