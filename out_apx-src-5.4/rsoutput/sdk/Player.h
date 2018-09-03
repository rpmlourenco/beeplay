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

#ifndef Player_h
#define Player_h


#include <string>
#include <windows.h>


class Player
{
public:
	virtual ~Player();

	virtual HWND window() const = 0;

	virtual void play() = 0;
	virtual void pause() = 0;
	virtual void stop() = 0;
	virtual void restart() = 0;
	virtual void startNext() = 0;
	virtual void startPrev() = 0;
	virtual void increaseVolume() = 0;
	virtual void decreaseVolume() = 0;
	virtual void toggleMute() = 0;
	virtual void toggleShuffle() = 0;
};


inline Player::~Player()
{
}


#endif // Player_h
