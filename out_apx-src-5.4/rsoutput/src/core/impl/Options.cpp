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
#include "DeviceInfo.h"
#include "DeviceNotification.h"
#include "Options.h"
#include "OptionsUtils.h"
#include "Platform.inl"
#include "Plugin.h"
#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <utility>
#include <openssl/evp.h>
#include <Poco/Format.h>
#include <Poco/NotificationCenter.h>
#include <Poco/SingletonHolder.h>


using Poco::AbstractObserver;
using Poco::Notification;
using Poco::NotificationCenter;
using Poco::SingletonHolder;


static Options::SharedPtr theOptions;


Options::SharedPtr Options::getOptions()
{
	return theOptions;
}


void Options::setOptions(Options::SharedPtr newOptions)
{
	assert(!newOptions.isNull());

	// hold reference to old options
	Options::SharedPtr oldOptions = theOptions;

	// make the change; old will be deleted when reference count drops to zero
	theOptions = newOptions;

	// post device change notifications

	if (!oldOptions.isNull())
	{
		for (DeviceInfoSet::const_iterator it = oldOptions->devices().begin();
			it != oldOptions->devices().end(); ++it)
		{
			const DeviceInfo& oldDeviceInfo = *it;

			DeviceInfoSet::const_iterator pos = newOptions->devices().find(oldDeviceInfo);
			if (pos != newOptions->devices().end())
			{
				const DeviceInfo& newDeviceInfo = *pos;

				if (!oldOptions->isActivated(oldDeviceInfo.name())
					&& newOptions->isActivated(newDeviceInfo.name()))
				{
					postNotification(new DeviceNotification(
						DeviceNotification::ACTIVATE, newDeviceInfo));
				}
				else if (oldOptions->isActivated(oldDeviceInfo.name())
					&& !newOptions->isActivated(newDeviceInfo.name()))
				{
					postNotification(new DeviceNotification(
						DeviceNotification::DEACTIVATE, newDeviceInfo));
				}
			}
			else
			{
				postNotification(new DeviceNotification(
					DeviceNotification::DEACTIVATE, oldDeviceInfo));

				postNotification(new DeviceNotification(
					DeviceNotification::DESTROY, oldDeviceInfo));
			}
		}
	}

	for (DeviceInfoSet::const_iterator it = newOptions->devices().begin();
		it != newOptions->devices().end(); ++it)
	{
		const DeviceInfo& newDeviceInfo = *it;

		if (oldOptions.isNull() || oldOptions->devices().count(newDeviceInfo) == 0)
		{
			postNotification(new DeviceNotification(
				DeviceNotification::CREATE, newDeviceInfo));

			if (newOptions->isActivated(newDeviceInfo.name()))
			{
				postNotification(new DeviceNotification(
					DeviceNotification::ACTIVATE, newDeviceInfo));
			}
		}
	}
}


static SingletonHolder<NotificationCenter> notificationCenter;


void Options::addObserver(const AbstractObserver& observer)
{
	NotificationCenter& nc = *(notificationCenter.get());
	nc.addObserver(observer);
}


void Options::removeObserver(const AbstractObserver& observer)
{
	NotificationCenter& nc = *(notificationCenter.get());
	nc.removeObserver(observer);
}


void Options::postNotification(Notification::Ptr notification)
{
	NotificationCenter& nc = *(notificationCenter.get());
	nc.postNotification(notification);
}


//------------------------------------------------------------------------------


bool Options::getVolumeControl() const
{
	return _volumeControl;
}


void Options::setVolumeControl(const bool state)
{
	_volumeControl = state;
}


bool Options::getPlayerControl() const
{
	return _playerControl;
}


void Options::setPlayerControl(const bool state)
{
	_playerControl = state;
}


bool Options::getResetOnPause() const
{
	return _resetOnPause;
}


void Options::setResetOnPause(const bool state)
{
	_resetOnPause = state;
}


const DeviceInfoSet& Options::devices() const
{
	return _devices;
}


DeviceInfoSet& Options::devices()
{
	return _devices;
}


bool Options::isActivated(const std::string& deviceName) const
{
	return (_activatedDevices.count(deviceName) != 0);
}


void Options::setActivated(const std::string& deviceName, const bool activate)
{
	if (activate)
	{
		_activatedDevices.insert(deviceName);
	}
	else
	{
		_activatedDevices.erase(deviceName);
	}
}


