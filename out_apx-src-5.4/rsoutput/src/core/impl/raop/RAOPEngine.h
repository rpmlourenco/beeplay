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

#ifndef RAOPEngine_h
#define RAOPEngine_h


#include "OutputFormat.h"
#include "PacketBuffer.h"
#include "Platform.h"
#include "RAOPDefs.h"
#include "RAOPDevice.h"
#include "..\wasapi\WASAPIDevice.h"
#include "..\wasapi\WASAPIBuffer.h"
#include "Uncopyable.h"
#include "impl/OutputObserver.h"
#include "impl/OutputSink.h"
#include <list>
#include <memory>
#include <string>
#include <openssl/aes.h>
#include <openssl/rsa.h>
#include <Poco/Mutex.h>
#include <Poco/Runnable.h>
#include <Poco/ScopedLock.h>
#include <Poco/Thread.h>
#include <Poco/Timestamp.h>
#include <Poco/Timer.h>
#include <Poco/Net/DatagramSocket.h>
#include <Poco/Net/SocketAddress.h>
#include <Poco/Net/SocketNotification.h>
#include <Poco/Net/SocketReactor.h>
#include <samplerate.h>


class RAOPEngine
:
	public OutputSink,
	public Poco::Runnable,
	private Uncopyable
{
	friend RAOPDevice;
	friend WASAPIDevice;

public:
	static const OutputFormat& outputFormat();

private:
	static int64_t samplesToMicroseconds(int64_t);
	static int32_t samplesToMilliseconds(int64_t);

public:
	explicit RAOPEngine(OutputObserver&);
	~RAOPEngine();

	void reinit(OutputInterval&); // also recalibrates interval to new RTP time

	// returns interval for given length and offset relative to internal RTP time
	OutputInterval getOutputInterval(time_t length, time_t offset) const;

	uint16_t controlPort() const;
	uint16_t timingPort() const;

	time_t latency(const OutputFormat&) const;
	size_t buffered() const;
	size_t canWrite() const;

	void write(const byte_t*, size_t);
	void flush();
	void reset();

private:
	void attach(class RAOPDevice*);
	void detach(class RAOPDevice*);

	void attach(class WASAPIDevice*);
	void detach(class WASAPIDevice*);

	void start();
	void stop();
	void run();

	size_t sendDataPacket(const Poco::Timestamp&);
	void sendSyncPacket(const Poco::Timestamp&);
	void handleTimingRequest(Poco::Net::ReadableNotification*);
	void handleControlRequest(Poco::Net::ReadableNotification*);
	void handleResendRequest(ResendRequestPacket&, const Poco::Net::SocketAddress&);

private:
	/** RSA encryption public key */
	std::tr1::shared_ptr<RSA> _rsaKey;
	RSA* rsaKey() { return _rsaKey.get(); }

	/** AES encryption key (binary and base64-encoded) */
	AES_KEY _aesKey;
	std::string _encodedKey;

	/** AES encryption initialization vector (binary and base64-encoded) */
	buffer_t _aesIV;
	std::string _encodedIV;

	/** RTP audio latency (in number of samples per channel) */
	unsigned int _audioLatency;

	/** RTP audio data packets */
	PacketBuffer _rtpDataSecured;
	PacketBuffer _rtpDataUnsecured;
	WASAPIBuffer _wasapiData;

	/** RTP packet sequence number */
	uint16_t _rtpSeqNumIncoming;
	uint16_t _rtpSeqNumOutgoing;

	/** RTP time */
	uint32_t _rtpTimeIncoming;
	uint32_t _rtpTimeOutgoing;

	/** RTP synchronization source identifier */
	uint32_t _rtpSsrc;

	/** count of samples written to remote speakers in current audio stream */
	int64_t _samplesWritten;

	bool _isFirstDataPacket;
	bool _isFirstSyncPacket;
	bool _isFirstWasapiPacket;
	Poco::Timestamp _firstDataTime;
	Poco::Timestamp _lastClockSyncTime;
	Poco::Timestamp _lastStreamSyncTime;

	volatile bool _stopSending;
	Poco::Thread _senderThread;
	Poco::Thread _reactorThread;
	Poco::Net::SocketReactor _socketReactor;

	Poco::Observer<RAOPEngine,Poco::Net::ReadableNotification> _controlRequestHandler;
	Poco::Observer<RAOPEngine,Poco::Net::ReadableNotification> _timingRequestHandler;

	Poco::Net::DatagramSocket _controlSocket;
	Poco::Net::DatagramSocket _timingSocket;
	Poco::Net::DatagramSocket _dataSocket;

	OutputObserver& _outputObserver;

	std::auto_ptr<class ALACEncoder> _alacEncoder;

	typedef std::list<class RAOPDevice*> RAOPDeviceList;
	RAOPDeviceList _raopDevices;

	WASAPIDevice *_wasapiDevice = NULL;

	mutable Poco::FastMutex _mutex;
	typedef const Poco::FastMutex::ScopedLock ScopedLock;
	typedef Poco::ScopedLockWithUnlock<Poco::FastMutex> ScopedLockWithUnlock;

	Poco::Timer *_wasapiStarter = NULL;
	void onTimer(Poco::Timer& timer);

};

#endif // RAOPEngine_h
