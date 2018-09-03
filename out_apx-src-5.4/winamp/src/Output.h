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

#ifndef Output_h
#define Output_h


#include <ctime>
#include <memory>
#include <string>
#include <utility>
#include <windows.h>


typedef std::auto_ptr<class OutputComponent> OutputComponentPtr;
typedef std::auto_ptr<class WinampHooks>     PlayerWindowHookPtr;
typedef WNDPROC                              PlayerWindowProcPtr;


class Output
{
public:
	static void init();
	static void quit();

	static void about(HWND);
	static void options(HWND);

	static int  open(int, int, int, int, int);
	static void close();
	static void reset(int);

	static int write(char*, int);

	static int canWrite();
	static int isPlaying();
	static int getOutputTime();
	static int getWrittenTime();

	static int  setPaused(int);
	static void setVolume(int);
	static void setBalance(int);

private:
	static void onBytesOutput(size_t);

	typedef std::pair<int,double> Progress;
	static void updateProgress(Progress&, size_t);

	static INT_PTR CALLBACK optionsDialogProc(HWND, UINT, WPARAM, LPARAM);
	static LRESULT CALLBACK playerWindowProc(HWND, UINT, WPARAM, LPARAM);

	static OutputComponentPtr _outputComponent;
	static PlayerWindowHookPtr _playerWindowHook;
	static PlayerWindowProcPtr _playerWindowProc;

	static Progress _bufferProgress;
	static Progress _outputProgress;
	static Progress::second_type _msecDivisor;

	static std::string _iniFilePath;
	static std::string _titleString;
	static std::time_t _resetToZero;

	Output();
};


#endif // Output_h
