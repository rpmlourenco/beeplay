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
#include "Plugin.h"
#include "RemoteControl.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <Poco/DateTimeFormat.h>
#include <Poco/DateTimeFormatter.h>
#include <Poco/Format.h>
#include <Poco/NumberParser.h>
#include <Poco/StringTokenizer.h>
#include <Poco/URI.h>
#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/StreamSocket.h>


using Poco::DateTime;
using Poco::DateTimeFormat;
using Poco::DateTimeFormatter;
using Poco::NumberParser;
using Poco::StringTokenizer;
using Poco::Thread;
using Poco::Timespan;
using Poco::URI;
using Poco::Net::ServerSocket;
using Poco::Net::Socket;
using Poco::Net::SocketAddress;
using Poco::Net::StreamSocket;


enum Command
{
	NONE,
	PLAY,
	PAUSE,
	STOP,
	RESTART,
	NEXT_TRACK,
	PREV_TRACK,
	VOLUME_UP,
	VOLUME_DOWN,
	TOGGLE_MUTE,
	TOGGLE_RNDM,
	SET_PROPERTY
};

struct Request
{
	Command command;
	std::map< std::string, std::string> params;
	std::map< std::string, std::string> headers;
};


static const uint16_t DACP_PORT = 3689;
static const std::string REMOTE_CONTROL_ID_PARAMETER("Active-Remote");


//------------------------------------------------------------------------------


static std::string receiveRequest(StreamSocket& socket)
{
	std::vector<char> requestBuffer(4096);
	int requestLength = 0;

	do
	{
		const int returnCode = socket.receiveBytes(
			&requestBuffer[requestLength], requestBuffer.size() - requestLength);
		if (returnCode <= 0)
		{
			throw std::runtime_error(Poco::format(
				"socket.receiveBytes returned %i", returnCode));
		}

		requestLength += returnCode;
	}
	while (std::strncmp(&requestBuffer[requestLength - 4], "\r\n\r\n", 4) != 0);

	assert(requestLength < requestBuffer.size());
	return std::string(&requestBuffer[0], requestLength);
}


static Request parseRequest(const std::string& requestText)
{
	Request request;

	const StringTokenizer requestLines(requestText, "\r\n", StringTokenizer::TOK_IGNORE_EMPTY);
	StringTokenizer::Iterator requestLine = requestLines.begin();
	if (requestLine == requestLines.end())
	{
		throw std::invalid_argument("requestText");
	}

	// first request line shoud be of the form: <method> <resource> <protocol>
	const StringTokenizer firstLineTokens(*requestLine, " ");
	if (firstLineTokens.count() != 3)
	{
		throw std::invalid_argument("requestText");
	}

	// check request method and protocol
	if (firstLineTokens[0] != "GET" && firstLineTokens[2] != "HTTP/1.1")
	{
		throw std::invalid_argument("requestText");
	}

	// parse resource identifier for command
	std::vector<std::string> resourcePath;
	URI resourceIdentifier(firstLineTokens[1]);
	resourceIdentifier.getPathSegments(resourcePath);

	request.command = NONE;
	if (resourcePath[0] == "ctrl-int")
	{
		if (resourcePath.size() != 3)
		{
			throw std::invalid_argument("requestText");
		}

		if (resourcePath[1] == "1")
		{
			if (resourcePath[2] == "play" || resourcePath[2] == "playpause")
			{
				request.command = PLAY;
			}
			else if (resourcePath[2] == "pause")
			{
				request.command = PAUSE;
			}
			else if (resourcePath[2] == "stop")
			{
				request.command = STOP;
			}
			else if (resourcePath[2] == "restartitem")
			{
				request.command = RESTART;
			}
			else if (resourcePath[2] == "nextitem")
			{
				request.command = NEXT_TRACK;
			}
			else if (resourcePath[2] == "previtem")
			{
				request.command = PREV_TRACK;
			}
			else if (resourcePath[2] == "volumeup")
			{
				request.command = VOLUME_UP;
			}
			else if (resourcePath[2] == "volumedown")
			{
				request.command = VOLUME_DOWN;
			}
			else if (resourcePath[2] == "mutetoggle")
			{
				request.command = TOGGLE_MUTE;
			}
			else if (resourcePath[2] == "shufflesongs")
			{
				request.command = TOGGLE_RNDM;
			}
			else if (resourcePath[2] == "setproperty")
			{
				request.command = SET_PROPERTY;
			}
		}
	}

	// parse resource identifier for params
	const StringTokenizer queryTokens(resourceIdentifier.getRawQuery(), "&");
	for (StringTokenizer::Iterator queryToken = queryTokens.begin();
		queryToken != queryTokens.end(); ++queryToken)
	{
		// tokenize param into name and value
		const StringTokenizer paramTokens(*queryToken, "=");

		std::string key, val;
		if (paramTokens.count() > 0) URI::decode(paramTokens[0], key);
		if (paramTokens.count() > 1) URI::decode(paramTokens[1], val);

		if (!key.empty()) request.params.insert(std::make_pair(key, val));
	}

	// parse request for headers
	while (++requestLine != requestLines.end())
	{
		// tokenize header into name and value
		const StringTokenizer headerTokens(*requestLine, ":");

		// skip space at beginning of value string
		request.headers.insert(
			std::make_pair(headerTokens[0], headerTokens[1].substr(1)));
	}

	return request;
}


