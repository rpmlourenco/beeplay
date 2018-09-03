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

#ifndef RemoteControl_h
#define RemoteControl_h


#include "DeviceManager.h"
#include "Platform.h"
#include "Player.h"
#include "ServiceDiscovery.h"
#include "Uncopyable.h"
#include <Poco/Runnable.h>
#include <Poco/Thread.h>
#include <Poco/Net/ServerSocket.h>


class RemoteControl
:
	public Poco::Runnable,
	public ServiceDiscovery::RegisterListener,
	private Uncopyable
{
public:
	RemoteControl(DeviceManager&, Player&);
	~RemoteControl();

	void run();

private:
	Poco::Net::ServerSocket startServer();

	void registerService(uint16_t);

	DeviceManager& _deviceManager;
	Player& _player;

	DNSServiceRef _registerOperation;

	volatile bool _stopThread;
	Poco::Thread _thread;
};


#endif // RemoteControl_h