const std::string& Options::getPassword(const std::string& deviceName) const
{
	static const std::string emptyPassword;

	std::map< std::string, std::pair< std::string,bool>>::const_iterator pos =
		_devicePasswords.find(deviceName);
	return (pos != _devicePasswords.end() ? pos->second.first : emptyPassword);
}


bool Options::getRememberPassword(const std::string& deviceName) const
{
	std::map< std::string, std::pair< std::string,bool>>::const_iterator pos =
		_devicePasswords.find(deviceName);
	return (pos != _devicePasswords.end() ? pos->second.second : false);
}


void Options::setPassword(const std::string& deviceName,
	const std::string& password, const bool rememberPassword)
{
	_devicePasswords.insert(std::make_pair(deviceName, std::make_pair(password, rememberPassword)));
}


void Options::clearPassword(const std::string& deviceName)
{
	_devicePasswords.erase(deviceName);
}


//------------------------------------------------------------------------------


static bool compare(const DeviceInfoSet& lhs, const DeviceInfoSet& rhs)
{
	typedef DeviceInfoSet::const_iterator iterator_t;
	using std::rel_ops::operator !=;

	for (iterator_t left = lhs.begin(); left != lhs.end(); ++left)
	{
		// look for device info of same name in rhs
		const iterator_t rght = rhs.find(*left);
		if (rght != rhs.end())
		{
			if (*left != *rght)
			{
				return false;
			}
		}
		else
		{
			if (!(*left).isZeroConf())
			{
				// mismatch cannot be tolerated if device info is manually created
				return false;
			}
		}
	}
	return true;
}


bool operator ==(const Options& lhs, const Options& rhs)
{
	if (lhs.getVolumeControl() != rhs.getVolumeControl()
		|| lhs.getPlayerControl() != rhs.getPlayerControl()
		|| lhs.getResetOnPause() != rhs.getResetOnPause()
		|| lhs._activatedDevices.size() != rhs._activatedDevices.size()
		|| !std::equal(lhs._activatedDevices.begin(), lhs._activatedDevices.end(),
				rhs._activatedDevices.begin())
		|| lhs._devicePasswords.size() != rhs._devicePasswords.size()
		|| !std::equal(lhs._devicePasswords.begin(), lhs._devicePasswords.end(),
				rhs._devicePasswords.begin()))
	{
		return false;
	}
	if (lhs.devices().size() == 1 && rhs.devices().size() == 1)
		return (*lhs.devices().begin() == *rhs.devices().begin());
	if (!compare(lhs.devices(), rhs.devices()))
		return false;
	if (!compare(rhs.devices(), lhs.devices()))
		return false;
	return true;
}


//------------------------------------------------------------------------------


