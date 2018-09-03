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

#include <cassert>
#include <windows.h>


HINSTANCE dllInstance = NULL;


/**
 * Main entry point for dynamic-link library (DLL).  This function is called
 * when the DLL is attached to or detached from a process or thread.
 *
 * @param instance
 * @param reason
 * @param reserved
 * @return <code>TRUE</code> on success, <code>FALSE</code> on failure
 */
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
	switch (reason)
	{
	case DLL_PROCESS_ATTACH:
		assert(dllInstance == NULL);
		dllInstance = instance;
		break;

	case DLL_PROCESS_DETACH:
		assert(dllInstance != NULL);
		dllInstance = NULL;
		break;
	}

	return TRUE;
}
