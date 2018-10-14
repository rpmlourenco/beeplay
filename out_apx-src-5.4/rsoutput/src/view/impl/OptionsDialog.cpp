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
#include "DeviceDialog.h"
#include "DeviceDiscovery.h"
#include "DeviceNotification.h"
#include "Dialog.h"
#include "MessageDialog.h"
#include "Options.h"
#include "OptionsDialog.h"
#include "Platform.inl"
#include "Resources.h"
#include <cassert>
#include <string>
#include <utility>
#include <vector>
#include <windowsx.h>
#include <Poco/Format.h>
#include <Poco/Observer.h>


using Poco::Observer;


#define LABEL_ADDDEVICE "Add device..."
#define LABEL_EDTDEVICE "Edit device..."
#define LABEL_EDTDEVNAM "Edit device \"%s\"..."
#define LABEL_REMDEVICE "Remove device"
#define LABEL_REMDEVNAM "Remove device \"%s\""

#define ERROR_DUPDEVICE "A device named \"%s\" already exists in the list."
#define ERROR_DUPADDRES "A device with the same address already exists in the list."

#define LISTBOX_ITEM_HEIGHT 24 // device icons are 16x16; leave room for padding


class OptionsDialogImpl
:
	public Dialog,
	public DeviceDiscovery::Listener,
	private Uncopyable
{
	friend class OptionsDialog;

	OptionsDialogImpl();
	~OptionsDialogImpl();

	void doStatusUpdate();
	bool isDirty() const;

	void onInitialize();
	void onFinalize();

	void onCommand(WORD, WORD);
	void onContextMenu(HWND, POINT);

	int onCompareItem(LPCOMPAREITEMSTRUCT);
	int onMeasureItem(LPMEASUREITEMSTRUCT);
	int onDrawItem(LPDRAWITEMSTRUCT);

	void onDeviceChanged(DeviceNotification*);
	void onDeviceFound(const DeviceInfo&);
	void onDeviceLost(const DeviceInfo&);

	HWND listbox() const;

	void insertListboxItem(const DeviceInfo&, bool, bool = false);
	bool removeListboxItem(const DeviceInfo&);
	void populateListbox(const DeviceInfoSet&);
	void enumerateListboxItems(Options::SharedPtr) const;
	void finalizeListbox();

	Options::SharedPtr buildOptions() const;

	Observer<OptionsDialogImpl,DeviceNotification> _observer;
	OptionsDialog::StatusCallback _statusCallback;
};


// small object for storing additional state in each listbox item
struct ListboxItemData : private std::pair<DeviceInfo,bool>
{
	ListboxItemData(const DeviceInfo& dev, const bool stl) : pair(dev, stl) { }
	const DeviceInfo& device() const { return first; }
	bool isStale() const { return second; }
};


//------------------------------------------------------------------------------


OptionsDialog::OptionsDialog()
:
	_impl(new OptionsDialogImpl)
{
}


OptionsDialog::~OptionsDialog()
{
	delete _impl;
}


void OptionsDialog::doModeless(HWND parentWindow)
{
	_impl->doModeless(DIALOG_OPTIONS, parentWindow);
}


BOOL OptionsDialog::isDialogMessage(LPMSG pMsg)
{
	return IsDialogMessage(_impl->_dialogWindow, pMsg);
}


void OptionsDialog::doApply()
{
	// change active options to those specified by dialog
	Options::setOptions(_impl->buildOptions());

	// update now that active options match dialog options
	_impl->doStatusUpdate();
}


bool OptionsDialog::isDirty() const
{
	return _impl->isDirty();
}


void OptionsDialog::setStatusCallback(StatusCallback callback)
{
	_impl->_statusCallback = callback;
}


//------------------------------------------------------------------------------


OptionsDialogImpl::OptionsDialogImpl()
:
	_observer(*this, &OptionsDialogImpl::onDeviceChanged)
{
}


OptionsDialogImpl::~OptionsDialogImpl()
{
	onFinalize();
}


