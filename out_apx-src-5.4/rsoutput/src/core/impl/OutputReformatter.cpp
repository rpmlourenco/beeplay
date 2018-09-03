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

#include "OutputReformatter.h"
#include "Platform.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <stdexcept>


static void ccc_mono_to_stereo(const byte_t* const in, byte_t* const out,
	const int sampleCount, const SampleSize& sampleSize)
{
	// work from back to front to support in-place conversion
	for (int s = sampleCount - 1; s >= 0; --s)
	{
		// duplicate channel data byte by byte
		for (int b = sampleSize - 1; b >= 0; --b)
		{
			out[(s * sampleSize * 2) + b] = in[(s * sampleSize) + b];
			out[(s * sampleSize * 2) + sampleSize + b] = in[(s * sampleSize) + b];
		}
	}
}


static void src_any_to_float_array(const byte_t* const in, float* const out,
	const int sampleCount, const SampleSize& sampleSize)
{
	const int signBit = 1 << ((sampleSize * 8) - 1);
	const float scale = 1.0F * signBit;

	for (int s = 0; s < sampleCount; ++s)
	{
		// load sample byte by byte
		int sample = 0;
		for (int b = sampleSize - 1; b >= 0; --b)
		{
			sample = (sample << 8) | in[(s * sampleSize) + b];
		}

		// extend sign bit if sample is negative
		if (sampleSize < sizeof(int) && sample & signBit)
		{
			for (int b = sampleSize; b < sizeof(int); ++b)
			{
				sample |= 0xFF << (b * 8);
			}
		}

		// scale sample into range [-1.0,1.0]
		out[s] = sample / scale;
	}
}


//------------------------------------------------------------------------------


OutputReformatter::OutputReformatter(const OutputFormat& inFormat,
	const OutputFormat& outFormat, OutputSink::SharedPtr outputSink)
:
	_inFormat(inFormat),
	_outFormat(outFormat),
	_reformatRatio(
		static_cast<double>(_outFormat.sampleRate() * _outFormat.sampleSize() * _outFormat.channelCount())
			/
		static_cast<double>(_inFormat.sampleRate() * _inFormat.sampleSize() * _inFormat.channelCount())),
	_resampleRatio(
		static_cast<double>(_outFormat.sampleRate())
			/
		static_cast<double>(_inFormat.sampleRate())),
	_inputFrameSize(_inFormat.sampleSize() * _inFormat.channelCount()),
	_intermediateFrameSize(_outFormat.sampleSize() * _inFormat.channelCount()),
	_outputFrameSize(_outFormat.sampleSize() * _outFormat.channelCount()),
	_outputSink(outputSink),
	_srcState(NULL)
{
	if (_inFormat.sampleRate() != _outFormat.sampleRate())
	{
		// initialize sample rate converter
		int error = 0;
		_srcState = src_new(SRC_SINC_MEDIUM_QUALITY, _inFormat.channelCount(), &error);
		if (_srcState == NULL)
		{
			throw std::runtime_error(src_strerror(error));
		}
	}
}


OutputReformatter::~OutputReformatter()
{
	if (_srcState != NULL)
	{
		src_delete(_srcState);
	}
}


time_t OutputReformatter::latency(const OutputFormat& format) const
{
	if (!(format == _inFormat))
	{
		throw std::logic_error("format != _inFormat");
	}

	const time_t latency = static_cast<time_t>(
		static_cast<double>(_outputSink->latency(_outFormat)) * _resampleRatio);
	// TODO: Add in some latency for reformatter?

	return latency;
}


size_t OutputReformatter::buffered() const
{
	const size_t buffered = static_cast<size_t>(
		static_cast<double>(_outputSink->buffered()) * _reformatRatio);

	return buffered;
}


size_t OutputReformatter::canWrite() const
{
	size_t canWrite = static_cast<size_t>(
		static_cast<double>(_outputSink->canWrite()) / _reformatRatio);
	// adjust value to an integral number of samples per channel
	canWrite -= (canWrite % _inputFrameSize);
	assert(canWrite % _inputFrameSize == 0);

	return canWrite;
}


