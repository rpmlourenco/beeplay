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
#include "NumberParser.h"
#include "Platform.h"
#include "Plugin.h"
#include "Random.h"
#include "RAOPDefs.h"
#include "RTSPClient.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <iterator>
#include <map>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>
#include <openssl/evp.h>
#include <openssl/md5.h>
#include <openssl/rsa.h>
#include <Poco/Format.h>
#include <Poco/Mutex.h>
#include <Poco/StringTokenizer.h>
#include <Poco/Timespan.h>
#include <Poco/Net/NetException.h>


using Poco::FastMutex;
using Poco::StringTokenizer;
using Poco::Timespan;
using Poco::Net::Socket;
using Poco::Net::StreamSocket;
using Poco::Net::ConnectionResetException;


static const std::string ACTIVE_REMOTE_HEADER("Active-Remote");
static const std::string AUDIO_JACK_STATUS_HEADER("Audio-Jack-Status");
static const std::string AUDIO_LATENCY_HEADER("Audio-Latency");
static const std::string AUTHORIZATION_HEADER("Authorization");
static const std::string CHALLENGE_REQUEST_HEADER("Apple-Challenge");
static const std::string CHALLENGE_RESPONSE_HEADER("Apple-Response");
static const std::string CLIENT_INSTANCE_HEADER("Client-Instance");
static const std::string CONTENT_LENGTH_HEADER("Content-Length");
static const std::string CONTENT_TYPE_HEADER("Content-Type");
static const std::string CSEQ_HEADER("CSeq");
static const std::string DACP_ID_HEADER("DACP-ID");
static const std::string RANGE_HEADER("Range");
static const std::string RTP_INFO_HEADER("RTP-Info");
static const std::string SESSION_HEADER("Session");
static const std::string TRANSPORT_HEADER("Transport");
static const std::string USER_AGENT_HEADER("User-Agent");
static const std::string WWW_AUTHENTICATE_HEADER("WWW-Authenticate");


//------------------------------------------------------------------------------


class RTSPRequest
{
public:
	explicit RTSPRequest(const std::string& method) : _method(method) {}

	const std::string& method() const { return _method; }

	void setBody(const buffer_t& contentData, const std::string& contentType) {
		setHeader(CONTENT_LENGTH_HEADER, Poco::format("%z", contentData.size()));
		setHeader(CONTENT_TYPE_HEADER, contentType);
		_bodyData = contentData;
	}

	void setBody(const std::string& contentData, const std::string& contentType) {
		setHeader(CONTENT_LENGTH_HEADER, Poco::format("%u", contentData.length()));
		setHeader(CONTENT_TYPE_HEADER, contentType);
		_bodyText = contentData;
	}

	void setHeader(const std::string& name, const std::string& value) {
		_headers.insert(std::make_pair(name, value));
	}

	void build(buffer_t& requestData, std::string& requestText, const std::string& requestURI) {
		requestText.assign(Poco::format("%s %s RTSP/1.0\r\n", _method, requestURI));

		typedef std::map<std::string, std::string>::const_iterator headers_iterator;
		for (headers_iterator it = _headers.begin(); it != _headers.end(); ++it) {
			requestText.append(it->first + ": " + it->second + "\r\n");
		}

		requestText.append("\r\n");

		requestData.assign(requestText.begin(), requestText.end());

		if (!_bodyData.empty()) {
			requestData.insert(requestData.end(), _bodyData.begin(), _bodyData.end());

			struct to_printable {
				char operator ()(const byte_t b) {
					return (std::isprint(b) || std::isspace(b)) ? b : '*';
				}
			};

			// transform short request bodies to printable characters for output
			if (_bodyData.size() <= 1024) {
				std::transform(_bodyData.begin(), _bodyData.end(),
					std::back_inserter(requestText), to_printable());
				if (*(requestText.rbegin()) != '\n') requestText.push_back('\n');
			}
		}
		else if (!_bodyText.empty()) {
			requestData.insert(requestData.end(), _bodyText.begin(), _bodyText.end());

			requestText.append(_bodyText);
		}
	}

private:
	std::string _method;
	buffer_t _bodyData;  std::string _bodyText;
	std::map< std::string, std::string> _headers;
};


