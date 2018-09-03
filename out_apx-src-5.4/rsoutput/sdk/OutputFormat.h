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

#ifndef OutputFormat_h
#define OutputFormat_h


#include "Platform.h"
#include <limits>


class RSOUTPUT_API SampleRate
{
public:
	explicit SampleRate(int);
	operator int() const;

private:
	int _value;
};


class RSOUTPUT_API SampleSize
{
public:
	explicit SampleSize(int);
	operator int() const;

private:
	int _value;
};


class RSOUTPUT_API ChannelCount
{
public:
	explicit ChannelCount(int);
	operator int() const;

private:
	int _value;
};


class RSOUTPUT_API OutputFormat
{
public:
	explicit OutputFormat(
		SampleRate rate = SampleRate(std::numeric_limits<int>::max()), // max Hz
		SampleSize size = SampleSize(2),                               // 16-bit
		ChannelCount count = ChannelCount(2));                         // stereo

	const SampleRate& sampleRate() const;
	const SampleSize& sampleSize() const;
	const ChannelCount& channelCount() const;

	bool operator ==(const OutputFormat&) const;

private:
	SampleRate _sampleRate;
	SampleSize _sampleSize;
	ChannelCount _channelCount;
};


#endif // OutputFormat_h
