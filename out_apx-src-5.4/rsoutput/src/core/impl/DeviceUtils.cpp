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

#include "DeviceDiscovery.h"
#include "DeviceInfo.h"
#include "DeviceUtils.h"
#include "Options.h"


class DeviceUtilsImpl : public DeviceDiscovery::Listener, private Uncopyable
{
public:
	DeviceUtilsImpl() {
		DeviceDiscovery::browseDevices(*this);
	}

	~DeviceUtilsImpl() {
		DeviceDiscovery::stopBrowsing(*this);
	}

	void onDeviceFound(const DeviceInfo& device) {
		_discoveredDevices.insert(device);
	}

	void onDeviceLost(const DeviceInfo& device) {
		_discoveredDevices.erase(device);
	}

	bool isDeviceStale(const DeviceInfo& device) const {
		return device.isZeroConf() && !_discoveredDevices.count(device);
	}

	DeviceInfoSet getAllKnownDevices() const {
		// hold reference to active options
		const Options::SharedPtr options = Options::getOptions();

		DeviceInfoSet devices(options->devices());
		devices.insert(_discoveredDevices.begin(), _discoveredDevices.end());

		return devices;
	}

	DeviceInfoSet getDefinedOrDiscoveredDevices() const {
		DeviceInfoSet devices(getAllKnownDevices());

		DeviceInfoSet::iterator it1 = devices.begin();
		while (it1 != devices.end()) {
			const DeviceInfoSet::iterator it2 = it1++;
			if (isDeviceStale(*it2)) devices.erase(it2);
		}

		return devices;
	}

private:
	DeviceInfoSet _discoveredDevices;
};


//------------------------------------------------------------------------------


DeviceUtils::DeviceUtils() : _impl(new DeviceUtilsImpl)
{
}


DeviceUtils::~DeviceUtils()
{
	delete _impl;
}


DeviceUtils::InfoListPtr DeviceUtils::enumerate() const
{
	InfoListPtr infos(new InfoList);

	const DeviceInfoSet devices(_impl->getAllKnownDevices());
	for (DeviceInfoSet::const_iterator it = devices.begin(); it != devices.end(); ++it)
	{
		const DeviceInfo& device = *it;

		const bool activated = Options::getOptions()->isActivated(device.name());
		const bool available = _impl->isDeviceStale(device);

		InfoList::value_type info = { device.name(), activated, available };
		infos->push_back(info);
	}

	return infos;
}


void DeviceUtils::toggle(const std::string& which)
{
	// hold reference to active options
	const Options::SharedPtr options = Options::getOptions();

	Options::SharedPtr opts = new Options;

	const DeviceInfoSet devices(_impl->getDefinedOrDiscoveredDevices());
	for (DeviceInfoSet::const_iterator it = devices.begin(); it != devices.end(); ++it)
	{
		const DeviceInfo& device = *it;

		bool checked = options->isActivated(device.name());
		if (which == device.name()) checked = !checked;

		// save only checked or manually-created devices
		if (checked || !device.isZeroConf())
		{
			opts->devices().insert(device);
			opts->setActivated(device.name(), checked);
		}
	}

	// transfer flags
	opts->setVolumeControl(options->getVolumeControl());
	opts->setPlayerControl(options->getPlayerControl());
	opts->setResetOnPause(options->getResetOnPause());

	// transfer passwords
	for (DeviceInfoSet::const_iterator it = opts->devices().begin();
		it != opts->devices().end(); ++it)
	{
		const DeviceInfo& device = *it;

		if (!options->getPassword(device.name()).empty())
		{
			opts->setPassword(device.name(),
				options->getPassword(device.name()),
				options->getRememberPassword(device.name()));
		}
	}

	Options::setOptions(opts);
}
