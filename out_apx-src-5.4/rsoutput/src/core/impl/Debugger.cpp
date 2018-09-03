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
#include "Platform.inl"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <stdexcept>
#include <vector>
#include <windows.h>
#include <Poco/Debugger.h>
#include <Poco/Exception.h>
#include <Poco/Format.h>

namespace UTF16 = Platform::Charset;

static Debugger::PrintCallback _echo = NULL;


void Debugger::print(const std::string& msg)
{
	if (_echo) try { _echo(msg.c_str()); } catch (...) {}

	try
	{
		if (Poco::Debugger::isAvailable())
		{
			Poco::Debugger::message(msg);
		}
		else
		{
			std::wstring msg2;
			UTF16::fromUTF8(msg, msg2);
			OutputDebugStringW(msg2.c_str());
		}
	}
	catch (...)
	{
		Poco::Debugger::enter(msg, __FILE__, __LINE__);
	}
}


void Debugger::printf(const char* const fmt, ...)
{
	try
	{
		va_list args;
		va_start(args, fmt);

		std::vector<char> buf(1024);
		const int len = vsnprintf(&buf[0], buf.size(), fmt, args);

		va_end(args);

		if (len < 0 || len > (int) buf.size())
		{
			throw std::length_error(Poco::format("vsnprintf returned %d", len).c_str());
		}

		print(std::string(&buf[0], len));
	}
	catch (...)
	{
		Poco::Debugger::enter(fmt, __FILE__, __LINE__);
	}
}


void Debugger::printException(const std::exception& except, const std::string& scope)
{
	std::string message;

	if (!scope.empty())
	{
		message.append(scope);
		message.append(" had exception: ");
	}

	try
	{
		const Poco::Exception& ex = dynamic_cast<const Poco::Exception&>(except);
		message.append(ex.displayText());
	}
	catch (...)
	{
		message.append(except.what());
	}

	print(message);
}


void Debugger::printLastError(const std::string& scope, const char* const file, const int line)
{
	std::string message;

	if (!scope.empty())
	{
		message.append(scope);
		message.append(" failed with error: ");
	}

	try
	{
		message.append(Platform::Error::describeLast());
	}
	catch (...)
	{
		char chars[64]; message.append(ultoa(GetLastError(), chars, 10));
	}

	print(message);

	if (file != NULL && file[0] != '\0' && line > 0)
	{
		Poco::Debugger::enter(file, line);
	}
}


void Debugger::setPrintCallback(const PrintCallback proc)
{
	_echo = proc;
}