void OptionsDialogImpl::doStatusUpdate()
{
	if (_statusCallback)
	{
		_statusCallback(isDirty());
	}
}


bool OptionsDialogImpl::isDirty() const
{
	return (!(*buildOptions() == *Options::getOptions()));
}


static BOOL CALLBACK setFont(HWND hWnd, LPARAM lParam)
{
	HFONT hFont = (HFONT) lParam;
	if (hFont)
	{
		SendMessage(hWnd, WM_SETFONT, (WPARAM) hFont, 0);
		return TRUE;
	}
	return FALSE;
}


void OptionsDialogImpl::onInitialize()
{
	RECT dlgRect;  GetWindowRect(_dialogWindow, &dlgRect);
	RECT encRect;  GetWindowRect(GetParent(_dialogWindow), &encRect);

	// verify dialog is aligned with top and left edges of the parent
	assert(dlgRect.top == encRect.top && dlgRect.left == encRect.left);
	// verify parent window is large enough to host the dialog without clipping
	assert(WIDTH(encRect) >= WIDTH(dlgRect) && HEIGHT(encRect) >= HEIGHT(dlgRect));

	LONG widthDiff = encRect.right - dlgRect.right;
	LONG heightDiff = encRect.bottom - dlgRect.bottom;
	if (widthDiff >= 0 && heightDiff >= 0 && (widthDiff > 0 || heightDiff > 0))
	{
		// stretch dialog window to fit the enclosing window completely
		SetWindowPos(_dialogWindow, 0, 0, 0, WIDTH(dlgRect) + widthDiff,
			HEIGHT(dlgRect) + heightDiff, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOZORDER);

		// expand the listbox vertically if space permits
		heightDiff -= (heightDiff % LISTBOX_ITEM_HEIGHT);
		ResizeDlgItem(_dialogWindow, DIALOG_OPTIONS_LISTBOX_SPEAKERS, widthDiff, heightDiff);
		RepositionDlgItem(_dialogWindow, DIALOG_OPTIONS_CHECKBOX_VOLCONTROL, 0, heightDiff);
		RepositionDlgItem(_dialogWindow, DIALOG_OPTIONS_CHECKBOX_PLYCONTROL, 0, heightDiff);
		RepositionDlgItem(_dialogWindow, DIALOG_OPTIONS_CHECKBOX_RESETONPAUSE, 0, heightDiff);
	}

	// set font of dialog controls to the enclosing window's font
	HWND enclosingWindow = GetParent(GetParent(_dialogWindow));
	EnumChildWindows(_dialogWindow, setFont,
		(LPARAM) (HFONT) SendMessage(enclosingWindow, WM_GETFONT, 0, 0));

	// hold reference to active options
	const Options::SharedPtr options = Options::getOptions();

	// set volume and balance control checkbox value
	CheckDlgButton(_dialogWindow, DIALOG_OPTIONS_CHECKBOX_VOLCONTROL,
		(options->getVolumeControl() ? BST_CHECKED : BST_UNCHECKED));

	// set player control checkbox value
	CheckDlgButton(_dialogWindow, DIALOG_OPTIONS_CHECKBOX_PLYCONTROL,
		(options->getPlayerControl() ? BST_CHECKED : BST_UNCHECKED));

	// set reset on pause checkbox value
	CheckDlgButton(_dialogWindow, DIALOG_OPTIONS_CHECKBOX_RESETONPAUSE,
		(options->getResetOnPause() ? BST_CHECKED : BST_UNCHECKED));

	populateListbox(options->devices());

	doStatusUpdate();

	DeviceDiscovery::browseDevices(*this);
	Options::addObserver(_observer);
}


void OptionsDialogImpl::onFinalize()
{
	Options::removeObserver(_observer);
	DeviceDiscovery::stopBrowsing(*this);

	finalizeListbox();
}