void OutputReformatter::write(const byte_t* const buffer, const size_t length)
{
	if (length > canWrite() || length % _inFormat.sampleSize() != 0)
	{
		throw std::invalid_argument(
			"length > canWrite() || length % _inFormat.sampleSize() != 0");
	}

	// calculate maximum possible number of reformatted output bytes
	size_t maxOutputLength = static_cast<size_t>(
		std::ceil(static_cast<double>(length) * _reformatRatio));
	// adjust value to an integral number of samples per channel
	size_t remainder = maxOutputLength % _outputFrameSize;
	if (remainder > 0)
		maxOutputLength += (_outputFrameSize - remainder);
	assert(maxOutputLength % _outputFrameSize == 0);

	// resize buffer if necessary to accommodate reformatted output
	if (_outputBuffer.size() < maxOutputLength)
		_outputBuffer.resize(maxOutputLength);

	const unsigned int inputSampleCount = (length / _inFormat.sampleSize());
	unsigned int outputSampleCount = inputSampleCount;

	if (_inFormat.sampleRate() != _outFormat.sampleRate()
		|| _inFormat.sampleSize() != _outFormat.sampleSize())
	{
		// resize buffer if necessary to accommodate input samples
		if (_inputBuffer.size() < inputSampleCount)
			_inputBuffer.resize(inputSampleCount);

		// convert samples to floating-point format
		src_any_to_float_array(
			buffer, &_inputBuffer[0], inputSampleCount, _inFormat.sampleSize());

		const float* sampleBuffer;

		if (_inFormat.sampleRate() != _outFormat.sampleRate())
		{
			// calculate maximum possible number of generated samples
			outputSampleCount = static_cast<unsigned int>(
				std::ceil(static_cast<double>(outputSampleCount) * _resampleRatio));
			// adjust value to an integral number of samples per channel
			remainder = outputSampleCount % _intermediateFrameSize;
			if (remainder > 0)
				outputSampleCount += (_intermediateFrameSize - remainder);
			assert(outputSampleCount % _intermediateFrameSize == 0);

			// resize buffer if necessary to accommodate output samples
			if (_intermediateBuffer.size() < outputSampleCount)
				_intermediateBuffer.resize(outputSampleCount);

			// prepare for sample rate conversion
			SRC_DATA srcData;
			std::memset(&srcData, 0, sizeof(SRC_DATA));
			srcData.data_in = &_inputBuffer[0];
			srcData.data_out = &_intermediateBuffer[0];
			srcData.input_frames = inputSampleCount / _inFormat.channelCount();
			srcData.output_frames = outputSampleCount / _inFormat.channelCount();
			srcData.src_ratio = _resampleRatio;

			if (length == 0)
			{
				// indicate there is no more input so sample rate converter will
				// not maintain any carryover state after next call
				srcData.end_of_input = 1;
			}

			assert(_srcState != NULL);

			// resample input data at output sample rate
			const int returnCode = src_process(_srcState, &srcData);
			if (returnCode != 0)
			{
				throw std::runtime_error(src_strerror(returnCode));
			}

			assert(srcData.input_frames_used == srcData.input_frames);

			outputSampleCount = srcData.output_frames_gen * _inFormat.channelCount();

			// check for buffer overflow
			assert(_intermediateBuffer.size() >= outputSampleCount);

			sampleBuffer = &_intermediateBuffer[0];
		}
		else
		{
			sampleBuffer = &_inputBuffer[0];
		}

		// check for buffer overflow
		assert(_outputBuffer.size() >= outputSampleCount * _outFormat.sampleSize());

		// use of src_float_to_short_array() assumes out sample size of 2
		assert(_outFormat.sampleSize() == 2);

		// convert samples to integer format
		src_float_to_short_array(
			sampleBuffer, reinterpret_cast<short*>(&_outputBuffer[0]), outputSampleCount);
	}

	// write may have been called with no input to flush sample rate converter;
	// check if nothing remains to be written
	if (outputSampleCount < 1)
	{
		return;
	}

	if (_inFormat.channelCount() != _outFormat.channelCount())
	{
		const byte_t* sampleBuffer;

		if (_inFormat.sampleRate() != _outFormat.sampleRate()
			|| _inFormat.sampleSize() != _outFormat.sampleSize())
		{
			// if sample rate and/or size conversion was done, output buffer
			// contains the sample data
			sampleBuffer = &_outputBuffer[0];
		}
		else
		{
			// otherwise use input buffer sample data
			sampleBuffer = buffer;
		}

		// check for buffer overflow
		assert(_outputBuffer.size() >=
			outputSampleCount * _outFormat.sampleSize() * _outFormat.channelCount());

		// use of ccc_mono_to_stereo() assumes in channel count of 1 and out channel count of 2
		assert(_inFormat.channelCount() == 1 && _outFormat.channelCount() == 2);

		ccc_mono_to_stereo(
			sampleBuffer, &_outputBuffer[0], outputSampleCount, _outFormat.sampleSize());

		assert(outputSampleCount % _inFormat.channelCount() == 0);
		outputSampleCount /= _inFormat.channelCount();
		outputSampleCount *= _outFormat.channelCount();
	}

	_outputSink->write(&_outputBuffer[0], outputSampleCount * _outFormat.sampleSize());
}


void OutputReformatter::flush()
{
	if (_srcState != NULL)
	{
		// flush sample rate converter with an empty write
		write(NULL, 0);
	}

	_outputSink->flush();
}


void OutputReformatter::reset()
{
	if (_srcState != NULL)
	{
		// reset state of sample rate converter
		int returnCode = src_reset(_srcState);
		if (returnCode != 0)
		{
			assert(returnCode == 0);
		}
	}

	_outputSink->reset();
}
