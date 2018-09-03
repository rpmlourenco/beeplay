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

#include "DeviceDialog.h"
#include "Dialog.h"
#include "Platform.inl"
#include "Resources.h"
#include <cassert>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <commctrl.h>
#include <windowsx.h>
#include <Poco/ByteOrder.h>
#include <Poco/Format.h>
#include <Poco/Net/SocketAddress.h>


using Poco::ByteOrder;
using Poco::Net::SocketAddress;


#define LABEL_ANY "AirPlay Device"
#define LABEL_APX "AirPort Express"
#define LABEL_ATV "Apple TV"


class DeviceDialogImpl
:
	public Dialog,
	private Uncopyable
{
	friend class DeviceDialog;

	DeviceDialogImpl();
	explicit DeviceDialogImpl(const DeviceInfo&);

	void onInitialize();
	void onCommand(WORD, WORD);

	int onMeasureItem(LPMEASUREITEMSTRUCT);
	int onDrawItem(LPDRAWITEMSTRUCT);

	void checkOkayButton();

	std::auto_ptr<const DeviceInfo> _device;
};


//------------------------------------------------------------------------------


DeviceDialog::DeviceDialog()
:
	_impl(new DeviceDialogImpl)
{
}


DeviceDialog::DeviceDialog(const DeviceInfo& device)
:
	_impl(new DeviceDialogImpl(device))
{
	if (device.isZeroConf())
		throw std::logic_error("device.isZeroConf()");
}


DeviceDialog::~DeviceDialog()
{
	delete _impl;
}


int DeviceDialog::doModal(HWND parentWindow)
{
	return _impl->doModal(DIALOG_DEVICE, parentWindow);
}


const DeviceInfo& DeviceDialog::device() const
{
	if (_impl->_device.get() == NULL)
		throw std::logic_error("_device.get() == NULL");
	return *(_impl->_device);
}


//------------------------------------------------------------------------------


DeviceDialogImpl::DeviceDialogImpl()
{
}


DeviceDialogImpl::DeviceDialogImpl(const DeviceInfo& device)
:
	_device(new DeviceInfo(device))
{
}


void DeviceDialogImpl::onInitialize()
{
	CenterWindowOverParent(_dialogWindow);

	// populate type droplist
	HWND typeList = GetDlgItem(_dialogWindow, DIALOG_DEVICE_INPUT_TYPE);
	ComboBox_AddItemData(typeList, DeviceInfo::ANY);
	ComboBox_AddItemData(typeList, DeviceInfo::APX);
	ComboBox_AddItemData(typeList, DeviceInfo::ATV);

	if (_device.get() == NULL)
	{
		// initialize type droplist value
		ComboBox_SetCurSel(typeList, ComboBox_FindItemData(typeList, -1, DeviceInfo::ANY));

		// initialize port textbox value
		Edit_SetText(GetDlgItem(_dialogWindow, DIALOG_DEVICE_INPUT_PORT), TEXT("5000"));
	}
	else
	{
		string_t name;
		Platform::Charset::fromUTF8(_device->name(), name);

		// initialize name textbox vale
		Edit_SetText(GetDlgItem(_dialogWindow, DIALOG_DEVICE_INPUT_NAME), name.c_str());

		// initialize type droplist value
		ComboBox_SetCurSel(typeList, ComboBox_FindItemData(typeList, -1, _device->type()));

		const SocketAddress address(_device->addr().first + ":" + _device->addr().second);

		// initialize IP address input value
		SendDlgItemMessage(_dialogWindow, DIALOG_DEVICE_INPUT_HOST,
			IPM_SETADDRESS, 0, ByteOrder::toNetwork(static_cast<uint32_t>(
				static_cast<const struct in_addr*>(address.host().addr())->S_un.S_addr)));

		string_t port;
		Platform::Charset::fromUTF8(Poco::format("%hu", address.port()), port);

		// initialize port textbox value
		Edit_SetText(GetDlgItem(_dialogWindow, DIALOG_DEVICE_INPUT_PORT), port.c_str());
	}

	checkOkayButton();
}


