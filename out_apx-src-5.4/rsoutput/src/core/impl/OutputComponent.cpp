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

#include "Debugger.h"
#include "DeviceManager.h"
#include "Options.h"
#include "OutputBuffer.h"
#include "OutputComponent.h"
#include "OutputObserver.h"
#include "OutputReformatter.h"
#include "OutputSink.h"
#include "Platform.h"
#include "RemoteControl.h"
#include <cassert>
#include <exception>
#include <memory>
#include <utility>
#include <Poco/Runnable.h>
#include <Poco/Thread.h>


using Poco::Runnable;
using Poco::Thread;


class OutputComponentImpl
:
	public OutputObserver,
	public Runnable,
	private Uncopyable
{
	friend class OutputComponent;

private:
	explicit OutputComponentImpl(Player&);
	~OutputComponentImpl();

	void checkDeviceVolume();
	void createOutputChain();
	void onBytesOutput(size_t);
	void run();

private:
	bool _closeGracefully;
	bool _paused;
	float _volume;
	double _formatRatio;
	unsigned _flushCounter;

	OutputFormat _outputFormat;
	OutputSink::SharedPtr _outputSink;
	OutputComponent::ProgressCallback _progressCallback;

	Player& _player;
	DeviceManager _deviceManager;
	std::auto_ptr<RemoteControl> _remoteControl;

	volatile bool _stopThread;
	Thread _thread;
};


//------------------------------------------------------------------------------


OutputComponent::OutputComponent(Player& player)
:
	_impl(new OutputComponentImpl(player))
{
	Debugger::print("Initialized remote speakers output component.");
}


OutputComponent::~OutputComponent()
{
	delete _impl;

	Debugger::print("Finalized remote speakers output component.");
}


void OutputComponent::open(const OutputFormat& format, const OutputMetadata& metadata)
{
	Debugger::printTimestamp(__FUNCTION__);

	Debugger::printf(
		"Starting playback; sample rate = %i Hz, sample size = %i bits, channel count = %i.",
		(int) format.sampleRate(), (int) format.sampleSize() * 8, (int) format.channelCount());

	close(); // in case

	setMetadata(metadata);

	_impl->checkDeviceVolume();

	_impl->_deviceManager.openDevices();

	if (_impl->_deviceManager.isAnyDeviceOpen())
	{
		_impl->_outputFormat = format;
		_impl->createOutputChain();
		_impl->_paused = false;
	}
	else
	{
		throw std::exception("deviceManager.openDevices failed");
	}
}


void OutputComponent::close()
{
	const bool closeGracefully = _impl->_closeGracefully;

	// reinitialize playback state variables
	_impl->_closeGracefully = false;
	_impl->_flushCounter = 0;
	_impl->_formatRatio = 1;
	_impl->_paused = true;

	// reinitialize playback metadata
	_impl->_deviceManager.clearMetadata();

	// take responsibility for destroying output chain
	OutputSink::SharedPtr outputSink;
	outputSink.swap(_impl->_outputSink);

	if (!outputSink.isNull())
	{
		Debugger::print("Stopping playback...");

		if (closeGracefully && _impl->_deviceManager.isAnyDeviceOpen())
		{
			// flush buffers in output chain
			outputSink->flush();

			// wait for output sink to drain
			while (outputSink->buffered() > 0
				&& _impl->_deviceManager.isAnyDeviceOpen())
			{
				Thread::yield();
			}
		}
		else
		{
			// discard any buffered output data
			outputSink->reset();
		}

		Debugger::print("Stopped playback.");
	}
}


void OutputComponent::reset(const time_t offset)
{
	if (!_impl->_outputSink.isNull())
	{
		// discard any buffered output data and prepare output chain for writing
		_impl->_outputSink->reset();
	}

	// update playback metedata with new starting offset
	_impl->_deviceManager.setOffset(offset);
}


bool OutputComponent::write(const byte_t* const buffer, const size_t length)
{
	if (_impl->_paused)
	{
		// indicate inability to write at this time
		return false;
	}

	// reset indicators because player resumed its writing phase
	_impl->_closeGracefully = false;
	_impl->_flushCounter = 0;

	if (buffer == NULL || length == 0)
	{
		// player writing phase is over, so flush output buffers
		_impl->_outputSink->flush();
	}
	else
	{
		_impl->_outputSink->write(buffer, length);
	}

	return true;
}


size_t OutputComponent::canWrite() const
{
	return (_impl->_paused ? 0 : _impl->_outputSink->canWrite());
}


size_t OutputComponent::buffered() const
{
	const size_t buffered = (_impl->_outputSink.isNull() ? 0 : _impl->_outputSink->buffered());

	if (buffered > 0)
	{
		if (++_impl->_flushCounter > 16)
		{
			// flush after so many attempts to prevent infinite wait for players
			// that are not nice enough to signal the end of their writing phase
			_impl->_outputSink->flush();
			_impl->_flushCounter = 0;
		}
	}
	else
	{
		if (!_impl->_paused)
		{
			// signal close method that it should wait for output sink to finish
			// writing buffered audio data and it should not reset
			_impl->_closeGracefully = true;
		}
	}

	return buffered;
}


