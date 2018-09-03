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

#ifndef RTSPClient_h
#define RTSPClient_h


#include "Platform.h"
#include "Uncopyable.h"
#include "impl/Device.h"
#include <string>
#include <Poco/Net/StreamSocket.h>


class RTSPClient
:
	private Uncopyable
{
public:
	 RTSPClient(Poco::Net::StreamSocket&, const uint32_t& remoteControlId);
	~RTSPClient();

	bool isReady() const;

	void setPassword(const std::string&);

	int doOptions(void* rsaKey);
	int doAnnounce(const std::string& aesKey, const std::string& aesIV);
	int doSetup(uint16_t& serverPort, uint16_t& controlPort, uint16_t& timingPort,
		unsigned int& audioLatency, AudioJackStatus&);
	int doRecord(uint16_t rtpSeqNum, uint32_t rtpTime, unsigned int& audioLatency);
	int doFlush(uint16_t rtpSeqNum, uint32_t rtpTime);
	int doTeardown();

	int doGetParameter(const std::string& key, std::string& val);
	int doSetParameter(const std::string& key, const std::string& val);
	int doSetParameter(const std::string& contentType, const buffer_t& requestBody,
		uint32_t rtpTime);

private:
	class RTSPClientImpl* const _impl;
};


enum {
	RTSP_STATUS_CODE_OK           = 200,
	RTSP_STATUS_CODE_UNAUTHORIZED = 401
};


#endif // RTSPClient_h
