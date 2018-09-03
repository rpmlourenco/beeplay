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

#include "Debugger.h"
#include "Platform.inl"
#include "ServiceDiscovery.h"
#include "Uncopyable.h"
#include <algorithm>
#include <cassert>
#include <limits>
#include <list>
#include <regex>
#include <stdexcept>
#include <vector>
#include <winsock2.h>
#include <Poco/ByteOrder.h>
#include <Poco/Format.h>
#include <Poco/Mutex.h>
#include <Poco/Runnable.h>
#include <Poco/ScopedLock.h>
#include <Poco/ScopedUnlock.h>
#include <Poco/SharedLibrary.h>
#include <Poco/Thread.h>


using Poco::ByteOrder;
using Poco::FastMutex;
using Poco::Runnable;
using Poco::SharedLibrary;
using Poco::Thread;


class ServiceDiscoveryImpl
:
	public Runnable,
	private Uncopyable
{
	friend class ServiceDiscovery;

	ServiceDiscoveryImpl();
	~ServiceDiscoveryImpl();

	bool isRunning(DNSServiceRef) const;
	void start(DNSServiceRef);
	void stop(DNSServiceRef);
	void run();

	// DNSServiceBrowseReply
	static void DNSSD_API browseCallback(
		DNSServiceRef,
		DNSServiceFlags,
		uint32_t,
		DNSServiceErrorType,
		const char*,
		const char*,
		const char*,
		void*);
	// DNSServiceQueryRecordReply
	static void DNSSD_API queryCallback(
		DNSServiceRef,
		DNSServiceFlags,
		uint32_t,
		DNSServiceErrorType,
		const char*,
		uint16_t,
		uint16_t,
		uint16_t,
		const void*,
		uint32_t,
		void*);
	// DNSServiceRegisterReply
	static void DNSSD_API registerCallback(
		DNSServiceRef,
		DNSServiceFlags,
		DNSServiceErrorType,
		const char*,
		const char*,
		const char*,
		void*);
	// DNSServiceResolveReply
	static void DNSSD_API resolveCallback(
		DNSServiceRef,
		DNSServiceFlags,
		uint32_t,
		DNSServiceErrorType,
		const char*,
		const char*,
		uint16_t,
		uint16_t,
		const unsigned char*,
		void*);

	typedef DNSServiceErrorType (DNSSD_API *DNSServiceBrowseProc)(
		DNSServiceRef*,
		DNSServiceFlags,
		uint32_t,
		const char*,
		const char*,
		DNSServiceBrowseReply,
		void*);
	typedef DNSServiceErrorType (DNSSD_API *DNSServiceQueryRecordProc)(
		DNSServiceRef*,
		DNSServiceFlags,
		uint32_t,
		const char*,
		uint16_t,
		uint16_t,
		DNSServiceQueryRecordReply,
		void*);
	typedef DNSServiceErrorType (DNSSD_API *DNSServiceRegisterProc)(
		DNSServiceRef*,
		DNSServiceFlags,
		uint32_t,
		const char*,
		const char*,
		const char*,
		const char*,
		uint16_t,
		uint16_t,
		const void*,
		DNSServiceRegisterReply,
		void*);
	typedef DNSServiceErrorType (DNSSD_API *DNSServiceResolveProc)(
		DNSServiceRef*,
		DNSServiceFlags,
		uint32_t,
		const char*,
		const char*,
		const char*,
		DNSServiceResolveReply,
		void*);

	typedef DNSServiceErrorType (DNSSD_API *DNSServiceGetPropertyProc)(
		char*,
		void*,
		uint32_t*);
	typedef DNSServiceErrorType (DNSSD_API *DNSServiceConstructFullNameProc)(
		char*,
		const char*,
		const char*,
		const char*);
	typedef DNSServiceErrorType (DNSSD_API *DNSServiceProcessResultProc)(DNSServiceRef);
	typedef void (DNSSD_API *DNSServiceRefDeallocateProc)(DNSServiceRef);
	typedef int (DNSSD_API *DNSServiceRefSockFDProc)(DNSServiceRef);

	typedef void (DNSSD_API *TXTRecordCreateProc)(
		TXTRecordRef*,
		uint16_t,
		void*);
	typedef void (DNSSD_API *TXTRecordDeallocateProc)(
		TXTRecordRef*);
	typedef uint16_t (DNSSD_API *TXTRecordGetLengthProc)(
		const TXTRecordRef*);
	typedef const void* (DNSSD_API *TXTRecordGetBytesPtrProc)(
		const TXTRecordRef*);
	typedef int (DNSSD_API *TXTRecordContainsKeyProc)(
		uint16_t,
		const void*,
		const char*);
	typedef uint16_t (DNSSD_API *TXTRecordCountKeysProc)(
		uint16_t,
		const void*);
	typedef DNSServiceErrorType (DNSSD_API *TXTRecordGetItemAtIndexProc)(
		uint16_t,
		const void*,
		uint16_t,
		uint16_t,
		char*,
		uint8_t*,
		const void**);
	typedef const void* (DNSSD_API *TXTRecordGetValuePtrProc)(
		uint16_t,
		const void*,
		const char*,
		uint8_t*);
	typedef DNSServiceErrorType (DNSSD_API *TXTRecordSetValueProc)(
		TXTRecordRef*,
		const char*,
		uint8_t,
		const void*);

	SharedLibrary _sharedLibrary;
	// shared library function pointers:
	DNSServiceBrowseProc            browseServices;
	DNSServiceRegisterProc          registerService;
	DNSServiceResolveProc           resolveService;
	DNSServiceQueryRecordProc       queryRecord;
	DNSServiceGetPropertyProc       getProperty;
	DNSServiceConstructFullNameProc makeFullName;
	DNSServiceRefDeallocateProc     deallocate;
	DNSServiceRefSockFDProc         getSocketFD;
	DNSServiceProcessResultProc     processResult;
	TXTRecordCreateProc             txtRecordCreate;
	TXTRecordDeallocateProc         txtRecordDeallocate;
	TXTRecordGetLengthProc          txtRecordGetLength;
	TXTRecordGetBytesPtrProc        txtRecordGetBytesPtr;
	TXTRecordContainsKeyProc        txtRecordContainsKey;
	TXTRecordCountKeysProc          txtRecordCountKeys;
	TXTRecordGetItemAtIndexProc     txtRecordGetItemAtIndex;
	TXTRecordGetValuePtrProc        txtRecordGetValue;
	TXTRecordSetValueProc           txtRecordSetValue;

	std::list<DNSServiceRef> _activeRefs;

	mutable FastMutex _mutex;
	typedef const Poco::ScopedLock<FastMutex> ScopedLock;
	typedef const Poco::ScopedUnlock<FastMutex> ScopedUnlock;

	Thread _thread;
};