class RTSPResponse
{
public:
	explicit RTSPResponse(const std::string& responseText) {
		assert(!responseText.empty());

		std::string::size_type beg = 0, end;

		// parse response for protocol
		end = responseText.find_first_of("/", beg);
		if (end <= beg || end == std::string::npos) {
			throw std::invalid_argument("responseText");
		}
		_protocol = responseText.substr(beg, end - beg);
		beg = responseText.find_first_not_of("/", end);

		// parse response for version
		end = responseText.find_first_of(" ", beg);
		if (end <= beg || end == std::string::npos) {
			throw std::invalid_argument("responseText");
		}
		_version = responseText.substr(beg, end - beg);
		beg = responseText.find_first_not_of(" ", end);

		// parse response for status code
		end = responseText.find_first_of(" ", beg);
		if (end <= beg || end == std::string::npos) {
			throw std::invalid_argument("responseText");
		}
		_statusCode = NumberParser::parseDecimalIntegerTo<int>(
			responseText.substr(beg, end - beg));
		beg = responseText.find_first_not_of(" ", end);

		// parse response for status text
		end = responseText.find_first_of("\r\n", beg);
		if (end <= beg || end == std::string::npos) {
			throw std::invalid_argument("responseText");
		}
		_statusText = responseText.substr(beg, end - beg);
		beg = responseText.find_first_not_of("\r\n", end);

		// parse response headers
		while (beg != std::string::npos
			&& end <= responseText.length() - 4
			&& responseText.compare(end, 4, "\r\n\r\n"))
		{
			// tokenize header into name and value
			end = responseText.find_first_of(":", beg);
			const std::string name = responseText.substr(beg, end - beg);
			beg = responseText.find_first_not_of(": ", end);

			end = responseText.find_first_of("\r\n", beg);
			const std::string value = responseText.substr(beg, end - beg);
			beg = end + 2;

			_headers.insert(std::make_pair(name, value));
		}

		const std::string::size_type contentLength = responseText.length() - (end + 4);
		if (contentLength > 0)
		{
			assert(hasHeader(CONTENT_LENGTH_HEADER));
			assert(contentLength == NumberParser::parseDecimalIntegerTo<
				std::string::size_type>(getHeader(CONTENT_LENGTH_HEADER)));

			body = responseText.substr(end + 4);
		}
	}

	int statusCode() const { return _statusCode; }
	bool hasHeader(const std::string& name) const { return !!_headers.count(name); }
	const std::string& getHeader(const std::string& name) { return _headers[name]; }

	std::string body;
private:
	int _statusCode;  std::string _statusText;
	std::string _protocol;  std::string _version;
	std::map< std::string, std::string> _headers;
};


class RTSPClientImpl : private Uncopyable
{
	friend class RTSPClient;

	 RTSPClientImpl(StreamSocket&, const uint32_t& remoteControlId);
	~RTSPClientImpl();

	RTSPResponse sendRequestReceiveResponse(RTSPRequest&);
	void sendRequest(const buffer_t&);
	std::string receiveResponse();

	void parseAuthenticateHeader(const std::string&);
	std::string buildAuthorizationHeader(
		const std::string& requestMethod, const std::string& requestURI) const;

private:
	FastMutex       _rtspMutex;
	StreamSocket    _rtspSocket;

	bool            _teardownRequired;
	uint32_t        _messageSequenceNumber;
	uint32_t        _localSessionId;
	std::string     _remoteSessionId;
	const uint32_t& _remoteControlId;

	int             _authenticationCasing;
	std::string     _authenticationMethod;
	std::string     _authenticationRealm;
	std::string     _authenticationNonce;
	std::string     _authenticationPassword;
};


//------------------------------------------------------------------------------


RTSPClient::RTSPClient(StreamSocket& socket, const uint32_t& remoteControlId)
:
	_impl(new RTSPClientImpl(socket, remoteControlId))
{
}


RTSPClient::~RTSPClient()
{
	delete _impl;
}


