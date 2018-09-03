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
#include "PasswordDialog.h"
#include "Platform.inl"
#include "Resources.h"
#include <cassert>
#include <string>
#include <vector>
#include <windowsx.h>


class PasswordDialogImpl
:
	public Dialog,
	private Uncopyable
{
	friend class PasswordDialog;

	explicit PasswordDialogImpl(const std::string&);

	void onInitialize();
	void onCommand(WORD, WORD);

	const std::string& _name;
	std::string _password;
	bool _rememberPassword;
};


//------------------------------------------------------------------------------


PasswordDialog::PasswordDialog(const std::string& name)
:
	_impl(new PasswordDialogImpl(name))
{
}


PasswordDialog::~PasswordDialog()
{
	delete _impl;
}


int PasswordDialog::doModal(HWND parentWindow)
{
	return _impl->doModal(DIALOG_PASSWORD, parentWindow);
}


const std::string& PasswordDialog::password() const
{
	return _impl->_password;
}


bool PasswordDialog::rememberPassword() const
{
	return _impl->_rememberPassword;
}


//------------------------------------------------------------------------------


PasswordDialogImpl::PasswordDialogImpl(const std::string& name)
:
	_name(name)
{
}


void PasswordDialogImpl::onInitialize()
{
	string_t name;
	Platform::Charset::fromUTF8(_name, name);

	int returnCode;
	char_t formatString[256];
	char_t finalString[256];

	returnCode = GetDlgItemText(_dialogWindow, DIALOG_PASSWORD_STRING,
		formatString, sizeof(formatString) / sizeof(char_t));
	assert(returnCode > 0 && returnCode < sizeof(formatString) / sizeof(char_t));

	returnCode = _stprintf_s(finalString, sizeof(finalString) / sizeof(char_t),
		formatString, name.c_str());
	assert(returnCode > 0 && returnCode < sizeof(finalString) / sizeof(char_t));

	returnCode = SetDlgItemText(_dialogWindow, DIALOG_PASSWORD_STRING, finalString);
	assert(returnCode != 0);
}


void PasswordDialogImpl::onCommand(WORD command, WORD control)
{
	if (control == IDOK && command == BN_CLICKED)
	{
		// get password textbox value
		HWND passwordEdit =
			GetDlgItem(_dialogWindow, DIALOG_PASSWORD_TEXTBOX_PASSWORD);
		std::vector<char_t> passwordBuffer(Edit_GetTextLength(passwordEdit) + 1);
		Edit_GetText(passwordEdit, &passwordBuffer[0], passwordBuffer.size());
		Platform::Charset::toUTF8(&passwordBuffer[0], _password);

		// get remember password checkbox value
		_rememberPassword = (BST_CHECKED == IsDlgButtonChecked(_dialogWindow,
			DIALOG_PASSWORD_CHECKBOX_REMEMBER));
	}

	Dialog::onCommand(command, control);
}
