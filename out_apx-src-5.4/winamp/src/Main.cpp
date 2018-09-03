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
#include "Output.h"
#include "Plugin.h"
#include "WinampPlayer.h"
#include <cassert>
#include <memory>
#include <string>
#include <wa_out.h>


HINSTANCE dllInstance = NULL;
std::auto_ptr<WinampPlayer> player;


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
		Debugger::print("Player attaching to plug-in...");
		assert(dllInstance == NULL);
		dllInstance = instance;
		break;

	case DLL_PROCESS_DETACH:
		Debugger::print("Player detaching from plug-in...");
		assert(dllInstance != NULL);
		dllInstance = NULL;
		break;
	}

	return TRUE;
}


/**
 * Main entry point for Winamp output plug-in.  This function is called when the
 * player registers the plug-in.
 *
 * @return address of output structure to be used as interface between player
 *         and plug-in
 */
extern "C" __declspec(dllexport) Out_Module* winampGetOutModule()
{
	Debugger::print("Player requesting plug-in interface...");

	// initialize output interface singleton
	static const std::string name(Plugin::name() + " (" + Plugin::version() + ")");
	static Out_Module out = {
		OUT_VER,
		const_cast<char*>(name.c_str()),
		Plugin::id(),
		0, // window handle
		0, // instance handle
		Output::options,
		Output::about,
		Output::init,
		Output::quit,
		Output::open,
		Output::close,
		Output::write,
		Output::canWrite,
		Output::isPlaying,
		Output::setPaused,
		Output::setVolume,
		Output::setBalance,
		Output::reset,
		Output::getOutputTime,
		Output::getWrittenTime
	};

	if (player.get() == NULL)
	{
		// initialize player interface singleton
		player.reset(new WinampPlayer(out.hMainWindow));
	}

	return &out;
}
