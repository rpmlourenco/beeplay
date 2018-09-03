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

#include "WinampHooks.h"
#include "Platform.inl"
#include "Debugger.h"
#include "Plugin.h"
#include <cassert>
#include <cstring>


#define LABEL_POPUP_MENU L"Remote Speakers"
#define LABEL_NO_DEVICES L"(none found)"

static const UINT ID_POPUP_MENU = Plugin::id();
static const BYTE ID_POPUP_MENU_ITEM = 222;


#pragma region winamp_hooks_callbacks

WinampHooks* WinampHooks::_this = NULL;


WinampHooks::WinampHooks(HWND playerWindow)
:
	_playerWindow(playerWindow),
	_callWindowProcHook(NULL),
	_getMessageProcHook(NULL),
	_popupMenu(NULL),
	_itemCount(0)
{
	_this = this;

	// hook into call window chain
	_callWindowProcHook = SetWindowsHookEx(WH_CALLWNDPROC,
		WinampHooks::callWindowProc, NULL, GetCurrentThreadId());
	if (_callWindowProcHook == NULL)
	{
		Debugger::printLastError("SetWindowsHook WH_CALLWNDPROC", __FILE__, __LINE__);
	}

	// hook into get message chain
	_getMessageProcHook = SetWindowsHookEx(WH_GETMESSAGE,
		WinampHooks::getMessageProc, NULL, GetCurrentThreadId());
	if (_getMessageProcHook == NULL)
	{
		Debugger::printLastError("SetWindowsHook WH_GETMESSAGE", __FILE__, __LINE__);
		UnhookWindowsHookEx(_callWindowProcHook);
	}
}


WinampHooks::~WinampHooks()
{
	// unhook from call window chain
	if (_callWindowProcHook != NULL && UnhookWindowsHookEx(_callWindowProcHook) == 0)
	{
		Debugger::printLastError("UnhookWindowsHook WH_CALLWNDPROC", __FILE__, __LINE__);
	}

	// unhook from get message chain
	if (_getMessageProcHook != NULL && UnhookWindowsHookEx(_getMessageProcHook) == 0)
	{
		Debugger::printLastError("UnhookWindowsHook WH_GETMESSAGE", __FILE__, __LINE__);
	}

	_this = NULL;

	destroyPopupMenu();
}


LRESULT CALLBACK WinampHooks::callWindowProc(const int code, WPARAM wParam, LPARAM lParam)
{
	LPCWPSTRUCT msg;

	if (code < 0)
	{
		return CallNextHookEx(_this->_callWindowProcHook, code, wParam, lParam);
	}

	msg = (LPCWPSTRUCT) lParam;

	// verify call is associated with the player's window before processing
	if (msg->hwnd == _this->_playerWindow
		&& _this->processMessage(msg->message, msg->wParam, msg->lParam) == 0)
	{
		return 0;
	}

	return CallNextHookEx(_this->_callWindowProcHook, code, wParam, lParam);
}


LRESULT CALLBACK WinampHooks::getMessageProc(const int code, WPARAM wParam, LPARAM lParam)
{
	LPMSG msg;

	if (code < 0)
	{
		return CallNextHookEx(_this->_getMessageProcHook, code, wParam, lParam);
	}

	msg = (LPMSG) lParam;

	// verify message is associated with the player's window before processing
	if (msg->hwnd == _this->_playerWindow
		&& _this->processMessage(msg->message, msg->wParam, msg->lParam) == 0)
	{
		return 0;
	}

	return CallNextHookEx(_this->_getMessageProcHook, code, wParam, lParam);
}


LRESULT WinampHooks::processMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_INITMENUPOPUP:
	case WM_UNINITMENUPOPUP:
		{
			HMENU menu = (HMENU) wParam;
			HMENU mainMenu = (HMENU) SendMessage(_playerWindow, WM_USER, 0, 281);
			HMENU optionsMenu = (HMENU) SendMessage(_playerWindow, WM_USER, 3, 281);

			// verify message is for the correct player menu before processing
			if (menu == mainMenu || menu == optionsMenu)
			{
				if (message == WM_INITMENUPOPUP)
				{
					attachPopupMenu(menu,
						GetMenuItemCount(menu) - (menu == mainMenu ? 5 : 2));
				}
				else if (message == WM_UNINITMENUPOPUP)
				{
					detachPopupMenu(menu);
				}
			}
		}
		break;

	case WM_COMMAND:
	case WM_SYSCOMMAND:
		if (HIWORD(wParam) == 0 && LOBYTE(LOWORD(wParam)) == ID_POPUP_MENU_ITEM)
		{
			// popup menu is destroyed at this point, so look up the device name
			const std::string& name(_names[HIBYTE(LOWORD(wParam))]);

			// toggle activation state
			_devices.toggle(name);
		}
		break;
	}

	return TRUE;
}
#pragma endregion