bool RTSPClient::isReady() const
{
	bool isReady = false;

	try
	{
		if (!_impl->_rtspSocket.poll(0, Socket::SELECT_ERROR))
		{
			if (_impl->_rtspSocket.poll(0, Socket::SELECT_READ))
			{
				int buffer;
				const int result = const_cast<RTSPClient*>(this)->_impl->
					_rtspSocket.receiveBytes(&buffer, sizeof(buffer), MSG_PEEK);
				isReady = (result > 0);
			}
			else
			{
				// no data to read, but connection is active and error-free
				isReady = true;
			}
		}
	}
	catch (const ConnectionResetException&)
	{
	}
	CATCH_ALL

	return isReady;
}


void RTSPClient::setPassword(const std::string& password)
{
	_impl->_authenticationPassword = password;
}


/**
 * Sends RTSP OPTIONS message.
 *
 * @param rsaKey optional RSA encryption key
 * @return response status code (positive) or protocol error code (negative)
 */
int RTSPClient::doOptions(void* const rsaKey)
{
	RTSPRequest request("OPTIONS");
	buffer_t challengeNonce(16);

	if (rsaKey)
	{
		// generate random challenge nonce
		Random::fill(&challengeNonce[0], challengeNonce.size());

		// base64 encode challenge nonce
		std::vector<char> encodedChallengeNonce(32);
		const int encodedChallengeNonceLength = EVP_EncodeBlock(
			(unsigned char*) &encodedChallengeNonce[0],
			(unsigned char*) &challengeNonce[0],
			(int) challengeNonce.size());
		if (encodedChallengeNonceLength <= 0
			|| encodedChallengeNonceLength > (int) encodedChallengeNonce.size())
		{
			throw std::runtime_error("EVP_EncodeBlock failed");
		}
		encodedChallengeNonce.resize(encodedChallengeNonceLength);

		// remove padding from base64-encoded challenge nonce
		while (encodedChallengeNonce.size() > 0
			&& encodedChallengeNonce.back() == '=')
		{
			encodedChallengeNonce.pop_back();
		}

		request.setHeader(CHALLENGE_REQUEST_HEADER,
			std::string(&encodedChallengeNonce[0], encodedChallengeNonce.size()));
	}

	RTSPResponse response(_impl->sendRequestReceiveResponse(request));

	if (response.statusCode() != RTSP_STATUS_CODE_OK
		// allow unauthorized condition to flow through so challenge is checked
		&& response.statusCode() != RTSP_STATUS_CODE_UNAUTHORIZED)
	{
		return response.statusCode();
	}

	if (rsaKey)
	{
		if (!response.hasHeader(CHALLENGE_RESPONSE_HEADER))
		{
			return -200000;
		}
		std::string encodedResponseNonce(response.getHeader(CHALLENGE_RESPONSE_HEADER));

		// add padding to base64-encoded string as necessary
		while (encodedResponseNonce.length() % 4 != 0)
		{
			encodedResponseNonce.push_back('=');
		}

		// base64 decode RSA-encrypted response nonce
		buffer_t encryptedResponseNonce(
			buffer_t::size_type(encodedResponseNonce.length() * 0.75));
		const int encryptedResponseNonceLength = EVP_DecodeBlock(
			(unsigned char*) &encryptedResponseNonce[0],
			(unsigned char*) encodedResponseNonce.c_str(),
			(int) encodedResponseNonce.length());
		if (encryptedResponseNonceLength <= 0
			|| encryptedResponseNonceLength > (int) encryptedResponseNonce.size())
		{
			throw std::runtime_error("EVP_DecodeBlock failed");
		}

		assert(RSA_size((RSA*) rsaKey) == 256);

		// remove padding from RSA-encrypted response nonce
		while (encryptedResponseNonce.size() > 256
			&& encryptedResponseNonce.back() == 0)
		{
			encryptedResponseNonce.pop_back();
		}

		// recheck length of RSA-encrypted response nonce
		if (encryptedResponseNonce.size() != 256)
		{
			return -200001;
		}

		// RSA decrypt response nonce
		buffer_t responseNonce(256);
		const int responseNonceLength = RSA_public_decrypt(
			(int) encryptedResponseNonce.size(),
			(unsigned char*) &encryptedResponseNonce[0],
			(unsigned char*) &responseNonce[0],
			(RSA*) rsaKey,
			RSA_PKCS1_PADDING);
		if (responseNonceLength <= 0
			|| responseNonceLength > (int) responseNonce.size())
		{
			return -200002;
		}

		if (responseNonceLength < (int) challengeNonce.size())
		{
			return -200003;
		}

		// compare response nonce to challenge nonce; they should match if
		// remote speakers posseses private counterpart to RSA public key used
		if (std::memcmp(&responseNonce[0], &challengeNonce[0], challengeNonce.size()) != 0)
		{
			return -200004;
		}
	}

	return response.statusCode();
}


