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

#include "ConnectDialog.h"
#include "Debugger.h"
#include "DeviceManager.h"
#include "MessageDialog.h"
#include "Options.h"
#include "PasswordDialog.h"
#include "Uncopyable.h"
#include "raop/RAOPDevice.h"
#include "raop/RAOPEngine.h"
//RML include new special wasapi device
#include "wasapi/WASAPIDevice.h"
#include <cassert>
#include <stdexcept>
#include <string>
#include <Poco/Format.h>
#include <Poco/Timespan.h>
#include <Poco/Timestamp.h>
#include <Poco/Net/StreamSocket.h>


using Poco::Timespan;
using Poco::Timestamp;
using Poco::Net::StreamSocket;


// shorthand for accessing the implementation type of device output sink
#define RAOP_ENGINE (*(*this).outputSinkForDevices().cast<RAOPEngine>())


DeviceManager::DeviceManager(Player& player, OutputObserver& outputObserver)
:
	_volume(FLT_MIN),
	_player(player),
	_outputObserver(outputObserver),
	_deviceObserver(*this, &DeviceManager::onDeviceChanged)
{
	Options::addObserver(_deviceObserver);
}


DeviceManager::~DeviceManager()
{
	Options::removeObserver(_deviceObserver);
}


void DeviceManager::openDevices()
{
	bool anyDeviceActivated = false;

	// hold reference to active options
	const Options::SharedPtr options = Options::getOptions();

	for (DeviceInfoSet::const_iterator it = options->devices().begin();
		it != options->devices().end(); ++it)
	{
		const DeviceInfo& deviceInfo = *it;

		if (options->isActivated(deviceInfo.name()))
		{
			anyDeviceActivated = true;

			openDevice(deviceInfo);
		}
	}

	if (!anyDeviceActivated)
	{
		// limit rate of warning messages because some players will try to open
		// the next track in a playlist if the current track failed and this can
		// cause a long sequence of dialog boxes that must be dismissed manually
		static Timestamp lastAlertTime = 0;
		if (lastAlertTime.elapsed() > (5 * Timespan::SECONDS))
		{
			lastAlertTime.update();

			MessageDialog("No remote speakers are selected for output.").doModal();
		}
	}
}


void DeviceManager::closeDevices()
{
	ScopedLock lock(_mutex);

	for (DeviceMap::const_iterator it = _devices.begin(); it != _devices.end(); ++it)
	{
		DeviceMap::mapped_type device = it->second;

		device->close();
	}
}


bool DeviceManager::isAnyDeviceOpen(const bool ping) const
{
	ScopedLock lock(_mutex);

	for (DeviceMap::const_iterator it = _devices.begin(); it != _devices.end(); ++it)
	{
		const DeviceMap::mapped_type device = it->second;

		if (device->isOpen(ping))
		{
			return true;
		}
	}

	return false;
}


void DeviceManager::setVolume(const float level)
{
	ScopedLock lock(_mutex);

	const float delta = volumeSet() ? (level - _volume) : 0;  _volume = level;

	for (DeviceMap::const_iterator it = _devices.begin(); it != _devices.end(); ++it)
	{
		DeviceMap::mapped_type device = it->second;

		if (device->isOpen())
		{
			device->setVolume(level, delta);
		}
	}
}


void DeviceManager::setOffset(const time_t offset)
{
	ScopedLock lock(_mutex);

	const time_t length = _outputMetadata.length();
	assert(offset <= length);

	// calculate timestamps of chapter/track begin and end
	_outputInterval = RAOP_ENGINE.getOutputInterval(length, offset);

	for (DeviceMap::const_iterator it = _devices.begin(); it != _devices.end(); ++it)
	{
		DeviceMap::mapped_type device = it->second;

		if (device->isOpen())
		{
			device->updateProgress(_outputInterval);
		}
	}
}


void DeviceManager::setMetadata(const OutputMetadata& metadata)
{
	ScopedLock lock(_mutex);

	_outputMetadata = metadata;

	for (DeviceMap::const_iterator it = _devices.begin(); it != _devices.end(); ++it)
	{
		DeviceMap::mapped_type device = it->second;

		if (device->isOpen())
		{
			device->updateMetadata(_outputMetadata);
		}
	}
}


void DeviceManager::clearMetadata()
{
	ScopedLock lock(_mutex);

	_outputInterval = OutputInterval();
	_outputMetadata = OutputMetadata();
}


const OutputFormat& DeviceManager::outputFormat() const
{
	return RAOPEngine::outputFormat();
}


OutputSink::SharedPtr DeviceManager::outputSinkForDevices()
{
	if (_deviceOutputSink.isNull())
	{
		_deviceOutputSink = new RAOPEngine(_outputObserver);
	}
	return _deviceOutputSink;
}