time_t OutputComponent::latency() const
{
	return _impl->_outputSink->latency(_impl->_outputFormat);
}


bool OutputComponent::setPaused(bool state)
{
	if (_impl->_paused != state)
	{
		std::swap(_impl->_paused, state);

		if (_impl->_paused)
		{
			if (Options::getOptions()->getResetOnPause())
			{
				// discard any buffered output data so playback stops immediately
				_impl->_outputSink->reset();
			}
		}
		else
		{
			// reopen devices if open for playback to recover from a long pause
			if (!_impl->_outputSink.isNull())
			{
				_impl->_deviceManager.openDevices();
			}
		}
	}

	return state;
}


void OutputComponent::setVolume(const float volume)
{
	assert(volume >= -100.0 && volume <= 0.0);

	_impl->_volume = volume;
}


void OutputComponent::setBalance(const float balance)
{
	assert(balance >= -1.0 && balance <= 1.0);

	// balance adjustment is not implemented
}


void OutputComponent::setMetadata(const OutputMetadata& metadata, const time_t offset)
{
	_impl->_deviceManager.setMetadata(metadata);
	_impl->_deviceManager.setOffset(offset);
}


void OutputComponent::setProgressCallback(ProgressCallback callback)
{
	_impl->_progressCallback = callback;
}


//------------------------------------------------------------------------------


OutputComponentImpl::OutputComponentImpl(Player& player)
:
	_closeGracefully(false),
	_paused(true),
	_formatRatio(1.0),
	_flushCounter(0),
	_player(player),
	_deviceManager(player, *this),
	_stopThread(false),
	_thread("OutputComponentImpl::run")
{
	Debugger::print("Initializing remote speakers output component...");

	_volume = _deviceManager.getVolume();

	_thread.start(*this);
}


OutputComponentImpl::~OutputComponentImpl()
{
	Debugger::print("Finalizing remote speakers output component...");

	try
	{
		_stopThread = true;
		_thread.join(5000);
	}
	CATCH_ALL
}


void OutputComponentImpl::checkDeviceVolume()
{
	bool volumeControlEnabled;
	// read option then release pointer immediately
	{
		const Options::SharedPtr options = Options::getOptions();
		volumeControlEnabled = options->getVolumeControl();
	}

	const float volume = (volumeControlEnabled ? _volume : 0.0f);

	if (_deviceManager.getVolume() != volume)
	{
		_deviceManager.setVolume(volume);
	}
}


void OutputComponentImpl::createOutputChain()
{
	// wrap device output sink to even out the unpredictability of write lengths
	_outputSink = new OutputBuffer(_deviceManager.outputSinkForDevices());

	if (!(_outputFormat == _deviceManager.outputFormat()))
	{
		Debugger::print("Different format :(");
		_outputSink = new OutputReformatter(
			_outputFormat, _deviceManager.outputFormat(), _outputSink);

		_formatRatio = _outputSink.cast<OutputReformatter>()->reformatRatio();
	} else {
		Debugger::print("Same format :)");
	}
}


void OutputComponentImpl::onBytesOutput(size_t bytesOutput)
{
	if (_progressCallback)
	{
		if (_formatRatio != 1.0)
		{
			bytesOutput = static_cast<size_t>(bytesOutput / _formatRatio);
		}

		_progressCallback(bytesOutput);
	}
}


void OutputComponentImpl::run()
{
	Debugger::print("Starting playback state monitoring thread...");

	static const long SLEEP_MSEC = 250;

	unsigned int pausedCount = 0;
	bool remoteControlEnabled;

	while (!_stopThread)
	{
		try
		{
			Thread::sleep(SLEEP_MSEC);

			// check for long pause with output devices open
			pausedCount = (_paused && _deviceManager.isAnyDeviceOpen(false) ? pausedCount + 1 : 0);
			if (pausedCount * SLEEP_MSEC >= 8000)
			{
				// close output devices when player has been paused or stopped
				// for at least eight seconds
				_deviceManager.closeDevices();
			}

			// check for change in volume or volume control option
			checkDeviceVolume();

			// read option then release pointer immediately
			{
				const Options::SharedPtr options = Options::getOptions();
				remoteControlEnabled = options->getPlayerControl();
			}

			// check for mismatch in state of remote control option and service
			if (remoteControlEnabled && _remoteControl.get() == NULL)
			{
				// start remote control service
				_remoteControl.reset(new RemoteControl(_deviceManager, _player));
			}
			else if (!remoteControlEnabled && _remoteControl.get() != NULL)
			{
				// stop remote control service
				_remoteControl.reset();
			}
		}
		CATCH_ALL
	}

	Debugger::print("Exiting playback state monitoring thread...");
}