/**
 * Sends RTSP ANNOUNCE message.
 *
 * @param aesKey AES encryption key
 * @param aesIV AES initialization vector
 * @return response status code (positive)
 */
int RTSPClient::doAnnounce(const std::string& aesKey, const std::string& aesIV)
{
	// generate local session identifier
	Random::fill(&_impl->_localSessionId, sizeof(uint32_t));

	std::string requestBody(Poco::format(
		"v=0\r\n"
		"o=iTunes %u 0 IN IP4 %s\r\n"
		"s=iTunes\r\n"
		"c=IN IP4 %s\r\n"
		"t=0 0\r\n",
		_impl->_localSessionId,
		_impl->_rtspSocket.address().host().toString(),
		_impl->_rtspSocket.peerAddress().host().toString()));

	requestBody.append(Poco::format(
		"m=audio 0 RTP/AVP 96\r\n"
		"a=rtpmap:96 AppleLossless\r\n"
		"a=fmtp:96 %u 0 %u 40 10 14 %u 255 0 0 %u\r\n",
		RAOP_PACKET_MAX_SAMPLES_PER_CHANNEL,
		RAOP_BITS_PER_SAMPLE,
		RAOP_CHANNEL_COUNT,
		RAOP_SAMPLES_PER_SECOND));

	if (!aesKey.empty() && !aesIV.empty())
	{
		requestBody.append(Poco::format(
			"a=rsaaeskey:%s\r\n"
			"a=aesiv:%s\r\n",
			aesKey, aesIV));
	}

	RTSPRequest request("ANNOUNCE");
	request.setBody(requestBody, "application/sdp");
	RTSPResponse response(_impl->sendRequestReceiveResponse(request));

	return response.statusCode();
}


/**
 * Sends RTSP SETUP message.
 *
 * @param serverPort [out] RTP server port
 * @param controlPort [out] RTP control port
 * @param timingPort [out] RTP timing port
 * @param audioLatency [out] remote speakers audio latency (in number of samples)
 * @param audioJackStatus [out] status of remote speakers audio jack
 * @return response status code (positive) or protocol error code (negative)
 */
