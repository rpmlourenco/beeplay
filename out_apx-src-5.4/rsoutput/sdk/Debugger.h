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

#ifndef Debugger_h
#define Debugger_h


#include "Platform.h"
#include <exception>
#include <string>
#include <stdio.h>


class RSOUTPUT_API Debugger
{
public:
	static void print(const std::string&);
	static void printf(const char* fmt, ...);
	static void printException(const std::exception&, const std::string& scope = "");
	static void printLastError(const std::string& scope = "", const char* file = NULL, int line = 0);

	typedef void (*PrintCallback)(const char*);
	static void setPrintCallback(PrintCallback);

private:
	Debugger();
};


#define CATCH_ALL                                                              \
	catch (const std::exception& except) {                                     \
		Debugger::printException(except, __FUNCTION__);                        \
	} catch (const std::string& string) {                                      \
		Debugger::printException(std::exception(string.c_str()), __FUNCTION__);\
	} catch (const char* const cstring) {                                      \
		Debugger::printException(std::exception(cstring), __FUNCTION__);       \
	} catch (const int& integer) {                                             \
		char chars[64]; ::sprintf_s(chars, "%d (%#.8x)", integer, integer);    \
		Debugger::printException(std::exception(chars), __FUNCTION__);         \
	} catch (...) {                                                            \
		Debugger::printException(std::exception("..."), __FUNCTION__);         \
	}


#endif // Debugger_h
