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

#include "PacketBuffer.h"
#include <cassert>
#include <cstring>
#include <stdexcept>


PacketBuffer::PacketBuffer(const size_t packetMaxSize, const uint16_t headLength, const uint16_t tailLength)
:
	_slotLength(sizeof(Slot) + packetMaxSize),
	_headLength(headLength * _slotLength),
	_tailLength(tailLength * _slotLength),
	_buffer(_headLength + _tailLength)
{
	reset();
}


PacketBuffer::~PacketBuffer()
{
}


void PacketBuffer::reset()
{
	_bufferAvailability = _headLength;
	_bufferReadIndex = _bufferWriteIndex = 0;
}


bool PacketBuffer::canWrite() const
{
	return (_bufferAvailability > 0);
}


bool PacketBuffer::canRead() const
{
	return (_bufferAvailability < _headLength);
}


PacketBuffer::Slot& PacketBuffer::nextAvailable()
{
	if (!canWrite())
	{
		throw std::logic_error("Can't write at this time");
	}

	byte_t* const ptr = &_buffer[_bufferWriteIndex];

	std::memset(ptr, 0, _slotLength);

	_bufferAvailability -= _slotLength;
	_bufferWriteIndex = (_bufferWriteIndex + _slotLength) % _buffer.size();

	return *reinterpret_cast<Slot*>(ptr);
}


PacketBuffer::Slot& PacketBuffer::nextBuffered()
{
	if (!canRead())
	{
		throw std::logic_error("Can't read at this time");
	}

	byte_t* const ptr = &_buffer[_bufferReadIndex];

	_bufferAvailability += _slotLength;
	_bufferReadIndex = (_bufferReadIndex + _slotLength) % _buffer.size();

	return *reinterpret_cast<Slot*>(ptr);
}


const PacketBuffer::Slot& PacketBuffer::prevBuffered(const uint16_t tailIndex) const
{
	if (tailIndex < 1 || tailIndex > (_tailLength / _slotLength))
	{
		throw std::logic_error("Can't find requested packet");
	}

	const size_t indexDiff = (tailIndex * _slotLength);
	const size_t prevIndex = (indexDiff <= _bufferReadIndex
		? _bufferReadIndex - indexDiff
		: _buffer.size() - (indexDiff - _bufferReadIndex));
	assert(prevIndex <= (_buffer.size() - _slotLength));
	assert(prevIndex % _slotLength == 0);

	return *reinterpret_cast<const Slot*>(&_buffer[prevIndex]);
}