static const DNSServiceFlags kDNSServiceFlagsNone = 0;


//------------------------------------------------------------------------------


ServiceDiscoveryImpl& ServiceDiscovery::impl()
{
	static ServiceDiscoveryImpl singleton;
	return singleton;
}


bool ServiceDiscovery::isAvailable()
{
	try
	{
		// check if daemon is running by requesting its version property
		uint32_t versionNo = 0;
		uint32_t versionSz = 4;
		const DNSServiceErrorType error = impl().getProperty(
			kDNSServiceProperty_DaemonVersion, &versionNo, &versionSz);
		if (error != kDNSServiceErr_NoError)
		{
			throw std::runtime_error(Poco::format(
				"DNSServiceGetProperty returned error code %i", error));
		}
		assert(versionNo > 0 && versionSz == 4);

		return true;
	}
	CATCH_ALL

	return false;
}


bool ServiceDiscovery::isRunning(DNSServiceRef sdRef)
{
	return impl().isRunning(sdRef);
}


void ServiceDiscovery::start(DNSServiceRef sdRef)
{
	try
	{
		impl().start(sdRef);
	}
	catch (...)
	{
		impl().deallocate(sdRef);

		throw;
	}
}


void ServiceDiscovery::stop(DNSServiceRef sdRef)
{
	try
	{
		impl().stop(sdRef);
	}
	catch (...)
	{
		impl().deallocate(sdRef);

		throw;
	}
}


