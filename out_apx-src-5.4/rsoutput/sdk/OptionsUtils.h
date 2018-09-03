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

#ifndef OptionsUtils_h
#define OptionsUtils_h


#include "Platform.h"
#include <string>


namespace OptionsUtils {

RSOUTPUT_API void loadOptions(const std::string& iniFilePath);
RSOUTPUT_API void saveOptions(const std::string& iniFilePath);

} // namespace OptionsUtils


#endif // OptionsUtils_h
