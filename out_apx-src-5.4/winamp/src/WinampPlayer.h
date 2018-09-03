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

#ifndef WinampPlayer_h
#define WinampPlayer_h


#include "Player.h"
#include "Platform.h"
#include "Uncopyable.h"
#include <string>


class WinampPlayer
:
	public Player,
	private Uncopyable
{
public:
	explicit WinampPlayer(HWND&);

	HWND window() const;

	void play();
	void pause();
	void stop();
	void restart();
	void startNext();
	void startPrev();
	void increaseVolume();
	void decreaseVolume();
	void toggleMute();
	void toggleShuffle();

	time_t getPlaybackMetadata(
		std::string& title,
		std::string& album,
		std::string& artist,
		buffer_t   & artworkData,
		std::string& artworkType,
		shorts_t   & playlistPos) const;

	time_t getPlaybackPosition() const;

private:
	void freeServices();
	void initServices();

	void sendCommand(int) const;
	template <typename type>
	type sendMessage(long, int = 0) const;

	class api_service     * _apiService;
	class api_memmgr      * _memMgmtService;
	class waServiceFactory* _memMgmtServiceFactory;

	HWND& _playerWindow;
};


inline HWND WinampPlayer::window() const
{
	return _playerWindow;
}


#endif // WinampPlayer_h