int RTSPClient::doSetup(
	uint16_t& serverPort, uint16_t& controlPort, uint16_t& timingPort,
	unsigned int& audioLatency, AudioJackStatus& audioJackStatus)
{
	RTSPRequest request("SETUP");
	request.setHeader(TRANSPORT_HEADER,
		Poco::format("RTP/AVP/UDP;unicast;interleaved=0-1;mode=record;"
			"control_port=%hu;timing_port=%hu", controlPort, timingPort));

	RTSPResponse response(_impl->sendRequestReceiveResponse(request));

	if (response.statusCode() != RTSP_STATUS_CODE_OK)
	{
		return response.statusCode();
	}

	// check for session header
	if (!response.hasHeader(SESSION_HEADER))
	{
		return -200100;
	}
	const std::string& sessionHeader(response.getHeader(SESSION_HEADER));

	// validate session header
	if (sessionHeader.empty())
	{
		return -200101;
	}
	_impl->_remoteSessionId = sessionHeader;

	// check for transport header
	if (!response.hasHeader(TRANSPORT_HEADER))
	{
		return -200102;
	}
	const std::string& transportHeader(response.getHeader(TRANSPORT_HEADER));

	// parse transport header
	std::string::size_type beg = 0;
	while (beg != std::string::npos)
	{
		std::string::size_type mid = transportHeader.find_first_of('=', beg);
		std::string::size_type end = transportHeader.find_first_of(';', beg);

		if (mid != std::string::npos && mid < end)
		{
			const std::string key(transportHeader.substr(beg, mid - beg));
			++mid; // advance past '='
			const std::string val(transportHeader.substr(mid, end - mid));

			if (key == "server_port")
			{
				serverPort = NumberParser::parseDecimalIntegerTo<uint16_t>(val);
			}
			else if (key == "control_port")
			{
				controlPort = NumberParser::parseDecimalIntegerTo<uint16_t>(val);
			}
			else if (key == "timing_port")
			{
				timingPort = NumberParser::parseDecimalIntegerTo<uint16_t>(val);
			}
		}

		beg = transportHeader.find_first_not_of(';', end);
	}

	// check for audio latency header
	if (response.hasHeader(AUDIO_LATENCY_HEADER))
	{
		const std::string& audioLatencyHeader(response.getHeader(AUDIO_LATENCY_HEADER));

		// parse audio latency header
		audioLatency = NumberParser::parseDecimalIntegerTo<unsigned int>(audioLatencyHeader);
	}

	// check for audio jack status header
	if (response.hasHeader(AUDIO_JACK_STATUS_HEADER))
	{
		const std::string& audioJackStatusHeader(response.getHeader(AUDIO_JACK_STATUS_HEADER));

		// parse audio jack status header
		audioJackStatus = (audioJackStatusHeader == "disconnected" ? AUDIO_JACK_DISCONNECTED : AUDIO_JACK_CONNECTED);
	}

	return response.statusCode();
}


/**
 * Sends RTSP RECORD message.
 *
 * @param rtpSeqNum RTP sequence number of next packet
 * @param rtpTime RTP stream time for start of next packet
 * @param audioLatency [out] remote speakers audio latency (in number of samples)
 * @return response status code (positive)
 */
int RTSPClient::doRecord(
	const uint16_t rtpSeqNum, const uint32_t rtpTime, unsigned int& audioLatency)
{
	RTSPRequest request("RECORD");
	request.setHeader(RANGE_HEADER, "npt=0-");
	request.setHeader(RTP_INFO_HEADER,
		Poco::format("seq=%hu;rtptime=%u", rtpSeqNum, rtpTime));

	RTSPResponse response(_impl->sendRequestReceiveResponse(request));

	if (response.statusCode() != RTSP_STATUS_CODE_OK)
	{
		return response.statusCode();
	}

	// check for audio latency header
	if (response.hasHeader(AUDIO_LATENCY_HEADER))
	{
		const std::string& audioLatencyHeader(response.getHeader(AUDIO_LATENCY_HEADER));

		// parse audio latency header
		audioLatency = NumberParser::parseDecimalIntegerTo<unsigned int>(audioLatencyHeader);

		Debugger::printf("DoRecord latency = %i", audioLatency);
	}

	// indicate that teardown is required on disconnect because record message
	// was received successfully and a session is now fully established
	_impl->_teardownRequired = true;

	return response.statusCode();
}


/**
 * Sends RTSP FLUSH message.
 *
 * @param rtpSeqNum RTP sequence number of next packet
 * @param rtpTime RTP stream time for start of next packet
 * @return response status code (positive)
 */
int RTSPClient::doFlush(const uint16_t rtpSeqNum, const uint32_t rtpTime)
{
	RTSPRequest request("FLUSH");
	request.setHeader(RTP_INFO_HEADER,
		Poco::format("seq=%hu;rtptime=%u", rtpSeqNum, rtpTime));

	RTSPResponse response(_impl->sendRequestReceiveResponse(request));

	return response.statusCode();
}


/**
 * Sends RTSP TEARDOWN message.
 *
 * @return response status code (positive)
 */
int RTSPClient::doTeardown()
{
	if (!_impl->_teardownRequired)
	{
		return RTSP_STATUS_CODE_OK;
	}

	RTSPRequest request("TEARDOWN");
	RTSPResponse response(_impl->sendRequestReceiveResponse(request));

	_impl->_teardownRequired = false;

	return response.statusCode();
}


