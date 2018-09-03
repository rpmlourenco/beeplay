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

#ifndef ServiceDiscovery_h
#define ServiceDiscovery_h


#include "Platform.h"
#include <dns_sd.h>
#include <string>


class ServiceDiscovery
{
public:
	class TXTRecord
	{
		friend class ServiceDiscovery;
		friend class ServiceDiscoveryImpl;

	public:
		const TXTRecord(const void* buf, uint16_t len);
		TXTRecord();
		~TXTRecord();

		std::string str() const;
		bool has(const std::string& key) const;
		const void* get(const std::string& key, uint8_t& len) const;
		std::string get(const std::string& key) const;
		void put(const std::string& key, const void* val, uint8_t len);
		void put(const std::string& key, const std::string& val);
		bool test(const std::string& key, const std::string& regex) const;

	private:
		const void* buf() const;
		uint16_t len() const;

		const void*  _buf;
		uint16_t     _len;
		TXTRecordRef _ref;
	};

	struct BrowseListener {
		virtual void onServiceFound(DNSServiceRef, std::string name, std::string type) = 0;
		virtual void onServiceLost(DNSServiceRef, std::string name, std::string type) = 0;
	};

	struct QueryListener {
		virtual void onServiceQueried(DNSServiceRef, std::string rrname,
			uint16_t rrtype, uint16_t rdlen, const void* rdata, uint32_t ttl) = 0;
	};

	struct RegisterListener {
		virtual void onServiceRegistered(DNSServiceRef, std::string name, std::string type) {};
	};

	struct ResolveListener {
		virtual void onServiceResolved(DNSServiceRef, std::string fullName,
			std::string host, uint16_t port, const TXTRecord&) = 0;
	};

	static bool isAvailable();

	static DNSServiceRef browseServices(
		const std::string& type,
		BrowseListener& listener);

	static DNSServiceRef queryService(
		const std::string& rrname,
		const uint16_t rrtype,
		QueryListener& listener);

	static DNSServiceRef registerService(
		const std::string& name,
		const std::string& type,
		const uint16_t port,
		const TXTRecord& txtRecord,
		RegisterListener& listener);

	static DNSServiceRef resolveService(
		const std::string& name,
		const std::string& type,
		ResolveListener& listener);

	static std::string fullName(
		const std::string& name,
		const std::string& type);

	static bool isRunning(DNSServiceRef);
	static void start(DNSServiceRef);
	static void stop(DNSServiceRef);

private:
	static class ServiceDiscoveryImpl& impl();

	ServiceDiscovery();
};


#endif // ServiceDiscovery_h