std::string ServiceDiscovery::fullName(
	const std::string& name,
	const std::string& type)
{
	char fullName[kDNSServiceMaxDomainName];
	const DNSServiceErrorType error = impl().makeFullName(
		fullName,
		name.c_str(),
		type.c_str(),
		"local.");
	if (error != 0)
	{
		throw std::runtime_error(Poco::format(
			"DNSServiceConstructFullName returned error code %i", error));
	}
	return fullName;
}


DNSServiceRef ServiceDiscovery::browseServices(
	const std::string& type,
	BrowseListener& listener)
{
	DNSServiceRef sdRef;
	const DNSServiceErrorType error = impl().browseServices(
		&sdRef,
		kDNSServiceFlagsNone,
		kDNSServiceInterfaceIndexAny,
		type.c_str(),
		"local.",
		ServiceDiscoveryImpl::browseCallback,
		(void*) &listener);
	if (error != kDNSServiceErr_NoError)
	{
		throw std::runtime_error(Poco::format(
			"DNSServiceBrowse returned error code %i", error));
	}
	return sdRef;
}


DNSServiceRef ServiceDiscovery::queryService(
	const std::string& rrname,
	const uint16_t rrtype,
	QueryListener& listener)
{
	DNSServiceRef sdRef;
	const DNSServiceErrorType error = impl().queryRecord(
		&sdRef,
		kDNSServiceFlagsLongLivedQuery
			| kDNSServiceFlagsAllowRemoteQuery
			| kDNSServiceFlagsSuppressUnusable,
		kDNSServiceInterfaceIndexAny,
		rrname.c_str(),
		rrtype,
		kDNSServiceClass_IN,
		ServiceDiscoveryImpl::queryCallback,
		(void*) &listener);
	if (error != kDNSServiceErr_NoError)
	{
		throw std::runtime_error(Poco::format(
			"DNSServiceQueryRecord returned error code %i", error));
	}
	return sdRef;
}


DNSServiceRef ServiceDiscovery::registerService(
	const std::string& name,
	const std::string& type,
	const uint16_t port,
	const TXTRecord& txtRecord,
	RegisterListener& listener)
{
	DNSServiceRef sdRef;
	const DNSServiceErrorType error = impl().registerService(
		&sdRef,
		kDNSServiceFlagsNone,
		kDNSServiceInterfaceIndexAny,
		name.c_str(),
		type.c_str(),
		"local.",
		NULL, // host
		ByteOrder::toNetwork(port),
		txtRecord.len(),
		txtRecord.buf(),
		ServiceDiscoveryImpl::registerCallback,
		(void*) &listener);
	if (error != kDNSServiceErr_NoError)
	{
		throw std::runtime_error(Poco::format(
			"DNSServiceRegister returned error code %i", error));
	}
	return sdRef;
}


DNSServiceRef ServiceDiscovery::resolveService(
	const std::string& name,
	const std::string& type,
	ResolveListener& listener)
{
	DNSServiceRef sdRef;
	const DNSServiceErrorType error = impl().resolveService(
		&sdRef,
		kDNSServiceFlagsNone,
		kDNSServiceInterfaceIndexAny,
		name.c_str(),
		type.c_str(),
		"local.",
		ServiceDiscoveryImpl::resolveCallback,
		(void*) &listener);
	if (error != kDNSServiceErr_NoError)
	{
		throw std::runtime_error(Poco::format(
			"DNSServiceResolve returned error code %i", error));
	}
	return sdRef;
}


//------------------------------------------------------------------------------


void DNSSD_API ServiceDiscoveryImpl::browseCallback(
	DNSServiceRef sdRef,
	const DNSServiceFlags flags,
	const uint32_t interfaceIndex,
	const DNSServiceErrorType error,
	const char* const name,
	const char* const type,
	const char* const domain,
	void* const context)
{
	ServiceDiscovery::BrowseListener* listener =
		static_cast<ServiceDiscovery::BrowseListener*>(context);
	try
	{
		assert(error == kDNSServiceErr_NoError);
		assert(std::string(domain) == "local.");

		if (flags & kDNSServiceFlagsAdd)
		{
			listener->onServiceFound(sdRef, name, type);
		}
		else
		{
			listener->onServiceLost(sdRef, name, type);
		}
	}
	CATCH_ALL
}


