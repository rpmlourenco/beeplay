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

#ifndef Dialog_h
#define Dialog_h


#include <windows.h>
#include <Poco/ThreadLocal.h>


class Dialog
{
public:
	Dialog();
	virtual ~Dialog() = 0;

	int doModal(UINT, HWND = NULL, BOOL startVisible = TRUE);
	void doModeless(UINT, HWND = NULL, BOOL startVisible = TRUE);

protected:
	void endDialog(int);

	virtual void onInitialize();
	virtual void onFinalize();
	virtual void onTimer();

	virtual void onCommand(WORD, WORD);
	virtual void onContextMenu(HWND, POINT);

	virtual int onCompareItem(LPCOMPAREITEMSTRUCT);
	virtual int onMeasureItem(LPMEASUREITEMSTRUCT);
	virtual int onDrawItem(LPDRAWITEMSTRUCT);

	HWND _dialogWindow;

private:
	static INT_PTR CALLBACK dialogProc(HWND, UINT, WPARAM, LPARAM);
	static Poco::ThreadLocal<Dialog*> _dialog;

	enum { MODAL, MODELESS, UNKNOWN } _dialogType;

	BOOL _startVisible;
	UINT _hideProgress;
};


#endif // Dialog_h
