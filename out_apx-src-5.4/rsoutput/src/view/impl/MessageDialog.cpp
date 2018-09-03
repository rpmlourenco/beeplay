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
#include "MessageDialog.h"
#include "Platform.inl"
#include "Plugin.h"
#include <cassert>
#include <Poco/ThreadLocal.h>


static Poco::ThreadLocal<HHOOK> hook;


static LRESULT CALLBACK hookCallback(INT nCode, WPARAM wParam, LPARAM lParam)
{
	assert(hook.get() != NULL);

	if (nCode == HCBT_ACTIVATE)
	{
		// remove hook before centering to prevent looping
		UnhookWindowsHookEx(hook.get());
		hook.get() = NULL;

		CenterWindowOverParent((HWND) wParam);

		return 0;
	}

	return CallNextHookEx(hook.get(), nCode, wParam, lParam);
}


//------------------------------------------------------------------------------


class MessageDialogImpl
:
	private Uncopyable
{
	friend class MessageDialog;

	MessageDialogImpl(const std::string& message, const long flags)
	:
		_message(message),
		_flags(flags | MB_OK | MB_SETFOREGROUND)
	{
	}

	const std::string _message;
	const long _flags;
};


MessageDialog::MessageDialog(const std::string& message, const long flags)
:
	_impl(new MessageDialogImpl(message, flags))
{
}


MessageDialog::~MessageDialog()
{
	delete _impl;
}


int MessageDialog::doModal(HWND parentWindow)
{
	string_t message;
	Platform::Charset::fromUTF8(_impl->_message, message);
	string_t title;
	Platform::Charset::fromUTF8(Plugin::name(), title);

	if (parentWindow)
	{
		// set hook for centering message box over parent window
		assert(hook.get() == NULL);
		hook.get() = SetWindowsHookEx(WH_CBT, &hookCallback, 0, GetCurrentThreadId());
		if (hook.get() == NULL)
		{
			Debugger::printLastError("SetWindowsHook WH_CBT", __FILE__, __LINE__);
		}
	}

	return MessageBox(parentWindow, message.c_str(), title.c_str(), _impl->_flags);
}
