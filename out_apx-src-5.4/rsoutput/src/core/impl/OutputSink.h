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

#ifndef OutputSink_h
#define OutputSink_h


#include "OutputFormat.h"
#include "Platform.h"
#include <Poco/SharedPtr.h>


class OutputSink
{
public:
	typedef Poco::SharedPtr<OutputSink> SharedPtr;

	virtual ~OutputSink();

	virtual time_t latency(const OutputFormat&) const = 0;
	virtual size_t buffered() const = 0;
	virtual size_t canWrite() const = 0;

	virtual void write(const byte_t*, size_t) = 0;
	virtual void flush() = 0;
	virtual void reset() = 0;
};


inline OutputSink::~OutputSink()
{
}


#endif // OutputSink_h
