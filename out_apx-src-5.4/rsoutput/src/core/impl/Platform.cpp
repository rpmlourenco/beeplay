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

#include "Platform.h"
#include <cassert>


void CenterWindowOverParent(HWND window)
{
	HWND parent = GetParent(window);
	assert(parent != NULL);
	RECT parentRect;
	GetWindowRect(parent, &parentRect);
	RECT windowRect;
	GetWindowRect(window, &windowRect);
	POINT centerPoint;
	centerPoint.x = parentRect.left + (WIDTH(parentRect) / 2);
	centerPoint.y = parentRect.top + (HEIGHT(parentRect) / 2);
	POINT originPoint;
	originPoint.x = centerPoint.x - (WIDTH(windowRect) / 2);
	originPoint.y = centerPoint.y - (HEIGHT(windowRect) / 2);

	SetWindowPos(window, HWND_TOP, originPoint.x, originPoint.y, 0, 0, SWP_NOSIZE);
}


void SetWindowEnabled(HWND window, BOOL enable)
{
	EnableWindow(window, enable);
}


HWND GetDlgItemInfo(HWND dlgWndw, int itmIdnt, LPRECT itmRect)
{
	HWND itmWndw = GetDlgItem(dlgWndw, itmIdnt);

	if (itmRect)
	{
		GetWindowRect(itmWndw, itmRect);
		// change from screen to dialog coordinates
		RECT dlgRect;  GetWindowRect(dlgWndw, &dlgRect);
		OffsetRect(itmRect, -dlgRect.left, -dlgRect.top);
	}

	return itmWndw;
}


void ResizeDlgItem(HWND dlgWndw, int itmIdnt, int dx, int dy)
{
	RECT itmRect; HWND itmWndw = GetDlgItemInfo(dlgWndw, itmIdnt, &itmRect);

	SetWindowPos(itmWndw, 0, 0, 0, WIDTH(itmRect) + dx, HEIGHT(itmRect) + dy,
		SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOZORDER);
}


void RepositionDlgItem(HWND dlgWndw, int itmIdnt, int dx, int dy)
{
	RECT itmRect; HWND itmWndw = GetDlgItemInfo(dlgWndw, itmIdnt, &itmRect);

	SetWindowPos(itmWndw, 0, itmRect.left + dx, itmRect.top + dy, 0, 0,
		SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOZORDER);
}
