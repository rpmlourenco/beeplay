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
#include "Dialog.h"
#include "Platform.inl"
#include "Resources.h"
#include "ServiceDiscovery.h"
#include <cassert>
#include <cmath>
#include <string>
#include <utility>
#include <commctrl.h>
#include <Poco/Net/Socket.h>
#include <Poco/Net/SocketAddress.h>


using Poco::Net::IPAddress;
using Poco::Net::Socket;
using Poco::Net::SocketAddress;
using Poco::Net::StreamSocket;


class ConnectDialogImpl
:
	public Dialog,
	public ServiceDiscovery::ResolveListener,
	public ServiceDiscovery::QueryListener,
	private Uncopyable
{
	friend class ConnectDialog;

	explicit ConnectDialogImpl(const DeviceInfo&);

	void onInitialize();
	void onCommand(WORD, WORD);
	void onTimer();

	void beginDeviceResolve();
	void beginSocketConnect(const SocketAddress&);

	void onServiceResolved(DNSServiceRef, std::string, std::string, uint16_t, const ServiceDiscovery::TXTRecord&);
	void onServiceQueried(DNSServiceRef, std::string, uint16_t, uint16_t, const void*, uint32_t);

	const DeviceInfo _device;
	StreamSocket _socket;

	uint32_t _milliseconds;
	uint32_t _connectCount;
	uint16_t _devicePort;
	DNSServiceRef _sdRef;
};


//------------------------------------------------------------------------------


ConnectDialog::ConnectDialog(const DeviceInfo& device)
:
	_impl(new ConnectDialogImpl(device))
{
}


ConnectDialog::~ConnectDialog()
{
	delete _impl;
}


int ConnectDialog::doModal(HWND parentWindow)
{
	return _impl->doModal(DIALOG_CONNECT, parentWindow, FALSE); // start hidden
}


StreamSocket& ConnectDialog::socket() const
{
	return _impl->_socket;
}


//------------------------------------------------------------------------------


ConnectDialogImpl::ConnectDialogImpl(const DeviceInfo& device)
:
	_device(device),
	_milliseconds(0),
	_connectCount(0),
	_devicePort(0),
	_sdRef(0)
{
}


void ConnectDialogImpl::onInitialize()
{
	string_t name;
	Platform::Charset::fromUTF8(_device.name(), name);

	int returnCode;
	char_t formatString[256];
	char_t finalString[256];

	returnCode = GetDlgItemText(_dialogWindow, DIALOG_CONNECT_STRING,
		formatString, sizeof(formatString) / sizeof(char_t));
	assert(returnCode > 0 && returnCode < sizeof(formatString) / sizeof(char_t));

	returnCode = _stprintf_s(finalString, sizeof(finalString) / sizeof(char_t),
		formatString, name.c_str());
	assert(returnCode > 0 && returnCode < sizeof(finalString) / sizeof(char_t));

	returnCode = SetDlgItemText(_dialogWindow, DIALOG_CONNECT_STRING, finalString);
	assert(returnCode != 0);

	// start timer to facilitate showing dialog and updating progress bar
	if (SetTimer(_dialogWindow, (UINT_PTR) this, 250, NULL) < 1)
	{
		Debugger::printLastError("SetTimer", __FILE__, __LINE__);
	}

	if (_device.isZeroConf())
	{
		beginDeviceResolve();
	}
	else
	{
		const std::string hostAndPort(
			_device.addr().first + ':' + _device.addr().second);

		beginSocketConnect(SocketAddress(hostAndPort));
	}
}


void ConnectDialogImpl::onCommand(WORD command, WORD control)
{
	if (control == IDCANCEL && command == BN_CLICKED)
	{
		KillTimer(_dialogWindow, (UINT_PTR) this);

		if (_sdRef)
		{
			DNSServiceRef sdRef = 0;
			std::swap(_sdRef, sdRef);
			try
			{
				ServiceDiscovery::stop(sdRef);
			}
			CATCH_ALL
		}

		_socket.close();
	}

	Dialog::onCommand(command, control);
}


void ConnectDialogImpl::onTimer()
{
	if (!IsWindowVisible(_dialogWindow))
	{
		if ((_milliseconds += 250) >= 3000)
		{
			// show dialog after 3 seconds
			ShowWindow(_dialogWindow, SW_SHOW);
		}
	}
	else
	{
		// update progress bar
		LRESULT progressLimit = SendDlgItemMessage(_dialogWindow,
			DIALOG_CONNECT_PROGRESS, PBM_GETRANGE, 0, 0);
		LRESULT progressValue = SendDlgItemMessage(_dialogWindow,
			DIALOG_CONNECT_PROGRESS, PBM_GETPOS, 0, 0);
		assert(progressLimit > 0);
		assert(progressValue >= 0);
		// increment progress value by 2% up to progress limit
		progressValue += (LRESULT) std::ceil(progressLimit * 0.02);
		progressValue %= progressLimit;
		SendDlgItemMessage(_dialogWindow, DIALOG_CONNECT_PROGRESS, PBM_SETPOS,
			(WPARAM) progressValue, 0);
	}

	if (_connectCount > 0)
	{
		// poll socket for writability
		if (_socket.poll(0, Socket::SELECT_WRITE))
		{
			KillTimer(_dialogWindow, (UINT_PTR) this);
			endDialog(0);
		}
		else if (_device.isZeroConf() && _connectCount++ > 20)
		{
			// start over
			_socket.close();
			beginDeviceResolve();
		}
	}
}


void ConnectDialogImpl::beginDeviceResolve()
{
	try
	{
		if (_sdRef) ServiceDiscovery::stop(_sdRef);
	}
	CATCH_ALL

	_connectCount = 0;
	_devicePort = 0;
	_sdRef = 0;

	try
	{
		// resolve host and port from service name and type
		_sdRef = ServiceDiscovery::resolveService(
			_device.addr().first, _device.addr().second, *this);
		ServiceDiscovery::start(_sdRef);
	}
	CATCH_ALL
}


void ConnectDialogImpl::beginSocketConnect(const SocketAddress& addr)
{
	_connectCount = 1;
	_socket.connectNB(addr);
}


void ConnectDialogImpl::onServiceResolved(
	DNSServiceRef     sdRef,
	const std::string name,
	const std::string host,
	const uint16_t    port,
	const ServiceDiscovery::TXTRecord& txtRecord)
{
	assert(sdRef == _sdRef);
	assert(name == ServiceDiscovery::fullName(
		_device.addr().first, _device.addr().second));
	assert(!host.empty());
	assert(port > 0);

	// stop service resolve activity
	ServiceDiscovery::stop(sdRef);

	_devicePort = port;

	// query host record for IPv4 address
	_sdRef = ServiceDiscovery::queryService(host, kDNSServiceType_A, *this);
	ServiceDiscovery::start(_sdRef);
}


void ConnectDialogImpl::onServiceQueried(
	DNSServiceRef     sdRef,
	const std::string rrname,
	const uint16_t    rrtype,
	const uint16_t    rdlen,
	const void* const rdata,
	const uint32_t    ttl)
{
	assert(sdRef == _sdRef);
	assert(!rrname.empty());
	assert(rrtype == kDNSServiceType_A);
	assert(rdlen == 4);
	assert(rdata != 0);
	assert(ttl > 0);

	// stop service query activity
	ServiceDiscovery::stop(sdRef);

	beginSocketConnect(SocketAddress(IPAddress(rdata, rdlen), _devicePort));
}