Device::SharedPtr DeviceManager::lookupDevice(const uint32_t remoteControlId) const
{
	if (remoteControlId > 0)
	{
		ScopedLock lock(_mutex);

		for (DeviceMap::const_iterator it = _devices.begin(); it != _devices.end(); ++it)
		{
			const DeviceMap::mapped_type device = it->second;

			if (device->remoteControlId() == remoteControlId)
			{
				return device;
			}
		}
	}

	return NULL;
}


//------------------------------------------------------------------------------


Device::SharedPtr DeviceManager::createDevice(const DeviceInfo& deviceInfo)
{
	switch (LOWORD(deviceInfo.type()))
	{
	case DeviceInfo::APX:
		return new RAOPDevice(RAOP_ENGINE,
			RAOPDevice::ET_NONE, RAOPDevice::MD_NONE);

	case DeviceInfo::AP2:
		return new RAOPDevice(RAOP_ENGINE,
			RAOPDevice::ET_NONE, RAOPDevice::MD_NONE);

	case DeviceInfo::AS3:
		return new RAOPDevice(RAOP_ENGINE,
			RAOPDevice::ET_SECURED, RAOPDevice::MD_NONE);

	case DeviceInfo::AS4:
	case DeviceInfo::ATV:
	case DeviceInfo::AVR:
		return new RAOPDevice(RAOP_ENGINE,
			RAOPDevice::ET_NONE, RAOPDevice::MD_TEXT
							   | RAOPDevice::MD_IMAGE
							   | RAOPDevice::MD_PROGRESS);

	case DeviceInfo::AFS:
		return new RAOPDevice(RAOP_ENGINE,
			RAOPDevice::ET_SECURED, RAOPDevice::MD_TEXT
								  | RAOPDevice::MD_IMAGE
								  | RAOPDevice::MD_PROGRESS);

	case DeviceInfo::ANY:
		return new RAOPDevice(RAOP_ENGINE,
			// check device type bit-field for encryption and metadata settings
			!!(HIWORD(deviceInfo.type()) & 0x08), (HIWORD(deviceInfo.type()) & 0x07));

	//RML create special wasapi device
	case DeviceInfo::WIN:
		return new WASAPIDevice(RAOP_ENGINE,
			WASAPIDevice::ET_NONE, WASAPIDevice::MD_NONE);

	default:
		const std::string message(Poco::format(
			"Unable to playback to remote speakers \"%s\".\r\n\r\n"
			"Device type (%i) is unsupported at this time.",
			deviceInfo.name(), (int) deviceInfo.type()));
		MessageDialog(message).doModal();

		throw std::runtime_error(message);
	}
}


void DeviceManager::destroyDevice(const DeviceInfo& deviceInfo)
{
	ScopedLock lock(_mutex);

	_devices.erase(deviceInfo.name());
}


