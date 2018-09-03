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

#include "Dialog.h"
#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <windowsx.h>


Poco::ThreadLocal<Dialog*> Dialog::_dialog;


INT_PTR CALLBACK Dialog::dialogProc(HWND dialogWindow, UINT message, WPARAM wParam, LPARAM lParam)
{
	// get dialog object pointer from window user data slot
	Dialog* dialog = (Dialog*) GetWindowLongPtr(dialogWindow, GWLP_USERDATA);

	switch (message)
	{
	case WM_SETFONT:
		if (dialog == NULL)
		{
			// take dialog object pointer from thread-local storage
			std::swap(dialog, _dialog.get());
			// save dialog object pointer in window user data slot
			SetWindowLongPtr(dialogWindow, GWLP_USERDATA, (LONG_PTR) dialog);
			// save dialog window handle in dialog object for use by its methods
			dialog->_dialogWindow = dialogWindow;
		}
		break;

	case WM_NCDESTROY:
		dialog->_dialogWindow = NULL;
		dialog->_dialogType = UNKNOWN;
		break;

	case WM_INITDIALOG:
		dialog->onInitialize();
		return TRUE;

	case WM_DESTROY:
		dialog->onFinalize();
		break;

	case WM_TIMER:
		dialog->onTimer();
		break;

	case WM_COMMAND:
		dialog->onCommand(HIWORD(wParam), LOWORD(wParam));
		break;

	case WM_CONTEXTMENU:
		{
			POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			dialog->onContextMenu((HWND) wParam, point);
		}
		break;

	case WM_COMPAREITEM:
		return dialog->onCompareItem((LPCOMPAREITEMSTRUCT) lParam);

	case WM_MEASUREITEM:
		return dialog->onMeasureItem((LPMEASUREITEMSTRUCT) lParam);

	case WM_DRAWITEM:
		return dialog->onDrawItem((LPDRAWITEMSTRUCT) lParam);

	case WM_SHOWWINDOW:
		if (!dialog->_startVisible && dialog->_hideProgress == 0 && wParam == 1)
		{
			dialog->_hideProgress += 1;
		}
		break;

	case WM_WINDOWPOSCHANGING:
		if (!dialog->_startVisible && dialog->_hideProgress == 1)
		{
			// hide dialog window until ShowWindow(..., SW_SHOW)
			((LPWINDOWPOS) lParam)->flags &= ~SWP_SHOWWINDOW;
			dialog->_hideProgress += 1;
		}
		break;
	}

	return FALSE;
}


//------------------------------------------------------------------------------


Dialog::Dialog()
:
	_dialogWindow(NULL),
	_dialogType(UNKNOWN),
	_startVisible(TRUE),
	_hideProgress(0)
{
}


Dialog::~Dialog()
{
	if (_dialogWindow != NULL)
	{
		endDialog(IDCANCEL);
	}
}


int Dialog::doModal(UINT templateId, HWND parentWindow, BOOL startVisible)
{
	if (_dialogWindow == NULL)
	{
		_dialog.get() = this;
		_dialogType = MODAL;
		_startVisible = startVisible;
		extern HINSTANCE dllInstance;
		return DialogBox(dllInstance, MAKEINTRESOURCE(templateId), parentWindow, Dialog::dialogProc);
	}
	else
	{
		throw std::runtime_error("dialogWindow != NULL");
	}
}


void Dialog::doModeless(UINT templateId, HWND parentWindow, BOOL startVisible)
{
	if (_dialogWindow == NULL)
	{
		_dialog.get() = this;
		_dialogType = MODELESS;
		_startVisible = startVisible;
		extern HINSTANCE dllInstance;
		HWND dialog = CreateDialog(dllInstance, MAKEINTRESOURCE(templateId), parentWindow, Dialog::dialogProc);
		ShowWindow(dialog, SW_SHOW);
	}
	else
	{
		BringWindowToTop(_dialogWindow);
	}
}


void Dialog::endDialog(const int result)
{
	switch (_dialogType)
	{
	case MODAL:
		EndDialog(_dialogWindow, result);
		break;

	case MODELESS:
		DestroyWindow(_dialogWindow);
		break;
	}
}


void Dialog::onInitialize()
{
}


void Dialog::onFinalize()
{
}


void Dialog::onTimer()
{
}


void Dialog::onCommand(WORD command, WORD control)
{
	switch (control)
	{
	case IDCANCEL:
		if (command == BN_CLICKED)
		{
			endDialog(1);
		}
		break;

	case IDOK:
		if (command == BN_CLICKED)
		{
			endDialog(0);
		}
		break;
	}
}


void Dialog::onContextMenu(HWND window, POINT point)
{
}


int Dialog::onCompareItem(LPCOMPAREITEMSTRUCT compareItemInfo)
{
	return 0;
}


int Dialog::onMeasureItem(LPMEASUREITEMSTRUCT measureItemInfo)
{
	return 0;
}


int Dialog::onDrawItem(LPDRAWITEMSTRUCT drawItemInfo)
{
	return 0;
}