void DNSSD_API ServiceDiscoveryImpl::queryCallback(
	DNSServiceRef sdRef,
	const DNSServiceFlags flags,
	const uint32_t interfaceIndex,
	const DNSServiceErrorType error,
	const char* const rrname,
	const uint16_t rrtype,
	const uint16_t rrclass,
	const uint16_t rdlen,
	const void* const rdata,
	const uint32_t ttl,
	void* const context)
{
	ServiceDiscovery::QueryListener* listener =
		static_cast<ServiceDiscovery::QueryListener*>(context);
	try
	{
		assert(error == kDNSServiceErr_NoError);
		assert(flags & kDNSServiceFlagsAdd);
		assert(rrclass == kDNSServiceClass_IN);

		listener->onServiceQueried(sdRef, rrname, rrtype, rdlen, rdata, ttl);
	}
	CATCH_ALL
}


void DNSSD_API ServiceDiscoveryImpl::registerCallback(
	DNSServiceRef sdRef,
	const DNSServiceFlags flags,
	const DNSServiceErrorType error,
	const char* const name,
	const char* const type,
	const char* const domain,
	void* const context)
{
	ServiceDiscovery::RegisterListener* listener =
		static_cast<ServiceDiscovery::RegisterListener*>(context);
	try
	{
		assert(error == kDNSServiceErr_NoError);
		assert(std::string(domain) == "local.");

		listener->onServiceRegistered(sdRef, name, type);
	}
	CATCH_ALL
}


void DNSSD_API ServiceDiscoveryImpl::resolveCallback(
	DNSServiceRef sdRef,
	const DNSServiceFlags flags,
	const uint32_t interfaceIndex,
	const DNSServiceErrorType error,
	const char* const fullName,
	const char* const host,
	const uint16_t port,
	const uint16_t txtLength,
	const unsigned char* const txtRecord,
	void* const context)
{
	ServiceDiscovery::ResolveListener* listener =
		static_cast<ServiceDiscovery::ResolveListener*>(context);
	try
	{
		assert(error == kDNSServiceErr_NoError);

		listener->onServiceResolved(sdRef,
			fullName, host, ByteOrder::fromNetwork(port),
			ServiceDiscovery::TXTRecord(txtRecord, txtLength));
	}
	CATCH_ALL
}


//------------------------------------------------------------------------------


ServiceDiscoveryImpl::ServiceDiscoveryImpl()
:
	_sharedLibrary("dnssd.dll"),
	browseServices(static_cast<DNSServiceBrowseProc>(
		_sharedLibrary.getSymbol("DNSServiceBrowse"))),
	registerService(static_cast<DNSServiceRegisterProc>(
		_sharedLibrary.getSymbol("DNSServiceRegister"))),
	resolveService(static_cast<DNSServiceResolveProc>(
		_sharedLibrary.getSymbol("DNSServiceResolve"))),
	queryRecord(static_cast<DNSServiceQueryRecordProc>(
		_sharedLibrary.getSymbol("DNSServiceQueryRecord"))),
	getProperty(static_cast<DNSServiceGetPropertyProc>(
		_sharedLibrary.getSymbol("DNSServiceGetProperty"))),
	makeFullName(static_cast<DNSServiceConstructFullNameProc>(
		_sharedLibrary.getSymbol("DNSServiceConstructFullName"))),
	deallocate(static_cast<DNSServiceRefDeallocateProc>(
		_sharedLibrary.getSymbol("DNSServiceRefDeallocate"))),
	getSocketFD(static_cast<DNSServiceRefSockFDProc>(
		_sharedLibrary.getSymbol("DNSServiceRefSockFD"))),
	processResult(static_cast<DNSServiceProcessResultProc>(
		_sharedLibrary.getSymbol("DNSServiceProcessResult"))),
	txtRecordCreate(static_cast<TXTRecordCreateProc>(
		_sharedLibrary.getSymbol("TXTRecordCreate"))),
	txtRecordDeallocate(static_cast<TXTRecordDeallocateProc>(
		_sharedLibrary.getSymbol("TXTRecordDeallocate"))),
	txtRecordGetLength(static_cast<TXTRecordGetLengthProc>(
		_sharedLibrary.getSymbol("TXTRecordGetLength"))),
	txtRecordGetBytesPtr(static_cast<TXTRecordGetBytesPtrProc>(
		_sharedLibrary.getSymbol("TXTRecordGetBytesPtr"))),
	txtRecordContainsKey(static_cast<TXTRecordContainsKeyProc>(
		_sharedLibrary.getSymbol("TXTRecordContainsKey"))),
	txtRecordCountKeys(static_cast<TXTRecordCountKeysProc>(
		_sharedLibrary.getSymbol("TXTRecordGetCount"))),
	txtRecordGetItemAtIndex(static_cast<TXTRecordGetItemAtIndexProc>(
		_sharedLibrary.getSymbol("TXTRecordGetItemAtIndex"))),
	txtRecordGetValue(static_cast<TXTRecordGetValuePtrProc>(
		_sharedLibrary.getSymbol("TXTRecordGetValuePtr"))),
	txtRecordSetValue(static_cast<TXTRecordSetValueProc>(
		_sharedLibrary.getSymbol("TXTRecordSetValue"))),
	_thread("ServiceDiscoveryImpl::run")
{
}


