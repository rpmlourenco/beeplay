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

#include "Plugin.h"


#include <Poco/Hash.h>
#include <Poco/Net/DNS.h>


using Poco::Net::DNS;


uint32_t Plugin::id()
{
	return 76777;
}


uint64_t Plugin::dacpId()
{
	static const uint64_t dacpId(
		static_cast<uint64_t>(Poco::hash(version())) << 32
			| static_cast<uint64_t>(Poco::hash(DNS::hostName())));
	return dacpId;
}


const std::string& Plugin::name()
{
	static const std::string name("Remote Speakers output");
	return name;
}


const std::string& Plugin::version()
{
	static const std::string version(__DATE__ " " __TIME__);
	return version;
}


const std::string& Plugin::userAgent()
{
	static const std::string userAgent(name() + "/" + version() + " (Windows; N)");
	return userAgent;
}


const std::string& Plugin::aboutText()
{
	static const std::string aboutText(name() + " (" + version() + ")\r\n"
		+ "\r\n"
		+ "Copyright \xC2\xA9 2006-2015  Eric Milles\r\n" // C2A9 is UTF-8 for ©
		+ "\r\n"
		+ "Special thanks to:\r\n"
		+ "Jon Lech Johansen, Erik de Castro Lopo and Ioannis Epaminonda\r\n"
		+ "\r\n"
		+ "This product includes software developed by the OpenSSL Project\r\n"
		+ "for use in the OpenSSL Toolkit (http://www.openssl.org/)");
	return aboutText;
}