void DeviceManager::openDevice(const DeviceInfo& deviceInfo)
{
	try
	{
		ScopedLock lock(_mutex);

		if (_devices.count(deviceInfo.name()) == 0)
		{
			Device::SharedPtr device = createDevice(deviceInfo);

			_devices[deviceInfo.name()] = device;

			if (deviceInfo.type() != DeviceInfo::WIN) {

				// run dialog box that will asynchronously resolve service name to
				// host and port, resolve host to IP address and connect to address
				// and port
				ConnectDialog connectDialog(deviceInfo);
				if (connectDialog.doModal() != 0)
				{
					const std::string message(Poco::format(
						"Unable to connect to remote speakers \"%s\".",
						deviceInfo.name()));
					throw std::runtime_error(message);
				}
				StreamSocket& socket = connectDialog.socket();

				Debugger::printf("Connected to remote speakers \"%s\" at %s.",
					deviceInfo.name().c_str(), socket.peerAddress().toString().c_str());

				int returnCode = device->test(socket, true);

				// check if remote speakers require a password
				while (returnCode == 401)
				{
					Options::SharedPtr options = Options::getOptions();

					// check for password in options
					if (options->getPassword(deviceInfo.name()).empty())
					{
						// prompt user for password
						PasswordDialog passwordDialog(deviceInfo.name());
						if (passwordDialog.doModal(_player.window()))
						{
							throw std::invalid_argument("No password entered.");
						}

						options->setPassword(deviceInfo.name(),
							passwordDialog.password(),
							passwordDialog.rememberPassword());
					}

					device->setPassword(options->getPassword(deviceInfo.name()));

					returnCode = device->test(socket, false);

					// check if password was not accepted
					if (returnCode == 401)
					{
						options->clearPassword(deviceInfo.name());
					}

					// repeat until password is accepted or user cancels
				}

				device->close();

				// check for initiation error
				if (returnCode)
				{
					const std::string message(Poco::format(
						"Unable to initiate session with remote speakers \"%s\".\r\n\r\n"
						"Error code: %i", deviceInfo.name(), returnCode));
					MessageDialog(message, MB_ICONERROR).doModal();

					throw std::runtime_error(message);
				}

			}
		}

		DeviceMap::mapped_type device = _devices[deviceInfo.name()];

		if (!device->isOpen())
		{
			if (!isAnyDeviceOpen())
			{
				// before opening the first device, init shared session state
				RAOP_ENGINE.reinit(_outputInterval);
			}

			if (deviceInfo.type() != DeviceInfo::WIN) {

				// run dialog box that will asynchronously resolve service name to
				// host and port, resolve host to IP address and connect to address
				// and port
				ConnectDialog connectDialog(deviceInfo);
				if (connectDialog.doModal() != 0)
				{
					const std::string message(Poco::format(
						"Unable to connect to remote speakers \"%s\".",
						deviceInfo.name()));
					throw std::runtime_error(message);
				}
				StreamSocket& socket = connectDialog.socket();

				AudioJackStatus audioJackStatus = AUDIO_JACK_CONNECTED;


				// negotiate session parameters with remote speakers
				int returnCode = 0;
				if (deviceInfo.type() == DeviceInfo::AP2) {
					returnCode = device->open(socket, audioJackStatus, true);
				}
				else 
				{
					returnCode = device->open(socket, audioJackStatus, false);
				}				

				// check if remote speakers require a password
				while (returnCode == 401)
				{
					Options::SharedPtr options = Options::getOptions();

					// check for password in options
					if (options->getPassword(deviceInfo.name()).empty())
					{
						// prompt user for password
						PasswordDialog passwordDialog(deviceInfo.name());
						if (passwordDialog.doModal(_player.window()))
						{
							throw std::invalid_argument("No password entered.");
						}

						options->setPassword(deviceInfo.name(),
							passwordDialog.password(),
							passwordDialog.rememberPassword());
					}

					device->setPassword(options->getPassword(deviceInfo.name()));

					// negotiate session parameters with remote speakers again
					if (deviceInfo.type() == DeviceInfo::AP2) {
						returnCode = device->open(socket, audioJackStatus, true);
					}
					else
					{
						returnCode = device->open(socket, audioJackStatus, false);
					}

					// check if password was not accepted
					if (returnCode == 401)
					{
						options->clearPassword(deviceInfo.name());
					}

					// repeat until password is accepted or user cancels
				}

				// check for negotiation error
				if (returnCode)
				{
					if (returnCode == 453)
					{
						const std::string message(Poco::format(
							"Remote speakers \"%s\" are in use by another player.",
							deviceInfo.name()));
						MessageDialog(message).doModal();

						throw std::runtime_error(message);
					}
					else
					{
						const std::string message(Poco::format(
							"Unable to connect to remote speakers \"%s\".\r\n\r\n"
							"Error code: %i", deviceInfo.name(), returnCode));
						MessageDialog(message, MB_ICONERROR).doModal();

						throw std::runtime_error(message);
					}
				}
				else if (audioJackStatus == AUDIO_JACK_DISCONNECTED)
				{
					const std::string message(Poco::format(
						"Audio jack on remote speakers \"%s\" is not connected.",
						deviceInfo.name()));
					MessageDialog(message).doModal();
				}

				if (deviceInfo.type() == DeviceInfo::AVR) device->getVolume();
				if (volumeSet()) device->setVolume(_volume, 0);

				if (_outputMetadata.length() > 0 || !_outputMetadata.title().empty())
				{
					device->updateMetadata(_outputMetadata);
					device->updateProgress(_outputInterval);
				}

			} 
			else
			{
				//WASAPI device
				StreamSocket& dummySocket = *new(StreamSocket);
				AudioJackStatus audioJackStatus = AUDIO_JACK_CONNECTED;
				int returnCode = device->open(dummySocket, audioJackStatus, false);
				if (!returnCode) {
					Debugger::printf("Opened WASAPI device");
				}
			}
		}
		else if (_outputMetadata.length() > 0)
		{
			device->updateProgress(_outputInterval);
		}

		return; // bypass exception handling
	}
	CATCH_ALL

	Options::getOptions()->setActivated(deviceInfo.name(), false);
	Options::postNotification(
		new DeviceNotification(DeviceNotification::DEACTIVATE, deviceInfo));
}


void DeviceManager::onDeviceChanged(DeviceNotification* const notification)
{
	try
	{
		switch (notification->changeType())
		{
		case DeviceNotification::ACTIVATE:
			// open device if open for playback to pick it up immediately
			if (_deviceOutputSink.referenceCount() > 1)
			{
				openDevice(notification->deviceInfo());
			}
			break;

		case DeviceNotification::DEACTIVATE:
			destroyDevice(notification->deviceInfo());
			break;
		}
	}
	CATCH_ALL
}
