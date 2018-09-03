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

#ifndef NumberParser_h
#define NumberParser_h


#include "Debugger.h"
#include <cerrno>
#include <limits>
#include <stdexcept>
#include <string>
#include <Poco/Format.h>


class NumberParser
{
public:
	template <typename T>
	static T parseDecimalIntegerTo(const std::string&);

	template <typename T>
	static T parseHexadecimalIntegerTo(const std::string&);

	template <typename T>
	static bool tryParseDecimalIntegerTo(const std::string&, T&);

	template <typename T>
	static bool tryParseHexadecimalIntegerTo(const std::string&, T&);

private:
	template <typename T, unsigned radix>
	static T parseIntegerTo(const std::string&);

	NumberParser();
};


//------------------------------------------------------------------------------


template <typename T>
T NumberParser::parseDecimalIntegerTo(const std::string& str)
{
	return parseIntegerTo<T,10>(str);
}


template <typename T>
T NumberParser::parseHexadecimalIntegerTo(const std::string& str)
{
	return parseIntegerTo<T,16>(str);
}


template <typename T>
bool NumberParser::tryParseDecimalIntegerTo(const std::string& str, T& val)
{
	try
	{
		val = parseIntegerTo<T,10>(str);
		return true;
	}
	CATCH_ALL

	return false;
}


template <typename T>
bool NumberParser::tryParseHexadecimalIntegerTo(const std::string& str, T& val)
{
	try
	{
		val = parseIntegerTo<T,16>(str);
		return true;
	}
	CATCH_ALL

	return false;
}


template <typename T, unsigned radix>
T NumberParser::parseIntegerTo(const std::string& str)
{
	typedef std::numeric_limits<T> TypeMetaData;

	poco_static_assert(TypeMetaData::is_integer);
	poco_static_assert(radix >= 2 && radix <= 36);

	char* end;
	errno = 0;

#pragma warning(push)
#pragma warning(disable:4127)
	if (TypeMetaData::is_signed)
#pragma warning(pop)
	{
		const long val = std::strtol(str.c_str(), &end, radix);
		if (*end != '\0')
		{
			throw std::invalid_argument(Poco::format(
				"The string '%s' is not a valid signed decimal integer", str));
		}
		if ((val == LONG_MIN && errno == ERANGE) || val < TypeMetaData::min())
		{
			throw std::range_error(Poco::format(
				"The string '%s' is too small to convert to the given type, which has a minimum of %li",
				str, static_cast<long>(TypeMetaData::min())));
		}
		if ((val == LONG_MAX && errno == ERANGE) || val > TypeMetaData::max())
		{
			throw std::range_error(Poco::format(
				"The string '%s' is too large to convert to the given type, which has a maximum of %li",
				str, static_cast<long>(TypeMetaData::max())));
		}
		return static_cast<T>(val);
	}
	else
	{
		const unsigned long val = std::strtoul(str.c_str(), &end, radix);
		if (*end != '\0')
		{
			throw std::invalid_argument(Poco::format(
				"The string '%s' is not a vaild unsigend decimal integer", str));
		}
		if ((val == ULONG_MAX && errno == ERANGE) || val > TypeMetaData::max())
		{
			throw std::range_error(Poco::format(
				"The string '%s' is too large to convert to the given type, which has a maximum of %lu",
				str, static_cast<unsigned long>(TypeMetaData::max())));
		}
		return static_cast<T>(val);
	}
}


#endif // NumberParser_h
