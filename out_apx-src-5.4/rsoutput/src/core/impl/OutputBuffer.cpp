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

#include "OutputBuffer.h"
#include "Platform.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <stdexcept>


static const size_t BUFFER_CAPACITY = 32 * 1024;


OutputBuffer::OutputBuffer(OutputSink::SharedPtr outputSink)
:
	_buffer(BUFFER_CAPACITY),
	_bufferAvailability(_buffer.size()),
	_bufferReadIndex(0),
	_bufferWriteIndex(0),
	_outputSink(outputSink)
{
}


OutputBuffer::~OutputBuffer()
{
}


time_t OutputBuffer::latency(const OutputFormat& format) const
{
	return _outputSink->latency(format);
}

size_t OutputBuffer::buffered() const
{
	checkOutputSink();

	return (_buffer.size() - _bufferAvailability) + _outputSink->buffered();
}


size_t OutputBuffer::canWrite() const
{
	checkOutputSink();

	return _bufferAvailability;
}


void OutputBuffer::write(const byte_t* const buffer, const size_t length)
{
	if (buffer == NULL || length == 0 || length > _bufferAvailability)
	{
		throw std::invalid_argument(
			"buffer == NULL || length == 0 || length > _bufferAvailability");
	}

	// write data to buffer
	if (_bufferWriteIndex < _bufferReadIndex
		|| length <= _buffer.size() - _bufferWriteIndex)
	{
		std::memcpy(&_buffer[_bufferWriteIndex], buffer, length);
	}
	else
	{
		const size_t part1 = _buffer.size() - _bufferWriteIndex;
		std::memcpy(&_buffer[_bufferWriteIndex], buffer, part1);
		std::memcpy(&_buffer[0], buffer + part1, length - part1);
	}

	_bufferAvailability -= length;
	_bufferWriteIndex = (_bufferWriteIndex + length) % _buffer.size();

	writeToOutputSink();
}


void OutputBuffer::flush()
{
	writeToOutputSink(true);
}


void OutputBuffer::reset()
{
	_bufferAvailability = _buffer.size();
	_bufferReadIndex = 0;
	_bufferWriteIndex = 0;
	_outputSink->reset();
}


//------------------------------------------------------------------------------


void OutputBuffer::checkOutputSink() const
{
	const size_t canRead = _buffer.size() - _bufferAvailability;

	if (canRead > 0 && canRead >= _outputSink->canWrite())
	{
		// resume writing to output sink if data is available and sink is as well
		const_cast<OutputBuffer*>(this)->writeToOutputSink();
	}
}


void OutputBuffer::writeToOutputSink(const bool flushBuffer)
{
	unsigned sleeps = 0;
repeat:
	const size_t canRead = _buffer.size() - _bufferAvailability;
	const size_t canWrite = _outputSink->canWrite();

	if (canRead > 0 && ((canWrite > 0 && canRead >= canWrite) || flushBuffer))
	{
		if (canWrite == 0)
		{
			if (sleeps++ > 11) return;
			Sleep(1); goto repeat;
		}
		sleeps = 0;

		buffer_t::const_pointer ptr;
		const size_t doWrite = std::min(canRead, canWrite);

		// check if data to be written is contiguous or not
		if (_bufferReadIndex < _bufferWriteIndex
			|| doWrite <= _buffer.size() - _bufferReadIndex)
		{
			ptr = &_buffer[_bufferReadIndex];
		}
		else
		{
			if (_temp.size() < doWrite)
			{
				_temp.resize(doWrite);
			}

			const size_t part1 = _buffer.size() - _bufferReadIndex;
			std::memcpy(&_temp[0], &_buffer[_bufferReadIndex], part1);
			std::memcpy(&_temp[part1], &_buffer[0], doWrite - part1);

			ptr = &_temp[0];
		}

		_outputSink->write(ptr, doWrite);

		_bufferAvailability += doWrite;
		_bufferReadIndex = (_bufferReadIndex + doWrite) % _buffer.size();

		goto repeat;
	}

	if (flushBuffer)
	{
		// pass on a single flush to output sink when buffer is drained
		_outputSink->flush();
	}
}