void OptionsDialogImpl::onCommand(WORD command, WORD control)
{
	switch (control)
	{
	case DIALOG_OPTIONS_CHECKBOX_VOLCONTROL:
	case DIALOG_OPTIONS_CHECKBOX_PLYCONTROL:
	case DIALOG_OPTIONS_CHECKBOX_RESETONPAUSE:
		if (command == BN_CLICKED)
		{
			// update when checkbox is checked/unchecked
			doStatusUpdate();
		}
		break;

	case DIALOG_OPTIONS_LISTBOX_SPEAKERS:
		if (command == LBN_SELCHANGE)
		{
			// update when listbox selection changes
			doStatusUpdate();
		}
		break;
	}
}


void OptionsDialogImpl::onContextMenu(HWND window, POINT point)
{
	if (window == listbox())
	{
		// determine index of item associated with context menu event
		int index = -1;

		if (point.x == -1 && point.y == -1)
		{
			// context menu key was pressed so use caret index
			index = ListBox_GetCaretIndex(window);

			// fill in point data for item under caret
			RECT rect;
			const int result = ListBox_GetItemRect(window, index, &rect);
			if (result == LB_ERR)
			{
				assert(result != LB_ERR);
			}
			point.x = rect.left;
			point.y = rect.top;
			ClientToScreen(window, &point);
		}
		else
		{
			// translate point to item index
			ScreenToClient(window, &point);
			LRESULT result = SendMessage(window, LB_ITEMFROMPOINT, 0,
				MAKELPARAM(point.x, point.y));
			if (HIWORD(result) == 0)
			{
				index = LOWORD(result);
			}
			ClientToScreen(window, &point);
		}

		const ListboxItemData* itemData = NULL;

		LRESULT result = ListBox_GetItemData(window, index);
		if (result != LB_ERR)
		{
			itemData = reinterpret_cast<const ListboxItemData*>(result);
		}

		// create the context menu
		HMENU contextMenu = CreatePopupMenu();

		enum ContextMenuCommand
		{
			ADD = 1,
			EDIT,
			REMOVE
		};

		MENUITEMINFO item;

		std::memset(&item, 0, sizeof(MENUITEMINFO));
		item.cbSize = sizeof(MENUITEMINFO);
		item.fMask = MIIM_ID | MIIM_FTYPE | MIIM_STRING | MIIM_STATE;
		item.wID = ADD;
		item.fType = MFT_STRING;
		item.dwTypeData = TEXT(LABEL_ADDDEVICE);
		item.fState = MFS_ENABLED;
		if (InsertMenuItem(contextMenu, GetMenuItemCount(contextMenu), TRUE,
			&item) == 0)
		{
			Debugger::printLastError("InsertMenuItem", __FILE__, __LINE__);
		}

		string_t editLabel;
		Platform::Charset::fromUTF8(
			(itemData == NULL
				? LABEL_EDTDEVICE
				: Poco::format(LABEL_EDTDEVNAM, (*itemData).device().name())),
			editLabel);

		std::memset(&item, 0, sizeof(MENUITEMINFO));
		item.cbSize = sizeof(MENUITEMINFO);
		item.fMask = MIIM_ID | MIIM_FTYPE | MIIM_STRING | MIIM_STATE;
		item.wID = EDIT;
		item.fType = MFT_STRING;
		item.dwTypeData = (LPTSTR) editLabel.c_str();
		item.fState = (itemData == NULL || (*itemData).device().isZeroConf()
			? MFS_DISABLED : MFS_ENABLED);
		if (InsertMenuItem(contextMenu, GetMenuItemCount(contextMenu), TRUE,
			&item) == 0)
		{
			Debugger::printLastError("InsertMenuItem", __FILE__, __LINE__);
		}

		string_t removeLabel;
		Platform::Charset::fromUTF8(
			(itemData == NULL
				? LABEL_REMDEVICE
				: Poco::format(LABEL_REMDEVNAM, (*itemData).device().name())),
			removeLabel);

		std::memset(&item, 0, sizeof(MENUITEMINFO));
		item.cbSize = sizeof(MENUITEMINFO);
		item.fMask = MIIM_ID | MIIM_FTYPE | MIIM_STRING | MIIM_STATE;
		item.wID = REMOVE;
		item.fType = MFT_STRING;
		item.dwTypeData = (LPTSTR) removeLabel.c_str();
		item.fState = (itemData == NULL || (*itemData).device().isZeroConf()
			? MFS_DISABLED : MFS_ENABLED);
		if (InsertMenuItem(contextMenu, GetMenuItemCount(contextMenu), TRUE,
			&item) == 0)
		{
			Debugger::printLastError("InsertMenuItem", __FILE__, __LINE__);
		}

		// run the context menu and execute selected command
		switch(TrackPopupMenu(contextMenu, TPM_NONOTIFY | TPM_RETURNCMD,
			point.x, point.y, 0, window, NULL))
		{
		case ADD:
			{
				DeviceDialog deviceDialog;
				if (deviceDialog.doModal(_dialogWindow) == 0)
				{
					insertListboxItem(deviceDialog.device(), false);
					doStatusUpdate();
				}
			}
			break;

		case EDIT:
			assert(itemData != NULL);
			{
				DeviceDialog deviceDialog((*itemData).device());
				if (deviceDialog.doModal(_dialogWindow) == 0)
				{
					const bool selected = removeListboxItem((*itemData).device());
					insertListboxItem(deviceDialog.device(), selected);
					doStatusUpdate();
				}
			}
			break;

		case REMOVE:
			assert(itemData != NULL);
			removeListboxItem((*itemData).device());
			doStatusUpdate();
			break;
		}

		DestroyMenu(contextMenu);
	}
}