/**
 * Sends RTSP GET_PARAMETER message.
 *
 * @param parameterName
 * @param parameterValue [out]
 * @return response status code (positive)
 */
int RTSPClient::doGetParameter(
	const std::string& parameterName, std::string& parameterValue)
{
	parameterValue.clear();
	assert(!parameterName.empty());

	RTSPRequest request("GET_PARAMETER");
	request.setBody(parameterName + "\r\n", "text/parameters");
	RTSPResponse response(_impl->sendRequestReceiveResponse(request));

	if (response.statusCode() == RTSP_STATUS_CODE_OK
		&& response.getHeader(CONTENT_TYPE_HEADER) == "text/parameters")
	{
		parameterValue = StringTokenizer(response.body, ":", StringTokenizer::TOK_TRIM)[1];
	}

	return response.statusCode();
}


/**
 * Sends RTSP SET_PARAMETER message.
 *
 * @param parameterName
 * @param parameterValue
 * @return response status code (positive)
 */
int RTSPClient::doSetParameter(
	const std::string& parameterName, const std::string& parameterValue)
{
	assert(!parameterName.empty());
	assert(!parameterValue.empty());

	const std::string requestBody(
		Poco::format("%s: %s\r\n", parameterName, parameterValue));

	RTSPRequest request("SET_PARAMETER");
	request.setBody(requestBody, "text/parameters");

	RTSPResponse response(_impl->sendRequestReceiveResponse(request));

	return response.statusCode();
}


/**
 * Sends RTSP SET_PARAMETER message.
 *
 * @param contentType parameter content type
 * @param requestBody parameter content data
 * @param rtpTime RTP stream time for server to apply parameter change
 * @return response status code (positive)
 */
int RTSPClient::doSetParameter(
	const std::string& contentType, const buffer_t& requestBody, const uint32_t rtpTime)
{
	assert(!contentType.empty());

	RTSPRequest request("SET_PARAMETER");
	request.setBody(requestBody, contentType);
	request.setHeader(RTP_INFO_HEADER, Poco::format("rtptime=%u", rtpTime));

	RTSPResponse response(_impl->sendRequestReceiveResponse(request));

	return response.statusCode();
}


//------------------------------------------------------------------------------


RTSPClientImpl::RTSPClientImpl(StreamSocket& rtspSocket, const uint32_t& remoteControlId)
:
	_teardownRequired(false),
	_authenticationCasing(0),
	_messageSequenceNumber(0),
	_localSessionId(0),
	_remoteSessionId(),
	_remoteControlId(remoteControlId),
	_rtspSocket(rtspSocket)
{
	_rtspSocket.setBlocking(true);
	_rtspSocket.setKeepAlive(true);
	_rtspSocket.setLinger(true, 1);
	_rtspSocket.setNoDelay(true);

	_rtspSocket.setSendTimeout(Timespan(10, 0));
	_rtspSocket.setReceiveTimeout(Timespan(10, 0));
}


RTSPClientImpl::~RTSPClientImpl()
{
	try
	{
		_rtspSocket.close();
	}
	CATCH_ALL
}