ServiceDiscoveryImpl::~ServiceDiscoveryImpl()
{
	std::list<DNSServiceRef> sdRefs;

	try
	{
		_mutex.tryLock(100);
		_activeRefs.swap(sdRefs);

		_mutex.unlock();
		_thread.join(500);
	}
	CATCH_ALL

	std::for_each(sdRefs.begin(), sdRefs.end(), deallocate);
}


bool ServiceDiscoveryImpl::isRunning(DNSServiceRef sdRef) const
{
	ScopedLock lock(_mutex);

	std::list<DNSServiceRef>::const_iterator pos =
		std::find(_activeRefs.begin(), _activeRefs.end(), sdRef);

	return (pos != _activeRefs.end());
}


void ServiceDiscoveryImpl::start(DNSServiceRef sdRef)
{
	ScopedLock lock(_mutex);

	if (!isRunning(sdRef))
	{
		_activeRefs.push_back(sdRef);
	}

	if (!_thread.isRunning())
	{
		_thread.start(*this);
	}
}


void ServiceDiscoveryImpl::stop(DNSServiceRef sdRef)
{
	ScopedLock lock(_mutex);

	_activeRefs.remove(sdRef);

	deallocate(sdRef);
}


void ServiceDiscoveryImpl::run()
{
	Debugger::print("Starting service discovery thread...");

	const timeval timeout = { 0, 10000 };

	fd_set readfds;

	for (;;)
	{
		Thread::yield();

		ScopedLock lock(_mutex);

		// check stopping condition now
		if (_activeRefs.empty()) break;

		std::memset(&readfds, 0, sizeof(fd_set));
		assert(_activeRefs.size() <= FD_SETSIZE);
		for (std::list<DNSServiceRef>::const_iterator it =
			_activeRefs.begin(); it != _activeRefs.end(); ++it)
		{
			const int fd = getSocketFD(*it);
#pragma warning(push)
#pragma warning(disable:4127)
#pragma warning(disable:4389)
			if (fd > 0) FD_SET(fd, &readfds);
#pragma warning(pop)
		}

		// find active service discovery operations with available results
		const int returnCode = select(0, &readfds, NULL, NULL, &timeout);
		if (returnCode == SOCKET_ERROR)
		{
			Debugger::print("select() failed with error: " +
				Platform::Error::describe(WSAGetLastError()));

			// prevent running a tight loop if select errors on every call
			Thread::sleep(timeout.tv_sec * 1000 + timeout.tv_usec / 1000);
		}
		else if (returnCode > 0)
		{
			std::list<DNSServiceRef>::const_iterator it = _activeRefs.begin();
			while (it != _activeRefs.end() && !_activeRefs.empty())
			{
				DNSServiceRef sdRef = *(it++);

				if (FD_ISSET(getSocketFD(sdRef), &readfds))
				{
					DNSServiceErrorType error;

					{
					// prevent deadlock
					ScopedUnlock unlock(_mutex);
					// trigger callback function
					error = processResult(sdRef);
					}

					switch (error)
					{
					case kDNSServiceErr_NoError:
						break;

					case kDNSServiceErr_ServiceNotRunning:
						_activeRefs.clear();
						break;

					default:
						Debugger::printf("DNSServiceProcessResult returned error code %d", error);
						_activeRefs.remove(sdRef);
						deallocate(sdRef);
					}
				}
			}
		}
	}

	Debugger::print("Exiting service discovery thread...");
}