int OptionsDialogImpl::onCompareItem(LPCOMPAREITEMSTRUCT compareItemInfo)
{
	if (compareItemInfo->CtlID == DIALOG_OPTIONS_LISTBOX_SPEAKERS
		&& compareItemInfo->CtlType == ODT_LISTBOX)
	{
		const DeviceInfo& lhs =
			reinterpret_cast<const ListboxItemData*>(compareItemInfo->itemData1)->device();
		const DeviceInfo& rhs =
			reinterpret_cast<const ListboxItemData*>(compareItemInfo->itemData2)->device();

		if (lhs.name() < rhs.name())
		{
			return -1;
		}
		else if (lhs.name() > rhs.name())
		{
			return 1;
		}
	}

	return 0;
}


int OptionsDialogImpl::onMeasureItem(LPMEASUREITEMSTRUCT measureItemInfo)
{
	if (measureItemInfo->CtlID == DIALOG_OPTIONS_LISTBOX_SPEAKERS
		&& measureItemInfo->CtlType == ODT_LISTBOX)
	{
		measureItemInfo->itemHeight = LISTBOX_ITEM_HEIGHT;
		return 1;
	}

	return 0;
}


int OptionsDialogImpl::onDrawItem(LPDRAWITEMSTRUCT drawItemInfo)
{
	if (drawItemInfo->CtlID == DIALOG_OPTIONS_LISTBOX_SPEAKERS
		&& drawItemInfo->CtlType == ODT_LISTBOX
		&& drawItemInfo->itemID != (UINT) -1
		&& (drawItemInfo->itemAction == ODA_DRAWENTIRE
			|| drawItemInfo->itemAction == ODA_SELECT))
	{
		const ListboxItemData* const itemData =
			reinterpret_cast<const ListboxItemData*>(drawItemInfo->itemData);

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

		// select icon for device type
		uint16_t iconId = ICON_ANY;
		switch ((*itemData).device().type())
		{
		case DeviceInfo::APX:
			iconId = ICON_APX;
			break;
		case DeviceInfo::AP2:
			iconId = ICON_APX;
			break;
		case DeviceInfo::ATV:
			iconId = ICON_ATV;
			break;
		case DeviceInfo::AVR:
			iconId = ICON_AVR;
			break;
		case DeviceInfo::AFS:
			iconId = ICON_AFS;
			break;
		case DeviceInfo::AS3:
		case DeviceInfo::AS4:
			iconId = ICON_ASV;
		}

		if (iconId)
		{
			extern HINSTANCE dllInstance;
			HICON icon = (HICON) LoadImage(dllInstance, MAKEINTRESOURCE(iconId),
				IMAGE_ICON, 0, 0, LR_SHARED);
			// subtract the height of the icon from the height of the item and
			// split the difference to make the padding around the icon even
			LONG padding = (itemHeight - 16) / 2;
			// draw icon with padding
			DrawIconEx(drawItemInfo->hDC, drawItemInfo->rcItem.left + padding,
				drawItemInfo->rcItem.top + padding, icon, 0, 0, 0, NULL, DI_NORMAL);
		}

		TEXTMETRIC tm;
		GetTextMetrics(drawItemInfo->hDC, &tm);

		// change text style if device record is stale so it stands out
		HFONT dialogFont = NULL;
		if ((*itemData).isStale())
		{
			/*const int length = GetTextFace(drawItemInfo->hDC, 0, NULL);
			std::vector<char_t> textFace(length);
			GetTextFace(drawItemInfo->hDC, textFace.size(), &textFace[0]);
			// create an italicized font that is based on current font
			HFONT italicFont = CreateFont(
				tm.tmHeight, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, &textFace[0]);
			if (italicFont != NULL)
				dialogFont = (HFONT) SelectObject(drawItemInfo->hDC, italicFont);*/
			SetTextColor(drawItemInfo->hDC, GetSysColor(COLOR_GRAYTEXT));
		}

		string_t name;
		Platform::Charset::fromUTF8((*itemData).device().name(), name);

		// draw device name string to the right of the icon
		TextOut(drawItemInfo->hDC, drawItemInfo->rcItem.left + itemHeight,
			(drawItemInfo->rcItem.bottom + drawItemInfo->rcItem.top - tm.tmHeight) / 2,
			name.c_str(), name.size());

		// restore device context to its initial state
		SetBkColor(drawItemInfo->hDC, backColor);
		SetTextColor(drawItemInfo->hDC, textColor);
		if (dialogFont != NULL)
			SelectObject(drawItemInfo->hDC, dialogFont);

		return 1;
	}

	return 0;
}