void WinampHooks::attachPopupMenu(HMENU parentMenu, const int index)
{
	_names.clear();
	destroyPopupMenu();

	_popupMenu = CreatePopupMenu();
	if (_popupMenu == NULL)
	{
		Debugger::printLastError("CreatePopupMenu", __FILE__, __LINE__);
	}

	const DeviceUtils::InfoListPtr devices = _devices.enumerate();
	if (!devices->empty())
	{
		typedef DeviceUtils::InfoList::const_iterator devices_iterator;
		for (devices_iterator it = devices->begin(); it != devices->end(); ++it)
		{
			const DeviceUtils::InfoList::value_type& device = *it;
			_names.push_back(device.display_name); // save for menu item selection
			insertPopupMenuItem(device.display_name, device.activated, device.available);
		}
	}
	else
	{
		// insert dummy item into popup menu
		MENUITEMINFO item;
		std::memset(&item, 0, sizeof(MENUITEMINFO));
		item.cbSize = sizeof(MENUITEMINFO);
		item.fMask = MIIM_FTYPE | MIIM_STRING | MIIM_STATE;
		item.fType = MFT_STRING;
		item.dwTypeData = LABEL_NO_DEVICES;
		item.fState = MFS_DISABLED;
		if (InsertMenuItemW(_popupMenu, 0, TRUE, &item) == 0)
		{
			Debugger::printLastError("InsertMenuItem", __FILE__, __LINE__);
		}
	}

	// insert popup menu into player's menu
	MENUITEMINFO item;
	std::memset(&item, 0, sizeof(MENUITEMINFO));
	item.cbSize = sizeof(MENUITEMINFO);
	item.fMask = MIIM_ID | MIIM_FTYPE | MIIM_STRING | MIIM_STATE | MIIM_SUBMENU;
	item.wID = ID_POPUP_MENU;
	item.fType = MFT_STRING;
	item.dwTypeData = LABEL_POPUP_MENU;
	item.fState = MFS_ENABLED;
	item.hSubMenu = _popupMenu;
	if (InsertMenuItemW(parentMenu, index, TRUE, &item) == 0)
	{
		Debugger::printLastError("InsertMenuItem", __FILE__, __LINE__);
	}
}


void WinampHooks::detachPopupMenu(HMENU parentMenu)
{
	if (RemoveMenu(parentMenu, ID_POPUP_MENU, MF_BYCOMMAND) == 0)
	{
		Debugger::printLastError("RemoveMenu", __FILE__, __LINE__);
	}

	destroyPopupMenu();
}


void WinampHooks::destroyPopupMenu()
{
	if (_popupMenu != NULL)
	{
		if (DestroyMenu(_popupMenu) == 0)
		{
			Debugger::printLastError("DestroyMenu", __FILE__, __LINE__);
		}
		_popupMenu = NULL;
	}

	_itemCount = 0;
}


void WinampHooks::insertPopupMenuItem(
	const std::string& label, const bool check, const bool stale)
{
	string_t label2;  Platform::Charset::fromUTF8(label, label2);

	MENUITEMINFO item;
	std::memset(&item, 0, sizeof(MENUITEMINFO));
	item.cbSize = sizeof(MENUITEMINFO);

	item.fMask |= MIIM_ID;
	item.wID = MAKEWORD(ID_POPUP_MENU_ITEM, _itemCount);

	item.fMask |= MIIM_FTYPE;
	item.fType = MFT_STRING;

	item.fMask |= MIIM_STRING;
	item.dwTypeData = (LPTSTR) label2.c_str();

	item.fMask |= MIIM_STATE;
	item.fState = MFS_ENABLED;
	if (check)
		item.fState |= MFS_CHECKED;
	if (stale)
		item.fState |= MFS_GRAYED;

	if (InsertMenuItem(_popupMenu, item.wID, FALSE, &item) == 0)
	{
		Debugger::printLastError("InsertMenuItem", __FILE__, __LINE__);
	}

	_itemCount += 1;

	// scheme of using LOBYTE of item id for constant ID_POPUP_MENU_ITEM and 
	// HIBYTE of item id for "unique item id" works only if there are fewer than
	// 255 remote speakers added; it is assumed to be a very rare case that the
	// user has more than 255 remote speakers base stations or that devices are
	// coming in and out of service so frequently that the counter wraps
	assert(_itemCount > 0 && _itemCount <= 255);
}