void OptionsUtils::loadOptions(const std::string& iniFilePath)
{
	Debugger::printf("Reading plug-in options from '%s'...", iniFilePath.c_str());

	Options::SharedPtr options = new Options;

	// read volume control flag
	options->setVolumeControl(0 != GetPrivateProfileIntA(
		Plugin::name().c_str(), "VolumeControl", 1, iniFilePath.c_str()));
	Debugger::printf(
		"Read 'VolumeControl' value '%i'.", (int) options->getVolumeControl());

	// read player control flag
	options->setPlayerControl(0 != GetPrivateProfileIntA(
		Plugin::name().c_str(), "PlayerControl", 1, iniFilePath.c_str()));
	Debugger::printf(
		"Read 'PlayerControl' value '%i'.", (int) options->getPlayerControl());

	// read reset on pause flag
	options->setResetOnPause(0 != GetPrivateProfileIntA(
		Plugin::name().c_str(), "ResetOnPause", 1, iniFilePath.c_str()));
	Debugger::printf(
		"Read 'ResetOnPause' value '%i'.", (int) options->getResetOnPause());

	int parameterValueLength;
	char parameterValue[128];

	for (int index = 1; index < 256; ++index)
	{
		// read device type integer
		const unsigned int type = GetPrivateProfileIntA(Plugin::name().c_str(),
			Poco::format("Device%i_Type", index).c_str(), 0, iniFilePath.c_str());
		switch (type)
		{
		case DeviceInfo::WIN:
		case DeviceInfo::APX:
		case DeviceInfo::ATV:
		case DeviceInfo::AVR:
		case DeviceInfo::AFS:
		case DeviceInfo::AS3:
		case DeviceInfo::AS4:
			break;
		default:
			if (LOWORD(type) != DeviceInfo::ANY) continue;
		}
		Debugger::printf("Read 'Device%i_Type' value '%u'.", index, type);

		// read device name string
		parameterValueLength = GetPrivateProfileStringA(Plugin::name().c_str(),
			Poco::format("Device%i_Name", index).c_str(), NULL, parameterValue,
			sizeof(parameterValue), iniFilePath.c_str());
		if (parameterValueLength <= 0)
		{
			continue;
		}
		const std::string name(parameterValue);
		Debugger::printf("Read 'Device%i_Name' value '%s'.", index, name.c_str());

		// read device address strings
		parameterValueLength = GetPrivateProfileStringA(Plugin::name().c_str(),
			Poco::format("Device%i_Address1", index).c_str(), NULL, parameterValue,
			sizeof(parameterValue), iniFilePath.c_str());
		if (parameterValueLength <= 0)
		{
			continue;
		}
		const std::string addr1(parameterValue);
		Debugger::printf("Read 'Device%i_Address1' value '%s'.", index, addr1.c_str());

		parameterValueLength = GetPrivateProfileStringA(Plugin::name().c_str(),
			Poco::format("Device%i_Address2", index).c_str(), NULL, parameterValue,
			sizeof(parameterValue), iniFilePath.c_str());
		if (parameterValueLength <= 0)
		{
			continue;
		}
		const std::string addr2(parameterValue);
		Debugger::printf("Read 'Device%i_Address2' value '%s'.", index, addr2.c_str());

		// read device zero-configuration flag
		const bool zeroConf = (0 != GetPrivateProfileIntA(Plugin::name().c_str(),
			Poco::format("Device%i_ZeroConf", index).c_str(), 0, iniFilePath.c_str()));
		Debugger::printf(
			"Read 'Device%i_ZeroConf' value '%i'.", index, (int) zeroConf);

		// create device info from parameters read so far
		const DeviceInfo device(
			DeviceInfo::DeviceType(type), name, std::make_pair(addr1, addr2), zeroConf);

		options->devices().insert(device);

		// read device activated flag
		options->setActivated(device.name(), (0 != GetPrivateProfileIntA(
			Plugin::name().c_str(), Poco::format("Device%i_Activated", index).c_str(),
			0, iniFilePath.c_str())));
		Debugger::printf(
			"Read 'Device%i_Activated' value '%i'.", index, options->isActivated(device.name()));

		// read device password string
		parameterValueLength = GetPrivateProfileStringA(Plugin::name().c_str(),
			Poco::format("Device%i_Password", index).c_str(), NULL,
			parameterValue, sizeof(parameterValue), iniFilePath.c_str());
		if (parameterValueLength <= 0)
		{
			continue;
		}
		else
		{
			Debugger::printf(
				"Read 'Device%i_Password' value '%s'.", index, parameterValue);

			// base64 decode password string
			parameterValueLength = EVP_DecodeBlock(
				(unsigned char*) parameterValue,
				(unsigned char*) parameterValue,
				parameterValueLength);
			if (parameterValueLength <= 0
				|| parameterValueLength >= (int) sizeof(parameterValue))
			{
				throw std::runtime_error("EVP_DecodeBlock failed");
			}
			parameterValue[parameterValueLength] = '\0';

			Debugger::printf("Decoded device password '%s'.", parameterValue);

			options->setPassword(device.name(), parameterValue, true);
		}
	}

	Options::setOptions(options);

	Debugger::printf("Read plug-in options from '%s'.", iniFilePath.c_str());
}