void DeviceDialogImpl::onCommand(WORD command, WORD control)
{
	if (control == IDOK && command == BN_CLICKED)
	{
		// get name textbox value
		HWND nameEdit = GetDlgItem(_dialogWindow, DIALOG_DEVICE_INPUT_NAME);
		std::vector<char_t> nameBuffer(Edit_GetTextLength(nameEdit) + 1);
		Edit_GetText(nameEdit, &nameBuffer[0], nameBuffer.size());
		std::string name;
		Platform::Charset::toUTF8(&nameBuffer[0], name);

		// get type droplist value
		HWND typeList = GetDlgItem(_dialogWindow, DIALOG_DEVICE_INPUT_TYPE);
		const DeviceInfo::DeviceType type = DeviceInfo::DeviceType(
			ComboBox_GetItemData(typeList, ComboBox_GetCurSel(typeList)));

		// get IP address input value
		DWORD addr;
		SendDlgItemMessage(_dialogWindow, DIALOG_DEVICE_INPUT_HOST,
			IPM_GETADDRESS, 0, (LPARAM) (LPDWORD) &addr);
		const std::string host(Poco::format("%?u.%?u.%?u.%?u",
			FIRST_IPADDRESS(addr), SECOND_IPADDRESS(addr),
			THIRD_IPADDRESS(addr), FOURTH_IPADDRESS(addr)));

		// get port textbox value
		HWND portEdit = GetDlgItem(_dialogWindow, DIALOG_DEVICE_INPUT_PORT);
		std::vector<char_t> portBuffer(Edit_GetTextLength(portEdit) + 1);
		Edit_GetText(portEdit, &portBuffer[0], portBuffer.size());
		std::string port;
		Platform::Charset::toUTF8(&portBuffer[0], port);

		// update associated device info and close dialog window normally
		_device.reset(
			new DeviceInfo(type, name, std::make_pair(host, port), false));
	}
	else if (control == DIALOG_DEVICE_INPUT_NAME && command == EN_CHANGE)
	{
		// enable/disable okay button when name input changes
		checkOkayButton();
	}

	Dialog::onCommand(command, control);
}


int DeviceDialogImpl::onMeasureItem(LPMEASUREITEMSTRUCT measureItemInfo)
{
	if (measureItemInfo->CtlID == DIALOG_DEVICE_INPUT_TYPE
		&& measureItemInfo->CtlType == ODT_COMBOBOX)
	{
		// device icons are 16x16; leave room for padding around icons
		measureItemInfo->itemHeight = 20;
		return 1;
	}

	return 0;
}


int DeviceDialogImpl::onDrawItem(LPDRAWITEMSTRUCT drawItemInfo)
{
	if (drawItemInfo->CtlID == DIALOG_DEVICE_INPUT_TYPE
		&& drawItemInfo->CtlType == ODT_COMBOBOX
		&& (drawItemInfo->itemAction == ODA_DRAWENTIRE
			|| drawItemInfo->itemAction == ODA_SELECT))
	{
		// save initial background and text colors
		COLORREF backColor = GetBkColor(drawItemInfo->hDC);
		COLORREF textColor = GetTextColor(drawItemInfo->hDC);

		// change background and text colors if item is selected
		if (drawItemInfo->itemState & ODS_SELECTED)
		{
			SetBkColor(drawItemInfo->hDC, GetSysColor(COLOR_HIGHLIGHT));
			SetTextColor(drawItemInfo->hDC, GetSysColor(COLOR_HIGHLIGHTTEXT));
		}

		// fill item rect with background color
		HBRUSH brush = CreateSolidBrush(GetBkColor(drawItemInfo->hDC));
		FillRect(drawItemInfo->hDC, &drawItemInfo->rcItem, brush);
		DeleteObject(brush);

		// calculate item height to aid in positioning icon and text
		LONG itemHeight = drawItemInfo->rcItem.bottom - drawItemInfo->rcItem.top;
		// subtract the height of the icon from the height of the item and split
		// the difference to make the padding around the icon even
		LONG padding = (itemHeight - 16) / 2;

		// select icon and name for device type
		uint16_t iconId = ICON_ANY;
		string_t nameSt = TEXT(LABEL_ANY);
		switch (drawItemInfo->itemData)
		{
		case DeviceInfo::APX:
			iconId = ICON_APX;
			nameSt = TEXT(LABEL_APX);
			break;
		case DeviceInfo::ATV:
			iconId = ICON_ATV;
			nameSt = TEXT(LABEL_ATV);
			break;
		}

		if (iconId)
		{
			extern HINSTANCE dllInstance;
			HICON icon = (HICON) LoadImage(dllInstance, MAKEINTRESOURCE(iconId),
				IMAGE_ICON, 0, 0, LR_SHARED);
			// draw icon with padding
			DrawIconEx(drawItemInfo->hDC, drawItemInfo->rcItem.left + padding,
				drawItemInfo->rcItem.top + padding, icon, 0, 0, 0, NULL, DI_NORMAL);
		}

		if (!nameSt.empty())
		{
			TEXTMETRIC tm;
			GetTextMetrics(drawItemInfo->hDC, &tm);
			// draw device name string to the right of the icon
			TextOut(drawItemInfo->hDC, drawItemInfo->rcItem.left + itemHeight + padding,
				(drawItemInfo->rcItem.bottom + drawItemInfo->rcItem.top - tm.tmHeight) / 2,
				nameSt.c_str(), nameSt.length());
		}

		// restore device context to its initial state
		SetBkColor(drawItemInfo->hDC, backColor);
		SetTextColor(drawItemInfo->hDC, textColor);

		return 1;
	}

	return 0;
}


void DeviceDialogImpl::checkOkayButton()
{
	// require a value in name input before enabling okay button
	EnableWindow(GetDlgItem(_dialogWindow, IDOK),
		Edit_GetTextLength(GetDlgItem(_dialogWindow, DIALOG_DEVICE_INPUT_NAME)));
}