static std::string buildResponse(const bool requestUnderstood)
{
	std::string responseText;

	responseText.append(Poco::format("HTTP/1.1 %s\r\n", std::string(requestUnderstood ? "204 No Content" : "501 Not Implemented")));
	responseText.append(Poco::format("Date: %s\r\n", DateTimeFormatter::format(DateTime(), DateTimeFormat::HTTP_FORMAT)));
	responseText.append(Poco::format("DAAP-Server: %s\r\n", Plugin::userAgent()));
	responseText.append("Content-Type: application/x-dmap-tagged\r\n");
	responseText.append("Content-Length: 0\r\n");
	responseText.append("\r\n");

	return responseText;
}


static void sendResponse(const std::string& responseText, StreamSocket& socket)
{
	assert(!responseText.empty());

	const char* const response = responseText.c_str();
	const size_t responseLength = responseText.length();

	// send response on specified socket
	size_t bytesSent = 0;
	while (bytesSent < responseLength)
	{
		int returnCode = socket.sendBytes(
			response + bytesSent, responseLength - bytesSent);
		if (returnCode <= 0)
		{
			throw std::runtime_error(Poco::format(
				"socket.sendBytes returned %i", returnCode));
		}

		bytesSent += static_cast<size_t>(returnCode);
	}
	if (bytesSent != responseLength)
	{
		throw std::runtime_error("bytesSent != responseLength");
	}
}


static void controlPlayer(Player& player, Command command)
{
	switch (command)
	{
	case PLAY:
		player.play();
		break;
	case PAUSE:
		player.pause();
		break;
	case STOP:
		player.stop();
		break;
	case RESTART:
		player.restart();
		break;
	case NEXT_TRACK:
		player.startNext();
		break;
	case PREV_TRACK:
		player.startPrev();
		break;
	case VOLUME_UP:
		player.increaseVolume();
		break;
	case VOLUME_DOWN:
		player.decreaseVolume();
		break;
	case TOGGLE_MUTE:
		player.toggleMute();
		break;
	case TOGGLE_RNDM:
		player.toggleShuffle();
		break;
	}
}


