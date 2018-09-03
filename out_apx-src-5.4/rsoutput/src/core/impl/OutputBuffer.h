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

#ifndef OutputBuffer_h
#define OutputBuffer_h


#include "OutputSink.h"
#include "Platform.h"
#include "Uncopyable.h"


class OutputBuffer
:
	public OutputSink,
	private Uncopyable
{
public:
	OutputBuffer(OutputSink::SharedPtr);
	~OutputBuffer();

	time_t latency(const OutputFormat&) const;
	size_t buffered() const;
	size_t canWrite() const;

	void write(const byte_t*, size_t);
	void flush();
	void reset();

private:
	void checkOutputSink() const;
	void writeToOutputSink(bool flushBuffer = false);

	buffer_t _buffer, _temp;
	size_t _bufferAvailability;
	size_t _bufferReadIndex;
	size_t _bufferWriteIndex;

	OutputSink::SharedPtr _outputSink;
};


#endif // OutputBuffer_h
