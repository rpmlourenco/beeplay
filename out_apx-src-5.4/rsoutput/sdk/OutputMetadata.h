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

#ifndef OutputMetadata_h
#define OutputMetadata_h


#include "Platform.h"
#include <string>


class RSOUTPUT_API OutputMetadata
{
public:
	explicit OutputMetadata(
		time_t length = 0, // in ms
		const std::string& title = "",
		const std::string& album = "",
		const std::string& artist = "",
		const shorts_t   & listpos = shorts_t(0,0),
		const buffer_t   & artworkData = buffer_t(0),
		const std::string& artworkType = "image/none");
	OutputMetadata(const OutputMetadata&);
	~OutputMetadata();

	OutputMetadata& operator =(const OutputMetadata&);

	time_t length() const;
	const std::string& title() const;
	const std::string& album() const;
	const std::string& artist() const;
	const buffer_t   & artworkData() const;
	      shorts_t     artworkDims() const;
	const std::string& artworkType() const;
	const shorts_t   & playlistPos() const;

private:
	class OutputMetadataImpl* const _impl;
};


#endif // OutputMetadata_h