//------------------------------------------------------------------------------


ServiceDiscovery::TXTRecord::TXTRecord(const void* const buf, const uint16_t len)
:
	_buf(buf),
	_len(len)
{
}


ServiceDiscovery::TXTRecord::TXTRecord()
:
	_buf(0),
	_len(0)
{
	ServiceDiscovery::impl().txtRecordCreate(&_ref, 0, NULL);
	put("txtvers", "1");
}


ServiceDiscovery::TXTRecord::~TXTRecord()
{
	if (!_buf)
		ServiceDiscovery::impl().txtRecordDeallocate(&_ref);
}


const void* ServiceDiscovery::TXTRecord::buf() const
{
	if (_buf)
		return _buf;

	return ServiceDiscovery::impl().txtRecordGetBytesPtr(&_ref);
}


uint16_t ServiceDiscovery::TXTRecord::len() const
{
	if (_len)
		return _len;

	return ServiceDiscovery::impl().txtRecordGetLength(&_ref);
}


std::string ServiceDiscovery::TXTRecord::str() const
{
	const void* const buf = this->buf();
	const uint16_t len = this->len();
	const uint16_t num = ServiceDiscovery::impl().txtRecordCountKeys(len, buf);
	std::vector<char> key(std::numeric_limits<uint8_t>::max() + 1);
	std::string str;

	for (uint16_t idx = 0; idx < num; ++idx)
	{
		uint8_t siz;
		const void* val;
		const DNSServiceErrorType error =
			ServiceDiscovery::impl().txtRecordGetItemAtIndex(len, buf, idx,
				static_cast<uint16_t>(key.size()), &key[0], &siz, &val);
		if (error != kDNSServiceErr_NoError)
		{
			throw std::runtime_error(Poco::format(
				"TXTRecordGetItemAtIndex returned error code %i", error));
		}

		if (idx > 0)
			str.append(" ");
		str.append(&key[0]);
		str.append("=");
		str.append(static_cast<const char*>(val), siz);
	}
	return str;
}


bool ServiceDiscovery::TXTRecord::has(const std::string& key) const
{
	return (0 != ServiceDiscovery::impl().txtRecordContainsKey(len(), buf(), key.c_str()));
}


const void* ServiceDiscovery::TXTRecord::get(const std::string& key, uint8_t& len) const
{
	return ServiceDiscovery::impl().txtRecordGetValue(this->len(), buf(), key.c_str(), &len);
}


std::string ServiceDiscovery::TXTRecord::get(const std::string& key) const
{
	uint8_t len;
	const void* val = get(key, len);
	return std::string(static_cast<const char*>(val), len);
}


void ServiceDiscovery::TXTRecord::put(const std::string& key, const void* const val, const uint8_t len)
{
	const DNSServiceErrorType error = ServiceDiscovery::impl().txtRecordSetValue(&_ref, key.c_str(), len, val);
	if (error != kDNSServiceErr_NoError)
	{
		throw std::runtime_error(Poco::format("TXTRecordSetValue returned error code %i", error));
	}
}


void ServiceDiscovery::TXTRecord::put(const std::string& key, const std::string& val)
{
	if (val.length() > std::numeric_limits<uint8_t>::max())
	{
		throw std::invalid_argument("val.length() > std::numeric_limits<uint8_t>::max()");
	}
	put(key, val.c_str(), static_cast<uint8_t>(val.length()));
}


bool ServiceDiscovery::TXTRecord::test(const std::string& key, const std::string& regex) const
{
	return std::tr1::regex_match(get(key), std::tr1::regex(regex));
}
