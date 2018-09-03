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

#ifndef Uncopyable_h
#define Uncopyable_h

#include "Platform.h"


class RSOUTPUT_API Uncopyable
{
protected:
	Uncopyable();
	~Uncopyable();

private:
	Uncopyable(const Uncopyable&);
	Uncopyable& operator =(const Uncopyable&);
};


inline Uncopyable::Uncopyable()
{
}


inline Uncopyable::~Uncopyable()
{
}


#endif // Uncopyable_h
