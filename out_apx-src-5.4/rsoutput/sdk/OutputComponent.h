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

#ifndef OutputComponent_h
#define OutputComponent_h


#include "OutputFormat.h"
#include "OutputMetadata.h"
#include "Platform.h"
#include "Player.h"
#include "Uncopyable.h"
#include <functional>


class RSOUTPUT_API OutputComponent
:
	private Uncopyable
{
public:
	explicit OutputComponent(Player&);
	        ~OutputComponent();

	// functions are not thread-safe; call from a single thread or synchronize
	// setVolume, setBalance and setProgressCallback may be called when closed

	void open(const OutputFormat&, const OutputMetadata& = OutputMetadata());
	void close(); // ends playback (gracefully if buffered() is polled until 0)
	void reset(time_t); // discards buffered data and updates playback position

	bool write(const byte_t*, size_t); // buffers (little-endian) PCM audio data
		// call write(NULL, 0) to flush buffers (i.e. just before calling close)

	size_t canWrite() const;
	size_t buffered() const;
	time_t latency() const; // est. milliseconds of delay between write and hear

	bool setPaused(bool); // true: pause, false: resume
	void setVolume(float); // decibels from -100.0 to 0.0
	void setBalance(float); // left to right from -1.0 to 1.0
	void setMetadata(const OutputMetadata&, time_t progress = 0);

	// optional notification that bytes written have been transmitted
	typedef std::tr1::function<void (size_t)> ProgressCallback;
	void setProgressCallback(ProgressCallback);

private:
	class OutputComponentImpl* const _impl;
};


#endif // OutputComponent_h
