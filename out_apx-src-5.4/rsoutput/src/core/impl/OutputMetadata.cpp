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

#include "OutputMetadata.h"
#include "Platform.h"
#include "Uncopyable.h"
#include <algorithm>
#include <limits>
#include <utility>
#include <Poco/ByteOrder.h>

using Poco::ByteOrder;


class OutputMetadataImpl
:
	private Uncopyable
{
	friend class OutputMetadata;

private:
	OutputMetadataImpl(
		time_t length,
		const std::string& title,
		const std::string& album,
		const std::string& artist,
		const buffer_t   & artworkData,
		const std::string& artworkType,
		const shorts_t   & playlistPos);
	~OutputMetadataImpl();

	time_t _length;
	std::string _title;
	std::string _album;
	std::string _artist;
	buffer_t    _artworkData;
	std::string _artworkType;
	shorts_t    _playlistPos;
};


OutputMetadataImpl::OutputMetadataImpl(
	const time_t length,
	const std::string& title,
	const std::string& album,
	const std::string& artist,
	const buffer_t   & artworkData,
	const std::string& artworkType,
	const shorts_t   & playlistPos)
:
	_length(length),
	_title(title),
	_album(album),
	_artist(artist),
	_artworkData(artworkData),
	_artworkType(artworkType),
	_playlistPos(playlistPos)
{
}


OutputMetadataImpl::~OutputMetadataImpl()
{
}


//------------------------------------------------------------------------------


OutputMetadata::OutputMetadata(
	const time_t length,
	const std::string& title,
	const std::string& album,
	const std::string& artist,
	const shorts_t   & listpos,
	const buffer_t   & artworkData,
	const std::string& artworkType)
:
	_impl(new OutputMetadataImpl(length, title, artist, album, artworkData, artworkType, listpos))
{
}


OutputMetadata::OutputMetadata(const OutputMetadata& that)
:
	_impl(new OutputMetadataImpl(
		that.length(),
		that.title(),
		that.album(),
		that.artist(),
		that.artworkData(),
		that.artworkType(),
		that.playlistPos())
	)
{
}


OutputMetadata::~OutputMetadata()
{
	delete _impl;
}


OutputMetadata& OutputMetadata::operator =(const OutputMetadata& that)
{
	_impl->_length = that.length();
	_impl->_title = that.title();
	_impl->_album = that.album();
	_impl->_artist = that.artist();
	_impl->_artworkData = that.artworkData();
	_impl->_artworkType = that.artworkType();
	_impl->_playlistPos = that.playlistPos();

	return *this;
}


time_t OutputMetadata::length() const
{
	return _impl->_length;
}


const std::string& OutputMetadata::title() const
{
	return _impl->_title;
}


const std::string& OutputMetadata::album() const
{
	return _impl->_album;
}


const std::string& OutputMetadata::artist() const
{
	return _impl->_artist;
}


const buffer_t& OutputMetadata::artworkData() const
{
	return _impl->_artworkData;
}


	#define read_and_shorten(pos, size, endian) short(std::min(                 \
		static_cast<const uint##size##_t>(std::numeric_limits<short>::max()),   \
		ByteOrder::from##endian(*reinterpret_cast<const uint##size##_t*>(&pos))))


shorts_t OutputMetadata::artworkDims() const
{
	short width = -1, height = -1;

	if (artworkType() == "image/none")
	{
		width = 0, height = 0;
	}
	if (artworkType() == "image/jpeg")
	{
		size_t i = 0, n = artworkData().size();
		const byte_t * jpg = &artworkData()[0];
		if (jpg[i++] == 0xff && jpg[i++] == 0xd8)
		{
		next:
			while (jpg[i++] != 0xff && i < n) ;
			while (jpg[i++] == 0xff && i < n) ;

			short len = read_and_shorten(jpg[i], 16, BigEndian);
			switch (jpg[i-1])
			{
			case 0xc0:
			case 0xc1:
			case 0xc2:
			case 0xc3:
			case 0xc5:
			case 0xc6:
			case 0xc7:
			case 0xc9:
			case 0xca:
			case 0xcb:
			case 0xcd:
			case 0xce:
			case 0xcf:
				if (len > 7) {
					height = read_and_shorten(jpg[i+3], 16, BigEndian);
					width  = read_and_shorten(jpg[i+5], 16, BigEndian);
					break;
				}
			default:
				if (len > 0) {
					i += len;
					goto next;
				}
			}
		}
	}
	if (artworkType() == "image/png")
	{
		const buffer_t& png = artworkData();
		if (png.size() > 32)
		{
			width  = read_and_shorten(png[16], 32, BigEndian);
			height = read_and_shorten(png[20], 32, BigEndian);
		}
	}
	if (artworkType() == "image/gif")
	{
		const buffer_t& gif = artworkData();
		if (gif.size() > 8)
		{
			const std::string format((char*) &gif[0], 6);
			if (format == "GIF87a" || format == "GIF89a")
			{
				width  = read_and_shorten(gif[6], 16, LittleEndian);
				height = read_and_shorten(gif[8], 16, LittleEndian);
			}
		}
	}

	return std::make_pair(width,height);
}


const std::string& OutputMetadata::artworkType() const
{
	return _impl->_artworkType;
}


const shorts_t& OutputMetadata::playlistPos() const
{
	return _impl->_playlistPos;
}
