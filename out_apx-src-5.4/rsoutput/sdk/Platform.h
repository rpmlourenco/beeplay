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

#ifndef Platform_h
#define Platform_h


// tag used on exported classes and functions
#if defined(RSOUTPUT_EXPORTS)
#	define RSOUTPUT_API __declspec(dllexport)
#else
#	define RSOUTPUT_API __declspec(dllimport)
#endif


#include <cstddef>
#include <ctime>
#include <string>
#include <utility>
#include <vector>
#include <tchar.h>
#include <windows.h>


// int types
typedef INT8 int8_t;
typedef UINT8 uint8_t;
typedef INT16 int16_t;
typedef UINT16 uint16_t;
typedef INT32 int32_t;
typedef UINT32 uint32_t;
typedef INT64 int64_t;
typedef UINT64 uint64_t;

// more types
using std::size_t;
using std::time_t;
typedef UINT8 byte_t;
typedef TCHAR char_t;
typedef std::vector<byte_t> buffer_t;
typedef std::pair<short,short> shorts_t;
typedef std::basic_string<TCHAR> string_t;


namespace Platform {

	namespace Charset // for string_t <-> std::string (with UTF-8)
	{
	// std::string s1(...); string_t s2; Platform::Charset::fromUTF8(s1, s2);
	void fromUTF8(const std::string& src, std::string& dst);
	void fromUTF8(const std::string& src, std::wstring& dst);

	// string_t s1(TEXT(...)); std::string s2; Platform::Charset::toUTF8(s1, s2);
	void toUTF8(const std::string& src, std::string& dst);
	void toUTF8(const std::wstring& src, std::string& dst);
	}

	namespace Error
	{
	extern std::string describe(int errorCode /* from [WSA]GetLastError */);
	inline std::string describeLast() { return describe(::GetLastError()); }
	}

} // namespace Platform


//------------------------------------------------------------------------------
// simple global utility functions for dialogs and windows

extern "C" {

RSOUTPUT_API void CenterWindowOverParent(HWND);
RSOUTPUT_API void SetWindowEnabled(HWND, BOOL);

RSOUTPUT_API void ResizeDlgItem(HWND dlg, int itm, int dx, int dy);
RSOUTPUT_API void RepositionDlgItem(HWND dlg, int itm, int dx, int dy);

inline LONG WIDTH(const RECT& rect) { return (rect.right - rect.left); }
inline LONG HEIGHT(const RECT& rect) { return (rect.bottom - rect.top); }

} // extern "C"


#endif // Platform_h
