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

#ifndef Random_h
#define Random_h


#include "Platform.h"
#include <cassert>
#include <cstdlib>


class Random
{
public:
	static void seed(unsigned int);
	static void fill(void*, size_t);

private:
	Random();
};


inline void Random::seed(const unsigned int seedValue)
{
	std::srand(seedValue);
}


inline void Random::fill(void* const buffer, const size_t length)
{
	assert(buffer != NULL && length > 0);

	for (size_t b = 0; b < length; ++b)
	{
		static_cast<byte_t*>(buffer)[b] = static_cast<byte_t>(std::rand());
	}
}


#endif // Random_h
