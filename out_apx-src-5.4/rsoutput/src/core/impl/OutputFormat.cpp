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

#include "OutputFormat.h"
#include <cassert>


SampleRate::SampleRate(const int value)
:
	_value(value)
{
	assert(value > 0);
}


SampleRate::operator int() const
{
	return _value;
}


SampleSize::SampleSize(const int value)
:
	_value(value)
{
	assert(value >= 1 && value <= 4);
}


SampleSize::operator int() const
{
	return _value;
}


ChannelCount::ChannelCount(const int value)
:
	_value(value)
{
	assert(value >= 1 && value <= 2);
}


ChannelCount::operator int() const
{
	return _value;
}


OutputFormat::OutputFormat(const SampleRate sr, const SampleSize ss,
	const ChannelCount cc)
:
	_sampleRate(sr),
	_sampleSize(ss),
	_channelCount(cc)
{
}


const SampleRate& OutputFormat::sampleRate() const
{
	return _sampleRate;
}


const SampleSize& OutputFormat::sampleSize() const
{
	return _sampleSize;
}


const ChannelCount& OutputFormat::channelCount() const
{
	return _channelCount;
}


bool OutputFormat::operator ==(const OutputFormat& rhs) const
{
	return _sampleRate == rhs._sampleRate
		&& _sampleSize == rhs._sampleSize
		&& _channelCount == rhs._channelCount;
}