void OptionsDialogImpl::onDeviceChanged(DeviceNotification* const notification)
{
	if (_dialogWindow != NULL
		&& (notification->changeType() == DeviceNotification::ACTIVATE
			|| notification->changeType() == DeviceNotification::DEACTIVATE))
	{
		const DeviceInfo& device = notification->deviceInfo();
		const int index = ListBox_FindStringExact(listbox(), -1, &device);
		if (index != LB_ERR)
		{
			BOOL selected =
				(notification->changeType() == DeviceNotification::ACTIVATE);
			const int result = ListBox_SetSel(listbox(), selected, index);
			if (result == LB_ERR)
			{
				assert(result != LB_ERR);
			}
			doStatusUpdate();
		}
	}
}


void OptionsDialogImpl::onDeviceFound(const DeviceInfo& device)
{
	const bool selected = removeListboxItem(device);
	insertListboxItem(device, selected);
	doStatusUpdate();
}


void OptionsDialogImpl::onDeviceLost(const DeviceInfo& device)
{
	const bool selected = removeListboxItem(device);
	if (selected)
	{
		// insert a stale item to retain visibility of selected item
		insertListboxItem(device, true, true);
	}
	doStatusUpdate();
}


//------------------------------------------------------------------------------


HWND OptionsDialogImpl::listbox() const
{
	return GetDlgItem(_dialogWindow, DIALOG_OPTIONS_LISTBOX_SPEAKERS);
}


void OptionsDialogImpl::insertListboxItem(const DeviceInfo& device,
	const bool select, const bool stale)
{
	// check for conflicts
	for (int index = 0, count = ListBox_GetCount(listbox()); index < count; ++index)
	{
		LRESULT result = ListBox_GetItemData(listbox(), index);
		if (result == LB_ERR)
		{
			assert(result != LB_ERR);
		}

		const DeviceInfo& dev = (*reinterpret_cast<const ListboxItemData*>(result)).device();

		if (device.name() == dev.name())
		{
			MessageDialog(
				Poco::format(ERROR_DUPDEVICE, device.name()), MB_ICONERROR
			).doModal(_dialogWindow);
			return;
		}

		if (device.addr() == dev.addr())
		{
			MessageDialog(
				ERROR_DUPADDRES, MB_ICONERROR
			).doModal(_dialogWindow);
			return;
		}
	}

	const int index = ListBox_AddString(listbox(), new ListboxItemData(device, stale));
	if (index == LB_ERR)
	{
		assert(index != LB_ERR);
	}

	const int result = ListBox_SetSel(listbox(), (select ? TRUE : FALSE), index);
	if (result == LB_ERR)
	{
		assert(result != LB_ERR);
	}
}


