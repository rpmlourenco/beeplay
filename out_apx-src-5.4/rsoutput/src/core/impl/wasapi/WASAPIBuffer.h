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

#ifndef WASAPIBuffer_h
#define WASAPIBuffer_h


#include "Platform.h"
#include "Uncopyable.h"


class WASAPIBuffer
:
	private Uncopyable
{
public:
	WASAPIBuffer(size_t packetMaxSize, uint16_t headLength, uint16_t tailLength = 0);
	~WASAPIBuffer();

	void reset();

	bool canWrite() const;
	bool canRead() const;

	struct Slot {
		size_t packetSize;
		size_t payloadSize;
		size_t originalSize; // of payload before compression, encoding or padding
		uint16_t frameCount;
#pragma warning(push)
#pragma warning(disable:4200)
		float packetData[];
#pragma warning(pop)
	};

	      Slot& nextAvailable();
	      Slot& nextBuffered();
	const Slot& prevBuffered(uint16_t tailIndex) const;

private:
	const size_t _slotLength;
	const size_t _headLength;
	const size_t _tailLength;
	size_t _bufferAvailability;
	size_t _bufferReadIndex;
	size_t _bufferWriteIndex;
	buffer_t _buffer;
};


#endif // WASAPIBuffer_h
