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

#include "Platform.h"
#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>


inline void Platform::Charset::fromUTF8(const std::string& src, std::string& dst)
{
	// transcode string from UTF-8 to UTF-16
	std::wstring uString; fromUTF8(src, uString);

	const int length = ::WideCharToMultiByte(
		CP_ACP, 0, uString.c_str(), -1, NULL, 0, NULL, NULL);
	if (length <= 0) {
		char message[100]; ::sprintf_s(message, sizeof(message),
			"WideCharToMultiByte failed with error code %lu", ::GetLastError());
		throw std::runtime_error(message);
	}

	std::vector<char> chars(length);

	const int result = ::WideCharToMultiByte(
		CP_ACP, 0, uString.c_str(), -1, &chars[0], length, NULL, NULL);
	if (result <= 0) {
		char message[100]; ::sprintf_s(message, sizeof(message),
			"WideCharToMultiByte failed with error code %lu", ::GetLastError());
		throw std::runtime_error(message);
	}

	dst = &chars[0];
}


inline void Platform::Charset::fromUTF8(const std::string& src, std::wstring& dst)
{
	const int length = ::MultiByteToWideChar(CP_UTF8, 0, src.c_str(), -1, NULL, 0);
	if (length <= 0) {
		char message[100]; ::sprintf_s(message, sizeof(message),
			"MultiByteToWideChar failed with error code %lu", ::GetLastError());
		throw std::runtime_error(message);
	}

	std::vector<wchar_t> wchars(length);

	const int result = ::MultiByteToWideChar(
		CP_UTF8, 0, src.c_str(), -1, &wchars[0], length);
	if (result <= 0) {
		char message[100]; ::sprintf_s(message, sizeof(message),
			"MultiByteToWideChar failed with error code %lu", ::GetLastError());
		throw std::runtime_error(message);
	}

	dst = &wchars[0];
}


inline void Platform::Charset::toUTF8(const std::string& src, std::string& dst)
{
	const int length = ::MultiByteToWideChar(CP_ACP, 0, src.c_str(), -1, NULL, 0);
	if (length <= 0) {
		char message[100]; ::sprintf_s(message, sizeof(message),
			"MultiByteToWideChar failed with error code %lu", ::GetLastError());
		throw std::runtime_error(message);
	}

	std::vector<wchar_t> wchars(length);

	const int result = ::MultiByteToWideChar(
		CP_ACP, 0, src.c_str(), -1, &wchars[0], length);
	if (result <= 0) {
		char message[100]; ::sprintf_s(message, sizeof(message),
			"MultiByteToWideChar failed with error code %lu", ::GetLastError());
		throw std::runtime_error(message);
	}

	// transcode string from UTF-16 to UTF-8
	toUTF8(&wchars[0], dst);
}


inline void Platform::Charset::toUTF8(const std::wstring& src, std::string& dst)
{
	const int length = ::WideCharToMultiByte(
		CP_UTF8, 0, src.c_str(), -1, NULL, 0, NULL, NULL);
	if (length <= 0) {
		char message[100]; ::sprintf_s(message, sizeof(message),
			"WideCharToMultiByte failed with error code %lu", ::GetLastError());
		throw std::runtime_error(message);
	}

	std::vector<char> chars(length);

	const int result = ::WideCharToMultiByte(
		CP_UTF8, 0, src.c_str(), -1, &chars[0], chars.size(), NULL, NULL);
	if (result <= 0) {
		char message[100]; ::sprintf_s(message, sizeof(message),
			"WideCharToMultiByte failed with error code %lu", ::GetLastError());
		throw std::runtime_error(message);
	}

	dst = &chars[0];
}


inline std::string Platform::Error::describe(const int errorCode)
{
	std::string errorDesc;

	assert(errorCode > 0); LPSTR string = NULL; DWORD length = ::FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, (DWORD) errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR) &string, 0, NULL);

	if (length > 0)
	{
		// capture string then free platform-allocated buffer
		std::string str(string, length); ::LocalFree(string);

		// strip trailing punctuation and whitespace characters
		const std::string::size_type pos = str.find_last_not_of(".! \t\r\n");
		if (pos != std::string::npos) str.erase(pos + 1);

		Platform::Charset::toUTF8(str, errorDesc);
	}

	return errorDesc;
}