void OptionsUtils::saveOptions(const std::string& iniFilePath)
{
	Debugger::printf("Writing plug-in options to '%s'...", iniFilePath.c_str());

	// clear existing options in file
	if (!WritePrivateProfileSectionA(Plugin::name().c_str(), "", iniFilePath.c_str()))
	{
		Debugger::printf("Clearing old plug-in options failed with error '%s'.",
			Platform::Error::describeLast().c_str());
	}

	const Options::SharedPtr options = Options::getOptions();

	// write volume control flag
	WritePrivateProfileStringA(Plugin::name().c_str(), "VolumeControl",
		Poco::format("%b", options->getVolumeControl()).c_str(),
		iniFilePath.c_str());
	Debugger::printf(
		"Wrote 'VolumeControl' value '%i'.", (int) options->getVolumeControl());

	// write player control flag
	WritePrivateProfileStringA(Plugin::name().c_str(), "PlayerControl",
		Poco::format("%b", options->getPlayerControl()).c_str(),
		iniFilePath.c_str());
	Debugger::printf(
		"Wrote 'PlayerControl' value '%i'.", (int) options->getPlayerControl());

	// write reset on pause flag
	WritePrivateProfileStringA(Plugin::name().c_str(), "ResetOnPause",
		Poco::format("%b", options->getResetOnPause()).c_str(),
		iniFilePath.c_str());
	Debugger::printf(
		"Wrote 'ResetOnPause' value '%i'.", (int) options->getResetOnPause());

	int index = 0;
	for (DeviceInfoSet::const_iterator it = options->devices().begin();
		it != options->devices().end(); ++it)
	{
		const DeviceInfo& device = *it;

		if (!options->isActivated(device.name()) && device.isZeroConf())
		{
			// don't bother writing inactive zero-configuration devices because
			// they will be rediscovered automatically; this causes password to
			// be forgotten, but it can easily be reentered on next activation
			continue;
		}

		++index;

		// write device type integer
		WritePrivateProfileStringA(Plugin::name().c_str(),
			Poco::format("Device%i_Type", index).c_str(),
			Poco::format("%u", static_cast<unsigned int>(device.type())).c_str(),
			iniFilePath.c_str());
		Debugger::printf(
			"Wrote 'Device%i_Type' value '%u'.", index, (unsigned int) device.type());

		// write device name string
		WritePrivateProfileStringA(Plugin::name().c_str(),
			Poco::format("Device%i_Name", index).c_str(),
			device.name().c_str(), iniFilePath.c_str());
		Debugger::printf(
			"Wrote 'Device%i_Name' value '%s'.", index, device.name().c_str());

		// write device address strings
		WritePrivateProfileStringA(Plugin::name().c_str(),
			Poco::format("Device%i_Address1", index).c_str(),
			device.addr().first.c_str(), iniFilePath.c_str());
		Debugger::printf(
			"Wrote 'Device%i_Address1' value '%s'.", index, device.addr().first.c_str());

		WritePrivateProfileStringA(Plugin::name().c_str(),
			Poco::format("Device%i_Address2", index).c_str(),
			device.addr().second.c_str(), iniFilePath.c_str());
		Debugger::printf(
			"Wrote 'Device%i_Address2' value '%s'.", index, device.addr().second.c_str());

		// write device zero-configuration flag
		WritePrivateProfileStringA(Plugin::name().c_str(),
			Poco::format("Device%i_ZeroConf", index).c_str(),
			Poco::format("%b", device.isZeroConf()).c_str(),
			iniFilePath.c_str());
		Debugger::printf(
			"Wrote 'Device%i_ZeroConf' value '%i'.", index, (int) device.isZeroConf());

		// write device activated flag
		WritePrivateProfileStringA(Plugin::name().c_str(),
			Poco::format("Device%i_Activated", index).c_str(),
			Poco::format("%b", options->isActivated(device.name())).c_str(),
			iniFilePath.c_str());
		Debugger::printf(
			"Wrote 'Device%i_Activated' value '%i'.", index, (int) options->isActivated(device.name()));

		if (!options->getPassword(device.name()).empty()
			&& options->getRememberPassword(device.name()))
		{
			const std::string password = options->getPassword(device.name());
			Debugger::printf("Encoding device password '%s'...", password.c_str());

			int parameterValueLength;
			char parameterValue[128];

			// base64 encode password to obfuscate it
			parameterValueLength = EVP_EncodeBlock(
				(unsigned char*) parameterValue,
				(unsigned char*) password.c_str(),
				(int) password.length());
			if (parameterValueLength <= 0
				|| parameterValueLength >= (int) sizeof(parameterValue))
			{
				throw std::runtime_error("EVP_EncodeBlock failed");
			}
			parameterValue[parameterValueLength] = '\0';

			// write device password string
			WritePrivateProfileStringA(Plugin::name().c_str(),
				Poco::format("Device%i_Password", index).c_str(),
				parameterValue, iniFilePath.c_str());
			Debugger::printf(
				"Wrote 'Device%i_Password' value '%s'.", index, parameterValue);
		}
	}

	Debugger::printf("Wrote plug-in options to '%s'.", iniFilePath.c_str());
}
