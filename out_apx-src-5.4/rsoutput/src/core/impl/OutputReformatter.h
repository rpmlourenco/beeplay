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

#ifndef OutputReformatter_h
#define OutputReformatter_h


#include "OutputFormat.h"
#include "OutputSink.h"
#include "Platform.h"
#include "Uncopyable.h"
#include <vector>
#include <samplerate.h>


class OutputReformatter
:
	public OutputSink,
	private Uncopyable
{
public:
	OutputReformatter(const OutputFormat& incoming, const OutputFormat& outgoing,
		OutputSink::SharedPtr);
	~OutputReformatter();

	double reformatRatio() const;
	double resampleRatio() const;

	time_t latency(const OutputFormat&) const;
	size_t buffered() const;
	size_t canWrite() const;

	void write(const byte_t*, size_t);
	void flush();
	void reset();

private:
	const OutputFormat _inFormat;
	const OutputFormat _outFormat;

	const double _reformatRatio;
	const double _resampleRatio;

	const size_t _inputFrameSize;
	const size_t _intermediateFrameSize;
	const size_t _outputFrameSize;

	std::vector<float> _inputBuffer;
	std::vector<float> _intermediateBuffer;
	std::vector<byte_t> _outputBuffer;

	OutputSink::SharedPtr _outputSink;

	SRC_STATE* _srcState;
};


inline double OutputReformatter::reformatRatio() const
{
	return _reformatRatio;
}


inline double OutputReformatter::resampleRatio() const
{
	return _resampleRatio;
}


#endif // OutputReformatter_h