bool OptionsDialogImpl::removeListboxItem(const DeviceInfo& device)
{
	int selected = 0;

	int index = ListBox_FindStringExact(listbox(), -1, &device);
	if (index != LB_ERR)
	{
		selected = ListBox_GetSel(listbox(), index);
		if (selected == LB_ERR)
		{
			assert(selected != LB_ERR);
		}

		LRESULT result = ListBox_GetItemData(listbox(), index);
		if (result == LB_ERR)
		{
			assert(result != LB_ERR);
		}

		// delete item data before removing item from listbox
		delete reinterpret_cast<const ListboxItemData*>(result);

		index = ListBox_DeleteString(listbox(), index);
		if (index == LB_ERR)
		{
			assert(index != LB_ERR);
		}
	}

	return (selected > 0);
}


void OptionsDialogImpl::populateListbox(const DeviceInfoSet& devices)
{
	for (DeviceInfoSet::const_iterator it = devices.begin();
		it != devices.end(); ++it)
	{
		const DeviceInfo& device = *it;
		insertListboxItem(device,
			Options::getOptions()->isActivated(device.name()), device.isZeroConf());
	}
}


void OptionsDialogImpl::enumerateListboxItems(Options::SharedPtr options) const
{
	for (int index = 0, count = ListBox_GetCount(listbox()); index < count; ++index)
	{
		LRESULT result = ListBox_GetItemData(listbox(), index);
		if (result == LB_ERR)
		{
			assert(result != LB_ERR);
		}

		const ListboxItemData* const itemData = reinterpret_cast<const ListboxItemData*>(result);

		const int selected = ListBox_GetSel(listbox(), index);
		if (selected == LB_ERR)
		{
			assert(selected != LB_ERR);
		}

		// save only selected or manually-created devices
		if (selected > 0 || !(*itemData).device().isZeroConf())
		{
			options->devices().insert((*itemData).device());
			options->setActivated((*itemData).device().name(), (selected > 0));
		}
	}
}


void OptionsDialogImpl::finalizeListbox()
{
	int count = ListBox_GetCount(listbox());
	while (count > 0)
	{
		LRESULT result = ListBox_GetItemData(listbox(), 0);
		if (result == LB_ERR)
		{
			assert(result != LB_ERR);
		}

		// delete item data before removing item from listbox
		delete reinterpret_cast<const ListboxItemData*>(result);

		count = ListBox_DeleteString(listbox(), 0);
		if (count == LB_ERR)
		{
			assert(count != LB_ERR);
		}
	}
}


Options::SharedPtr OptionsDialogImpl::buildOptions() const
{
	Options::SharedPtr opts = new Options;

	// get volume control checkbox value
	opts->setVolumeControl(IsDlgButtonChecked(_dialogWindow,
		DIALOG_OPTIONS_CHECKBOX_VOLCONTROL) == BST_CHECKED);

	// get player control checkbox value
	opts->setPlayerControl(IsDlgButtonChecked(_dialogWindow,
		DIALOG_OPTIONS_CHECKBOX_PLYCONTROL) == BST_CHECKED);

	// get reset on pause checkbox value
	opts->setResetOnPause(IsDlgButtonChecked(_dialogWindow,
		DIALOG_OPTIONS_CHECKBOX_RESETONPAUSE) == BST_CHECKED);

	// populate device info
	enumerateListboxItems(opts);

	const Options::SharedPtr options = Options::getOptions();

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

	return opts;
}