RTSPResponse RTSPClientImpl::sendRequestReceiveResponse(RTSPRequest& request)
{
	FastMutex::ScopedLock lock(_rtspMutex); // serialize request-response pairs

	const std::string requestURI(_localSessionId == 0 ? "*" : Poco::format(
		"rtsp://%s/%u", _rtspSocket.address().host().toString(), _localSessionId));

	request.setHeader(USER_AGENT_HEADER, Plugin::userAgent());
	request.setHeader(CSEQ_HEADER, Poco::format("%u", _messageSequenceNumber + 1));
	request.setHeader(ACTIVE_REMOTE_HEADER, Poco::format("%u", _remoteControlId));
	request.setHeader(CLIENT_INSTANCE_HEADER, Poco::format("%016?X", Plugin::dacpId()));
	request.setHeader(DACP_ID_HEADER, Poco::format("%016?X", Plugin::dacpId()));
	if (!_remoteSessionId.empty()) request.setHeader(SESSION_HEADER, _remoteSessionId);
	if (!_authenticationMethod.empty()) request.setHeader(AUTHORIZATION_HEADER,
		buildAuthorizationHeader(request.method(), requestURI));

	buffer_t requestData;  std::string requestText;
	request.build(requestData, requestText, requestURI);

	Debugger::print(requestText + std::string(80, '-'));
	sendRequest(requestData);

	const std::string responseText(receiveResponse());
	Debugger::print(responseText + std::string(80, '-'));

	// increment sequence number after successful reception of response
	_messageSequenceNumber += 1;

	RTSPResponse response(responseText);

	// validate response sequence number
	if (response.hasHeader(CSEQ_HEADER))
	{
		const std::string& cSeqHeader(response.getHeader(CSEQ_HEADER));
		assert(_messageSequenceNumber == NumberParser::parseDecimalIntegerTo<uint32_t>(cSeqHeader));
	}

	// check response for authenticate header
	if (response.hasHeader(WWW_AUTHENTICATE_HEADER))
	{
		parseAuthenticateHeader(response.getHeader(WWW_AUTHENTICATE_HEADER));
	}

	return response;
}


void RTSPClientImpl::sendRequest(const buffer_t& requestData)
{
	assert(!requestData.empty());

	const byte_t* const requestBuffer = &requestData[0];
	const size_t requestLength = requestData.size();

	size_t bytesSent = 0;
	while (bytesSent < requestLength)
	{
		const int returnCode = _rtspSocket.sendBytes(
			requestBuffer + bytesSent, requestLength - bytesSent);
		if (returnCode <= 0)
		{
			throw std::runtime_error(
				Poco::format("_rtspSocket.sendBytes returned %i", returnCode));
		}

		bytesSent += static_cast<size_t>(returnCode);
	}
	if (bytesSent != requestLength)
	{
		throw std::runtime_error("bytesSent != requestLength");
	}
}


std::string RTSPClientImpl::receiveResponse()
{
	std::vector<char> responseBuffer(512);
	size_t responseLength = 0;
	int32_t lastFour = 0;
	char octet;
	int code;

	do
	{
		if (responseBuffer.size() == responseLength)
			responseBuffer.resize(responseBuffer.size() * 2);

		if ((code = _rtspSocket.receiveBytes(&octet, 1)) <= 0)
		{
			throw std::runtime_error(
				Poco::format("_rtspSocket.receiveBytes returned %i", code));
		}

		lastFour = (lastFour << 8) | octet;
		responseBuffer[responseLength] = octet;
		responseLength += static_cast<size_t>(code);
	}
	while (lastFour != 0x0D0A0D0A); // read until "\r\n\r\n"

	// read response body if content length is provided
	std::tr1::match_results<std::vector<char>::iterator> mr;
	if (std::tr1::regex_search(responseBuffer.begin(), responseBuffer.end(),
							   mr, std::tr1::regex("\\bContent-Length:\\s*(\\d+)\r\n")))
	{
		const size_t contentLength = NumberParser::parseDecimalIntegerTo<size_t>(mr[1]);

		if (contentLength > responseBuffer.size() - responseLength) responseBuffer.resize(responseLength + contentLength);
		code = _rtspSocket.receiveBytes(&responseBuffer[responseLength], contentLength);
		assert(static_cast<size_t>(code) == contentLength);
		responseLength += contentLength;
	}

	assert(responseBuffer.size() >= responseLength);
	return std::string(&responseBuffer[0], responseLength);
}