static void handleRequest(StreamSocket& socket,
	const DeviceManager& deviceManager, Player& player)
{
	Device::SharedPtr dvc;
	uint32_t id = 0;

	const std::string requestText(receiveRequest(socket));
	Debugger::print(requestText + std::string(80, '-'));
	Request request(parseRequest(requestText));

	// validate remote control identifier before allowing command
	if (!NumberParser::tryParseUnsigned(
		request.headers[REMOTE_CONTROL_ID_PARAMETER], id))
	{
		request.command = NONE;
	}

	const std::string responseText(buildResponse(request.command != NONE));
	Debugger::print(responseText + std::string(80, '-'));
	sendResponse(responseText, socket);

	switch (request.command)
	{
	case SET_PROPERTY:
		if (!(dvc = deviceManager.lookupDevice(id)).isNull())
		{
			if (request.params.count("dmcp.device-volume"))
			{
				const std::string volumeStr(request.params["dmcp.device-volume"]);
				const float volume = float(NumberParser::parseFloat(volumeStr));
				dvc->putVolume(volume);
			}
		}
		break;
	default:
		controlPlayer(player, request.command);
	}
}


//------------------------------------------------------------------------------


RemoteControl::RemoteControl(DeviceManager& deviceManager, Player& player)
:
	_deviceManager(deviceManager),
	_player(player),
	_stopThread(false),
	_thread("RemoteControl::run")
{
	_thread.start(*this);
}


RemoteControl::~RemoteControl()
{
	try
	{
		ServiceDiscovery::stop(_registerOperation);
	}
	CATCH_ALL

	try
	{
		_stopThread = true;
		_thread.join(5000);
	}
	CATCH_ALL
}


void RemoteControl::run()
{
	Debugger::print("Starting remote control thread...");

	try
	{
		// create socket to listen for DACP requests
		ServerSocket serverSocket(startServer());

		Socket::SocketList clientList;

		Socket::SocketList readList;
		Socket::SocketList writeList;
		Socket::SocketList exceptList;
		const Timespan timeout(50 * Timespan::MILLISECONDS);

		while (!_stopThread)
		{
			Thread::yield();

			readList.clear();
			readList.push_back(serverSocket);
			readList.insert(readList.end(), clientList.begin(), clientList.end());

			// select sockets in set which can be read from without blocking
			if (Socket::select(readList, writeList, exceptList, timeout) > 0)
			{
				// check if server socket is selected
				const Socket::SocketList::const_iterator pos =
					std::find(readList.begin(), readList.end(), serverSocket);
				if (pos != readList.end())
				{
					readList.erase(pos);

					// accept new client connection
					clientList.push_back(serverSocket.acceptConnection());
				}

				// check if any client sockets are selected
				if (!readList.empty())
				{
					for (Socket::SocketList::const_iterator it = readList.begin();
						it != readList.end(); ++it)
					{
						try
						{
							StreamSocket socket = *it;
							if (socket.available() > 0)
							{
								handleRequest(socket, _deviceManager, _player);
								continue; // retain client socket connection
							}
						}
						CATCH_ALL

						clientList.erase(
							std::find(clientList.begin(), clientList.end(), *it));
					}
				}
			}
		}
	}
	CATCH_ALL

	Debugger::print("Exiting remote control thread...");
}


ServerSocket RemoteControl::startServer()
{
	ServerSocket serverSocket;
	serverSocket.init(AF_INET);

	// ensure bind will provide exclusive access to DACP port if successful
	serverSocket.setOption(SOL_SOCKET, SO_EXCLUSIVEADDRUSE, TRUE);

	bool done = false;
	uint16_t port = DACP_PORT;
	do
	{
		try
		{
			serverSocket.bind(port);
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
		throw std::runtime_error("serverSocket.bind failed");
	}

	serverSocket.setNoDelay(true);
	serverSocket.listen();

	// advertise DACP server
	registerService(port);

	return serverSocket;
}


void RemoteControl::registerService(const uint16_t servicePort)
{
	if (ServiceDiscovery::isAvailable())
	{
		const std::string dacpId(Poco::format("%016?X", Plugin::dacpId()));
		const std::string serviceName("iTunes_Ctrl_" + dacpId);

		ServiceDiscovery::TXTRecord txtRecord;
		txtRecord.put("Ver", "65536");
		txtRecord.put("DbId", dacpId);

		_registerOperation = ServiceDiscovery::registerService(
			serviceName, "_dacp._tcp.", servicePort, txtRecord, *this);
		ServiceDiscovery::start(_registerOperation);
	}
}