void RTSPClientImpl::parseAuthenticateHeader(const std::string& authenticateHeader)
{
	assert(!authenticateHeader.empty());

	std::string::size_type beg = 0, end = authenticateHeader.find_first_of(" ", beg);

	if (end <= beg || end == std::string::npos)
		throw std::runtime_error("end <= beg || end == std::string::npos");

	_authenticationMethod = authenticateHeader.substr(beg, end - beg);

	if (_authenticationMethod != "Digest")
		throw std::runtime_error("_authenticationMethod != \"Digest\"");

	beg = authenticateHeader.find_first_not_of(" ", end);

	while (beg != std::string::npos)
	{
		assert(end != std::string::npos);

		end = authenticateHeader.find_first_of("=\"", beg);
		const std::string name(authenticateHeader.substr(beg, end - beg));
		beg = authenticateHeader.find_first_not_of("=\"", end);

		end = authenticateHeader.find_first_of("\"", beg);
		const std::string value(authenticateHeader.substr(beg, end - beg));
		beg = authenticateHeader.find_first_not_of("\", ", end);

		if (name == "realm")
		{
			assert(!value.empty());
			_authenticationRealm = value;
		}
		else if (name == "nonce")
		{
			assert(!value.empty());
			_authenticationNonce = value;
		}
	}

	if (_authenticationRealm.empty() || _authenticationNonce.empty())
		throw std::runtime_error("_authenticationRealm.empty() || _authenticationNonce.empty()");

	// check for any uppercase hex digits in nonce so proper casing can be used during authorization
	_authenticationCasing = (_authenticationNonce.find_first_of("ABCDEF") != std::string::npos ? 1 : -1);
}


#define HEX(bytes, length, string, capacity, casing)                           \
{                                                                              \
	const char* format = (casing < 0 ? "%.2x" : casing > 0 ? "%.2X" : NULL);   \
	for (size_t i = 0; i < length; ++i)                                        \
		sprintf_s(string + 2 * i, capacity - 2 * i, format, bytes[i]);         \
}


std::string RTSPClientImpl::buildAuthorizationHeader(
	const std::string& requestMethod, const std::string& requestURI) const
{
	int length;
	char temp[256];
	byte_t ha1Digest[MD5_DIGEST_LENGTH];
	char ha1String[MD5_DIGEST_LENGTH * 2 + 1];
	byte_t ha2Digest[MD5_DIGEST_LENGTH];
	char ha2String[MD5_DIGEST_LENGTH * 2 + 1];
	byte_t responseDigest[MD5_DIGEST_LENGTH];
	char responseString[MD5_DIGEST_LENGTH * 2 + 1];

	assert(!requestMethod.empty());
	assert(!requestURI.empty());

	// this function assumes RFC 2617 digest authentication
	assert(_authenticationMethod == "Digest");

	// concatenate username, realm and password
	length = sprintf_s(temp, sizeof(temp), "iTunes:%s:%s",
		_authenticationRealm.c_str(), _authenticationPassword.c_str());
	assert(length > 0 && length < sizeof(temp));

	// create HA1 digest
	MD5((unsigned char*) temp, length, ha1Digest);
	HEX(ha1Digest, sizeof(ha1Digest), ha1String, sizeof(ha1String),
		_authenticationCasing);

	// concatenate request method and URI
	length = sprintf_s(temp, sizeof(temp), "%s:%s",
		requestMethod.c_str(), requestURI.c_str());
	assert(length > 0 && length < sizeof(temp));

	// create HA2 digest
	MD5((unsigned char*) temp, length, ha2Digest);
	HEX(ha2Digest, sizeof(ha2Digest), ha2String, sizeof(ha2String),
		_authenticationCasing);

	// concatenate HA1 digest, nonce and HA2 digest
	length = sprintf_s(temp, sizeof(temp), "%s:%s:%s",
		ha1String, _authenticationNonce.c_str(), ha2String);
	assert(length > 0 && length < sizeof(temp));

	// create response digest
	MD5((unsigned char*) temp, length, responseDigest);
	HEX(responseDigest, sizeof(responseDigest), responseString,
		sizeof(responseString), _authenticationCasing);

	// build request authorization header
	length = sprintf_s(temp, sizeof(temp),
		"%s username=\"iTunes\", realm=\"%s\", "
		"nonce=\"%s\", uri=\"%s\", response=\"%s\"",
		_authenticationMethod.c_str(), _authenticationRealm.c_str(),
		_authenticationNonce.c_str(), requestURI.c_str(), responseString);
	assert(length > 0 && length < sizeof(temp));

	return temp;
}
