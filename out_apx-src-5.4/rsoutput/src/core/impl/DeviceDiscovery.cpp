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
#include "DeviceDiscovery.h"
#include "ServiceDiscovery.h"
#include "Uncopyable.h"
#include <cassert>
#include <exception>
#include <map>
#include <set>
#include <string>
#include <Poco/Mutex.h>


class DeviceDiscoveryImpl
:
	public ServiceDiscovery::BrowseListener,
	public ServiceDiscovery::QueryListener,
	private Uncopyable
{
	friend class DeviceDiscovery;

	void addListener(DeviceDiscovery::Listener&);
	void removeListener(DeviceDiscovery::Listener&);
	std::set<DeviceDiscovery::Listener*> _listeners;

	void onServiceFound(DNSServiceRef, std::string, std::string);
	void onServiceLost(DNSServiceRef, std::string, std::string);
	void onServiceQueried(DNSServiceRef, std::string, uint16_t, uint16_t, const void*, uint32_t);

	DeviceInfo::DeviceType determineDeviceType(const ServiceDiscovery::TXTRecord&) const;

	struct ServiceInfo { std::string name; std::string type; };
	std::map<DNSServiceRef,const ServiceInfo> _services;
	std::map<std::string, DeviceInfo> _devices;
	DNSServiceRef _discovery;
	Poco::FastMutex _mutex;

	typedef const Poco::FastMutex::ScopedLock ScopedLock;
};


//------------------------------------------------------------------------------


DeviceDiscoveryImpl& DeviceDiscovery::impl()
{
	static DeviceDiscoveryImpl singleton;
	return singleton;
}


void DeviceDiscovery::browseDevices(Listener& listener)
{
	impl().addListener(listener);
}


void DeviceDiscovery::stopBrowsing(Listener& listener)
{
	impl().removeListener(listener);
}


//------------------------------------------------------------------------------


void DeviceDiscoveryImpl::addListener(DeviceDiscovery::Listener& listener)
{
	ScopedLock lock(_mutex);

	if (ServiceDiscovery::isAvailable() &&
		!ServiceDiscovery::isRunning(_discovery))
	{
		_devices.clear(); _services.clear();
		_discovery = ServiceDiscovery::browseServices("_raop._tcp.", *this);
		ServiceDiscovery::start(_discovery);
	}

	// RML insert special device
	const DeviceInfo device(
		DeviceInfo::WIN,
		"Primary Sound Driver",
		std::make_pair("Primary Sound Driver", "0"),
		true); // zeroConf = yes

	_devices.insert(std::make_pair(device.name(), device));

	assert(_listeners.count(&listener) == 0);
	_listeners.insert(&listener);

	// inform listener of all known devices
	for (std::map<std::string, DeviceInfo>::const_iterator it =
		_devices.begin(); it != _devices.end(); ++it)
	{
		listener.onDeviceFound(it->second);
	}
}


void DeviceDiscoveryImpl::removeListener(DeviceDiscovery::Listener& listener)
{
	ScopedLock lock(_mutex);

	_listeners.erase(&listener);
}


void DeviceDiscoveryImpl::onServiceFound(
	DNSServiceRef sdRef,
	const std::string name,
	const std::string type)
{
	ScopedLock lock(_mutex);
	assert(sdRef == _discovery);

	// leave service browse activity running

	// create service info record
	const ServiceInfo info = {name, type};

	// create service query activity
	sdRef = ServiceDiscovery::queryService(
		ServiceDiscovery::fullName(name, type), kDNSServiceType_TXT, *this);

	// store service info for later use
	_services.insert(std::make_pair(sdRef, info));

	// start service query activity
	ServiceDiscovery::start(sdRef);
}


void DeviceDiscoveryImpl::onServiceLost(
	DNSServiceRef sdRef,
	const std::string name,
	const std::string type)
{
	ScopedLock lock(_mutex);
	assert(sdRef == _discovery);

	// leave service browse activity running

	// retrieve device info from map
	std::map< std::string, DeviceInfo>::const_iterator pos =
		_devices.find(name.substr(name.find_first_of('@') + 1));
	if (pos != _devices.end())
	{
		const DeviceInfo device = pos->second;

		// lose track of old device info
		_devices.erase(pos);

		// inform all listeners of lost device
		for (std::set<DeviceDiscovery::Listener*>::const_iterator it =
			_listeners.begin(); it != _listeners.end(); ++it)
		{
			(**it).onDeviceLost(device);
		}
	}
}


void DeviceDiscoveryImpl::onServiceQueried(
	DNSServiceRef sdRef,
	const std::string domName,
	const uint16_t rrtype,
	const uint16_t rdlen,
	const void* const rdata,
	const uint32_t ttl)
{
	// stop service query activity
	ServiceDiscovery::stop(sdRef);

	// retrieve service info record
	const ServiceInfo info(_services[sdRef]);
	_services.erase(sdRef);

	const ServiceDiscovery::TXTRecord txtRecord(rdata, rdlen);

	Debugger::printf("Discovered device \"%s\" with TXT record: %s",
		info.name.c_str(), txtRecord.str().c_str());

	const DeviceInfo device(
		determineDeviceType(txtRecord),
		info.name.substr(info.name.find_first_of('@') + 1),
		std::make_pair(info.name, info.type),
		true); // zeroConf = yes

	ScopedLock lock(_mutex);

	// keep track of new device info
	_devices.insert(std::make_pair(device.name(), device));

	// inform all listeners of found device
	for (std::set<DeviceDiscovery::Listener*>::const_iterator it =
		_listeners.begin(); it != _listeners.end(); ++it)
	{
		(**it).onDeviceFound(device);
	}
}


DeviceInfo::DeviceType DeviceDiscoveryImpl::determineDeviceType(
	const ServiceDiscovery::TXTRecord& txtRecord) const
{
	// AirPlay/AirTunes device TXT record keys:

	// txtvers=1                TXT version
	// sr=44100                 Sample rate
	// ss=16                    Sample size
	// ch=2                     Channel count
	// cn=[0,1,2,3]             Audio coding(s) (0=PCM, 1=ALAC, 2=AAC, 3=AAC ELD)
	// tp=[TCP,UDP]             Transport protocol(s)
	// et=[0,1,2,3,4,5]         Encryption type(s) (0=none, 1=RSA, 2=FPv?, 3=FPv?, 4=MFiSAP, 5=FPSAPv2pt5)
	// ek=1                     Has private encryption key (for indicated encryption type(s))
	// ft=0x[HEX]               Feature bits (0xX000: ?,?,PhotoCaching,FPSAPv2pt5_AES_GCM
	//                                        0x0X00: AudioRedundant,?,Audio,ScreenRotate
	//                                        0x00X0: Screen,?,Slideshow,VideoHTTPLiveStreams
	//                                        0x000X: VideoVolumeControl,FairPlay,Photo,Video
	// md=[0,1,2]               Metadata type(s) (0=text, 1=image, 2=progress)
	// pw=[true|false]          Password protected
	// vn=[n]                   Version number
	// vs=[n.n]                 Version string
	// fv=[n.n]                 Firmware version
	// am=[str]                 Model string (AirPort4,107, AppleTV1,1, ...)

	// da=true                  Digital audio?
	// pk=[hex]{64}             Public key?
	// sd=99                    ?
	// sf=0x[hex]               ? (1=?, 2=?, 4=audio, 8=?)
	// sm=false                 ?
	// sv=[true|false]          ?
	// vv=[1|2]                 ?

	// rhd=[n.n.n]              AirServer program version
	// rmodel=[str]             AirReceiver/X-Mirage host info
	// rast=afs                 Rogue Amoeba speaker type - Airfoil Speakers
	// rastx=iafs               Rogue Amoeba speaker type (for iOS or OS X?)
	// ramach=[str]             Rogue Amoeba machine type
	// raver=[n.n.n.n]          Rogue Amoeba program version
	// raflakyzeroconf=true     Rogue Amoeba zero-conf service?
	// raAudioFormats=ALAC,L16  Rogue Amoeba accepted audio formats


	// AirPort Express 802.11g TXT record looks like:
	// 6.1.1: txtvers=1 sr=44100 ss=16 ch=2 cn=1   ek=1 et=1   pw=false sm=false sv=false            vn=3
	// 6.2  : txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 ek=1 et=0,1 pw=false sm=false sv=false            vn=3
	// 6.3  : txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 ek=1 et=0,1 pw=false sm=false sv=false tp=TCP,UDP vn=3

	// AirPort Express 802.11n TXT record looks like:
	// 7.3  : txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 ek=1 et=0,1 da=true pw=false sv=false tp=TCP,UDP vn=65537 vs=100.18
	// 7.3.1: txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 ek=1 et=0,1 da=true pw=false sv=false tp=TCP,UDP vn=65537 vs=101.1
	// 7.3.2: txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 ek=1 et=0,1 da=true pw=false sv=false tp=TCP,UDP vn=65537 vs=101.1
	// 7.4.1: txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 ek=1 et=0,1 da=true pw=false sv=false tp=TCP,UDP vn=65537 vs=101.9 am=AirPort4,107 fv=74100.25 sf=0x5
	// 7.4.2: txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 ek=1 et=0,1 da=true pw=false sv=false tp=TCP,UDP vn=65537 vs=101.9 am=AirPort4,107 fv=74200.13 sf=0x5
	// 7.5.2: txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 ek=1 et=0,1 da=true pw=false sv=false tp=TCP,UDP vn=65537 vs=102.2 am=AirPort4,107 fv=75200.14 sf=0x5
	// 7.6  : txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 ek=1 et=0,1 da=true pw=false sv=false tp=TCP,UDP vn=65537 vs=105.1 am=AirPort4,107 fv=76000.14 sf=0x5
	// 7.6.1: txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 ek=1 et=0,1 da=true pw=false sv=false tp=TCP,UDP vn=65537 vs=105.1 am=AirPort4,107 fv=76100.4  sf=0x5
	// 7.6.3: txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 ek=1 et=0,1 da=true pw=false sv=false tp=TCP,UDP vn=65537 vs=105.1 am=AirPort4,107 fv=76300.7  sf=0x5
	// 7.6.4: txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 ek=1 et=0,1 da=true pw=false sv=false tp=TCP,UDP vn=65537 vs=105.1 am=AirPort4,107 fv=76400.10 sf=0x5

	// AirPort Express 802.11n (2nd Gen) TXT record looks like:
	// 7.6.2: txtvers=1 sr=44100 ss=16 ch=2 cn=0,1      et=0,4 da=true pw=false sv=false tp=TCP,UDP vn=65537 vs=105.1 am=AirPort10,115 fv=76200.16 sf=0x4
	// 7.6.3: txtvers=1 sr=44100 ss=16 ch=2 cn=0,1      et=0,4 da=true pw=false sv=false tp=TCP,UDP vn=65537 vs=105.1 am=AirPort10,115 fv=76300.7  sf=0x5
	// 7.6.4: txtvers=1 sr=44100 ss=16 ch=2 cn=0,1      et=0,4 da=true pw=false sv=false tp=TCP,UDP vn=65537 vs=105.1 am=AirPort10,115 fv=76400.10 sf=0x5

	// Apple TV (1st Gen) TXT record looks like:
	// 2.2  : txtvers=1 sr=44100 ss=16 ch=2 cn=0,1      et=0,2 md=0,1,2 da=true pw=false sv=false tp=TCP,UDP vn=65537 vs=101.8 am=AppleTV1,1 sf=0x4
	// 2.3  : txtvers=1 sr=44100 ss=16 ch=2 cn=0,1      et=0,2 md=0,1,2 da=true pw=false sv=false tp=TCP,UDP vn=65537 vs=101.9 am=AppleTV1,1 sf=0x4
	// 3.0.2: txtvers=1 sr=44100 ss=16 ch=2 cn=0,1      et=0,2 md=0,1,2 da=true pw=false sv=false tp=TCP,UDP vn=65537 vs=102.2 am=AppleTV1,1 sf=0x4

	// Apple TV (2nd Gen) TXT record looks like:
	// 4.x  : txtvers=1 sr=44100 ss=16 ch=2 cn=0,1     et=0,3   md=0,1,2 da=true pw=false sv=false tp=TCP,UDP vn=65537 vs=104.29      am=AppleTV2,1 sf=0x4
	// 4.3  : txtvers=1 sr=44100 ss=16 ch=2 cn=0,1     et=0,3   md=0,1,2 da=true pw=false sv=false tp=TCP,UDP vn=65537 vs=105.5       am=AppleTV2,1 sf=0x4
	// 4.4.x: txtvers=1 sr=44100 ss=16 ch=2 cn=0,1,2,3 et=0,3   md=0,1,2 da=true pw=false sv=false tp=UDP     vn=65537 vs=120.2       am=AppleTV2,1 sf=0x4
	// 5.0.x: txtvers=1 sr=44100 ss=16 ch=2 cn=0,1,2,3 et=0,3,5 md=0,1,2 da=true pw=false sv=false tp=UDP     vn=65537 vs=130.14      am=AppleTV2,1 sf=0x4
	// 5.1.x: txtvers=1 sr=44100 ss=16 ch=2 cn=0,1,2,3 et=0,3,5 md=0,1,2 da=true pw=false sv=false tp=UDP     vn=65537 vs=150.35 vv=1 am=AppleTV2,1 sf=0x4  ft=0xA7FCA00      pk=[0-9a-f]{64}
	// 5.2|3: txtvers=1 sr=44100 ss=16 ch=2 cn=0,1,2,3 et=0,3,5 md=0,1,2 da=true pw=false sv=false tp=UDP     vn=65537 vs=160.10 vv=1 am=AppleTV2,1 sf=0x4  ft=0x4A7FFFF7     pk=[0-9a-f]{64}
	// 6.0.x:                               cn=0,1,2,3 et=0,3,5 md=0,1,2 da=true                   tp=UDP     vn=65537 vs=190.9  vv=2 am=AppleTV2,1 sf=0x44 ft=0x4A7FFFF7,0xE pk=[0-9a-f]{64}
	// 6.1|2:                               cn=0,1,2,3 et=0,3,5 md=0,1,2 da=true                   tp=UDP     vn=65537 vs=200.54 vv=2 am=AppleTV2,1 sf=0x44 ft=0x4A7FFFF7,0xE pk=[0-9a-f]{64}

	// Apple TV (3rd Gen) TXT record looks like:
	// 5.2|3: txtvers=1 sr=44100 ss=16 ch=2 cn=0,1,2,3 et=0,3,5 md=0,1,2 da=true pw=false sv=false tp=UDP     vn=65537 vs=160.10 vv=1 am=AppleTV3,[12] sf=0x4  ft=0x5A7FFFF7      pk=[0-9a-f]{64}
	// 6.0.x:                               cn=0,1,2,3 et=0,3,5 md=0,1,2 da=true                   tp=UDP     vn=65537 vs=190.9  vv=2 am=AppleTV3,[12] sf=0x44 ft=0x5A7FFFF7,0xE  pk=[0-9a-f]{64}
	// 6.1|2:                               cn=0,1,2,3 et=0,3,5 md=0,1,2 da=true                   tp=UDP     vn=65537 vs=200.54 vv=2 am=AppleTV3,[12] sf=0x44 ft=0x5A7FFFF7,0xE  pk=[0-9a-f]{64}
	// 7.0.x:                               cn=0,1,2,3 et=0,3,5 md=0,1,2 da=true                   tp=UDP     vn=65537 vs=211.2  vv=2 am=AppleTV3,[12] sf=0x4  ft=0x5A7FFFF7,0x1E pk=[0-9a-f]{64}
	// 7.1  :
	// 7.2  : vs=220.68

	// Denon AVR TXT record looks like:
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 et=0,4 md=0,1,2 da=true pw=false sv=false tp=UDP vn=65537 vs=103.2 am=DENON,1  fv=65.9200
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 et=0,4 md=0,1,2 da=true pw=false sv=false tp=UDP vn=65537 vs=103.2 am=DENON,1  fv=66.8570
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 et=0,4 md=0,1,2 da=true pw=false sv=false tp=UDP vn=65537 vs=103.2 am=AVR2313  fv=s9092.0210.0
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 et=0,4 md=0,1,2 da=true pw=false sv=false tp=UDP vn=65537 vs=141.9 am=AVRX3000 fv=s9610.1000.0 ft=0x44F8A00

	// Denon DNP TXT record looks like:
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 et=0,4 md=0,2   da=true pw=false sv=false tp=UDP vn=65537 vs=103.2    am=DNP-F109 fv=s9207.1005.0
	//                               cn=0,1 et=0,4 md=0,2   da=true                   tp=UDP vn=65537 vs=190.9.p6 am=DNP-730  fv=p6.1005.0 ft=0x444F0A00 sf=0x4

	// Marantz AVR TXT record looks like:
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 et=0,4 md=0,2   da=true pw=false sv=false tp=UDP vn=65537 vs=103.2 am=DENON,1 fv=66.8786
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 et=0,4 md=0,1,2 da=true pw=false sv=false tp=UDP vn=65537 vs=141.9 am=NR1504  fv=s9610.1000.0 ft=0x44F8A00
	//                               cn=0,1 et=0,4 md=0,1,2 da=true                   tp=UDP vn=65537 vs=190.9 am=NR1605  fv=s9830.1102.0 ft=0x444F8A00 sd=99 sf=0x6

	// Pioneer VSX TXT record looks like:
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 et=0,4 md=0,1,2 da=true pw=false sv=false tp=UDP vn=65537 vs=103.2 am=PIONEER,1 fv=s1050.0.0
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 et=0,4 md=0,1,2 da=true pw=false sv=false tp=UDP vn=65537 vs=103.2 am=PIONEER,1 fv= 136.8960

	// Yamaha RX-A1020 TXT record looks like:
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 et=0,4 md=0,1,2 da=true pw=false sv=false tp=UDP vn=65537 vs=103.2 am=RX-A1020 fv=s8927.1051.0
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 et=0,4 md=0,1,2 da=true pw=false sv=false tp=UDP vn=65537 vs=141.9 am=RX-V677  fv=s9575.1050.0 ft=0x44F8A00

	// Bose SoundTouch TXT record looks like:
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 et=0,4 md=0     da=true pw=false sv=false tp=UDP vn=65537 vs=141.9 am=Bose SoundTouch fv=s9423.40404.3372 ft=0x44E0A00

	// JBL SoundFly Air TXT record looks like:
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 et=0,4          da=true pw=false sv=false tp=UDP vn=65537 vs=103.2 am=JBL SoundFly Air fv=s9110.11.545

	// Klipsch G-17 Air TXT record looks like:
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 et=0,4          da=true pw=false sv=false tp=UDP vn=65537 vs=103.2 am=JB2 Gen fv=93.1000
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 et=0,4          da=true pw=false sv=false tp=UDP vn=65537 vs=141.9 am=G17 fv=s9236.102.0 ft=0x44C0A00

	// Pioneer XW-SMA[134] TXT record looks like:
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 et=0,4          da=true pw=false sv=false tp=UDP vn=65537 vs=103.2 am=XW-SMA1 fv=s1010.1000.0

	// Sony CMT-G2NiP/G2BNiP TXT record looks like:
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 et=0,4 md=0,2   da=true pw=false sv=false tp=UDP vn=65537 vs=103.2 am=JB2 Gen fv=107.204


	// Airfoil Speakers TXT record looks like:
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 ek=1 et=0,1 md=0,1,2 pw=false sm=false sv=false tp=UDP vn=3 rast=afs rastx=iafs ramach=Macmini3,1 raAudioFormats=ALAC,L16
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 ek=1 et=0,1 md=0,1,2 pw=false sm=false sv=false tp=UDP vn=3 rast=afs rastx=iafs ramach=Win32NT.6  raAudioFormats=ALAC     raflakyzeroconf=true raver=3.6.0.0

	// AirReceiver (Fire TV) TXT record looks like:
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1,2,3 et=0,3,5 md=0,1,2 da=true pw=false sm=false sv=false sf=0x4 tp=UDP vn=65537 vs=150.33 am=AppleTV3,1 rmodel=AirRecever,1
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1,2,3 et=0,3,5 md=0,1,2 da=true pw=false sm=false sv=false sf=0x4 tp=UDP vn=65537 vs=211.3  am=AppleTV3,1 rmodel=AirReceiver3,1 ft=0x527ffff7 pk=[0-9a-f]{64}

	// AirServer (OS X) 3.x TXT record looks like:
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 ek=1 et=0,1 pw=false sm=false sv=false tp=UDP vn=3 rhd=3.1.2

	// AirServer (OS X) 4.x TXT record looks like:
	// 4.0.1: txtvers=1 sr=44100 ss=16 ch=2 cn=0,1,2,3 ek=1 et=0,3   md=0,1,2 da=true pw=false sv=false sf=0x4 tp=UDP     vn=65537 vs=120.2       am=AppleTV2,1    rhd=4.0.1
	// 4.2.2: txtvers=1 sr=44100 ss=16 ch=2 cn=0,1,2,3 ek=1 et=0,3   md=0,1,2 da=true pw=false sv=false sf=0x4 tp=UDP     vn=65537 vs=120.2       am=AppleTV3,1    rhd=4.2.2
	//        txtvers=1 sr=44100 ss=16 ch=2 cn=0,1     ek=1 et=0,1   md=0,1,2 da=true pw=false sv=false sf=0x4 tp=UDP     vn=65537 vs=120.2       am=AppleTV3,1    rhd=4.2.2
	// 4.3.1: txtvers=1 sr=44100 ss=16 ch=2 cn=0,1,2,3 ek=1 et=0,3   md=0,1,2 da=true pw=false sv=false sf=0x4 tp=UDP     vn=65537 vs=120.2       am=AppleTV3,1    rhd=4.3.1
	//        txtvers=1 sr=44100 ss=16 ch=2 cn=0,1     ek=1 et=0,1   md=0,1,2 da=true pw=false sv=false sf=0x4 tp=UDP     vn=65537 vs=120.2       am=AppleTV3,1    rhd=4.3.1
	// 4.4.0: txtvers=1 sr=44100 ss=16 ch=2 cn=0,1,2,3 ek=1 et=0,3,5 md=0,1,2 da=true pw=false sv=false sf=0x4 tp=UDP     vn=65537 vs=130.14      am=AppleTV3,1    rhd=4.4
	//        txtvers=1 sr=44100 ss=16 ch=2 cn=0,1     ek=1 et=0,1   md=0,1,2 da=true pw=false sv=false sf=0x4 tp=UDP     vn=65537 vs=130.14      am=AppleTV3,1    rhd=4.4
	// 4.5.2: txtvers=1 sr=44100 ss=16 ch=2 cn=0,1,2,3 ek=1 et=0,3,5 md=0,1,2 da=true pw=false sv=false sf=0x4 tp=UDP     vn=65537 vs=150.33      am=AppleTV3,1    rhd=4.5.2
	//        txtvers=1 sr=44100 ss=16 ch=2 cn=0,1     ek=1 et=0,1   md=0,1,2 da=true pw=false sv=false sf=0x4 tp=UDP     vn=65537 vs=150.33      am=AppleTV3,1    rhd=4.5.2
	// 4.6.0: txtvers=1 sr=44100 ss=16 ch=2 cn=1,2,3        et=0,3,5 md=0,1,2 da=true pw=false sv=false sf=0x4 tp=UDP     vn=65537 vs=150.35 vv=1 am=AppleTV3,1    rhd=4.6
	//        txtvers=1 sr=44100 ss=16 ch=2 cn=1,2          et=0,3,5 md=0,1,2 da=true pw=false sv=false sf=0x4 tp=TCP,UDP vn=65537 vs=150.35 vv=1 am=AppleTV3,1    rhd=4.6
	// 4.6.1: txtvers=1 sr=44100 ss=16 ch=2 cn=1,2,3        et=0,3,5 md=0,1,2 da=true pw=false sv=false sf=0x4 tp=UDP     vn=65537 vs=150.35 vv=1 am=AppleTV3,1    rhd=4.6.1
	//        txtvers=1 sr=44100 ss=16 ch=2 cn=1,2          et=0,3,5 md=0,1,2 da=true pw=false sv=false sf=0x4 tp=UDP     vn=65537 vs=150.35 vv=1 am=AirPort10,115 rhd=4.6.1
	// 4.6.5: txtvers=1 sr=44100 ss=16 ch=2 cn=1,2,3        et=0,3,5 md=0,1,2 da=true pw=false sv=false sf=0x4 tp=UDP     vn=65537 vs=150.33 vv=1 am=AppleTV3,1    rhd=4.6.5
	//        txtvers=1 sr=44100 ss=16 ch=2 cn=1            et=0,3,5 md=0,1,2 da=true pw=false sv=false sf=0x4 tp=UDP     vn=65537 vs=105.1  vv=1 am=AirPort10,115 rhd=4.6.5 fv=76200.16
	// 4.7.1: txtvers=1 sr=44100 ss=16 ch=2 cn=1,2,3        et=0,3,5 md=0,1,2 da=true pw=false sv=false sf=0x4 tp=UDP     vn=65537 vs=150.33 vv=1 am=AppleTV3,2    rhd=4.7.1
	//        txtvers=1 sr=44100 ss=16 ch=2 cn=1            et=0,3,5 md=0,1,2 da=true pw=false sv=false sf=0x4 tp=UDP     vn=65537 vs=105.1  vv=1 am=AirPort10,115 rhd=4.7.1 fv=76200.16
	// 4.8.1: txtvers=1 sr=44100 ss=16 ch=2 cn=1,2,3        et=0,3,5 md=0,1,2 da=true pw=false sv=false sf=0x4 tp=UDP     vn=65537 vs=150.33 vv=1 am=AppleTV3,2    rhd=4.8.1
	//        txtvers=1 sr=44100 ss=16 ch=2 cn=1            et=0,3,5 md=0,1,2 da=true pw=false sv=false sf=0x4 tp=UDP     vn=65537 vs=105.1  vv=1 am=AirPort10,115 rhd=4.8.1 fv=76200.16
	// 5.0.7: txtvers=1 sr=44100 ss=16 ch=2 cn=1,2,3        et=0,3,5 md=0,1,2 da=true pw=false sv=false sf=0x4 tp=UDP     vn=65537 vs=150.33 vv=1 am=AppleTV3,2    rhd=5.0.7
	//        txtvers=1 sr=44100 ss=16 ch=2 cn=1            et=0,3,5 md=0,1,2 da=true pw=false sv=false sf=0x4 tp=UDP     vn=65537 vs=105.1  vv=1 am=AirPort10,115 rhd=5.0.7 fv=76200.16
	// 5.1.1: txtvers=1 sr=44100 ss=16 ch=2 cn=1,2,3        et=0,3,5 md=0,1,2 da=true pw=false sv=false sf=0x4 tp=UDP     vn=65537 vs=150.33 vv=1 am=AppleTV3,2    rhd=5.1.1
	//        txtvers=1 sr=44100 ss=16 ch=2 cn=1            et=0,3,5 md=0,1,2 da=true pw=false sv=false sf=0x4 tp=UDP     vn=65537 vs=105.1  vv=1 am=AirPort10,115 rhd=5.1.1 fv=76200.16
	// 5.2.0: txtvers=1 sr=44100 ss=16 ch=2 cn=1,2,3        et=0,3,5 md=0,1,2 da=true pw=false sv=false sf=0x4 tp=UDP     vn=65537 vs=150.33 vv=1 am=AppleTV3,2    rhd=5.2
	//        txtvers=1 sr=44100 ss=16 ch=2 cn=1            et=0,3,5 md=0,1,2 da=true pw=false sv=false sf=0x4 tp=UDP     vn=65537 vs=105.1  vv=1 am=AirPort10,115 rhd=5.2   fv=76200.16
	// 5.3.2: txtvers=1 sr=44100 ss=16 ch=2 cn=1,2,3        et=0,3,5 md=0,1,2 da=true pw=false sv=false sf=0x4 tp=UDP     vn=65537 vs=150.33 vv=2 am=AppleTV3,2    rhd=5.3.2 ft=0x100029ff
	//        txtvers=1 sr=44100 ss=16 ch=2 cn=1       ek=1 et=0,1   md=0,1,2 da=true pw=false sv=false sf=0x4 tp=UDP     vn=65537 vs=105.1       am=AirPort10,115 rhd=5.3.2 fv=76200.16
	// 6.0  :                               cn=0,1,2,3      et=0,3,5 md=0,1,2 da=true                   sf=0x4 tp=UDP     vn=65537 vs=220.68 vv=2 am=AppleTV3,2              ft=0x5A7FFFF7,0x1E pk=2219e9e1d4ee98a977ea5ac458861a2f97c314979b8daaecb82f9b300893d736
	//        txtvers=1 sr=44100 ss=16 ch=2 cn=0,1,2        et=0,3,5 md=0,1,2 da=true pw=false sv=true  sf=0x4 tp=UDP     vn=65537 vs=105.1       am=Airport10,115           ft=0x25FCA00 fv=76400.10

	// AirServer (Win32) TXT record looks like:
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1,2,3 et=0,3,5 md=0,1,2 da=true pw=false sv=false sf=0x4 tp=UDP vn=65537 vs=150.33 vv=1 am=AppleTV3,2                rhd=3.0.26
	// txtvers=1 sr=44100 ss=16 ch=2 cn=1       et=0,3,5 md=0,1,2 da=true pw=false sv=false sf=0x4 tp=UDP vn=65537 vs=105.1  vv=1 am=AirPort10,115 fv=76200.16 rhd=3.0.26

	// Freebox Server TXT record looks like:
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 ek=1 et=0,1 md=0,1,2 da=true pw=false sv=false tp=UDP vn=65537 vs=110.63 am=AirPort4,107 sf=0x4

	// ShairPort, AirBubble, AirMobi iDevice, Boxee Box, Sabrent WF-RADU, etc. TXT record looks like:
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 ek=1 et=0,1                  pw=false sm=false sv=false tp=UDP vn=3

	// Kodi TXT record looks like:
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 ek=1 et=0,1 md=0,1,2 da=true pw=false sm=false sv=false tp=UDP vn=3 vs=130.14 am=Kodi,1

	// XBMC TXT record looks like:
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 ek=1 et=0,1                  pw=false sm=false sv=false tp=UDP vn=3
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 ek=1 et=0,1 md=0,1,2 da=true pw=false sm=false sv=false tp=UDP vn=3 vs=130.14
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 ek=1 et=0,1 md=0,1,2 da=true pw=false sm=false sv=false tp=UDP vn=3 vs=130.14 am=Xbmc,1
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1 ek=1 et=0,1 md=0,1,2 da=true pw=false sm=false sv=false tp=UDP vn=3 vs=160.10 am=AppleTV3,1

	// X-Mirage TXT record looks like:
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1,2,3 ek=1 et=0,1,3 md=0,1,2 da=true pw=false sm=false sv=false sf=0x4 tp=UDP vn=65537 vs=150.33 vv=1 am=AppleTV3,1
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1,2,3 ek=1 et=0,1,3 md=0,1,2 da=true pw=false sm=false sv=false sf=0x4 tp=UDP vn=65537 vs=105.1  vv=1 am=AirPort10,115 fv=76200.16
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1,2,3      et=0,1,3 md=0,1,2 da=true pw=false          sv=false sf=0x4 tp=UDP vn=65537 vs=150.33 vv=1 am=AppleTV3,1                rhd=1.06.3 rmodel=Macmini3,1
	// txtvers=1 sr=44100 ss=16 ch=2 cn=0,1,2,3      et=0,1,3 md=0,1,2 da=true pw=false          sv=false sf=0x4 tp=UDP vn=65537 vs=105.1  vv=1 am=AirPort10,115 fv=76200.16 rhd=1.06.3 rmodel=Macmini3,1


	// check common properties of all device types
	assert(!txtRecord.has("txtvers") || txtRecord.get("txtvers") == "1");
	assert(!txtRecord.has("sr") || txtRecord.get("sr") == "44100");
	assert(!txtRecord.has("ss") || txtRecord.get("ss") == "16");
	assert(!txtRecord.has("ch") || txtRecord.get("ch") == "2");
	assert(!txtRecord.has("pw") || txtRecord.test("pw", "true|false"));
	assert(!txtRecord.has("sv") || txtRecord.test("sv", "true|false"));
	assert( txtRecord.has("cn") && txtRecord.test("cn", "(0,)?1(,2)?(,3)?"));

	// determine device type from remaining properties
	if (txtRecord.has("rast") || txtRecord.has("rastx")
		|| txtRecord.has("raver") || txtRecord.has("ramach"))
	{
		// Airfoil Speakers

		assert(txtRecord.has("ek") && txtRecord.get("ek") == "1");
		assert(txtRecord.has("et") && txtRecord.get("et") == "0,1");
		assert(txtRecord.has("md") && txtRecord.get("md") == "0,1,2");
		assert(txtRecord.has("sm") && txtRecord.get("sm") == "false");
		assert(txtRecord.has("tp") && txtRecord.get("tp") == "UDP");
		assert(txtRecord.has("vn") && txtRecord.get("vn") == "3");

		assert(!txtRecord.has("am"));
		assert(!txtRecord.has("da"));
		assert(!txtRecord.has("ft"));
		assert(!txtRecord.has("fv"));
		assert(!txtRecord.has("pk"));
		assert(!txtRecord.has("sf"));
		assert(!txtRecord.has("vs"));
		assert(!txtRecord.has("vv"));

		return DeviceInfo::AFS;
	}
	else if (txtRecord.has("rhd") && !txtRecord.has("md"))
	{
		// AirServer Classic

		assert(txtRecord.has("ek") && txtRecord.get("ek") == "1");
		assert(txtRecord.has("et") && txtRecord.get("et") == "0,1");
		assert(txtRecord.has("sm") && txtRecord.get("sm") == "false");
		assert(txtRecord.has("tp") && txtRecord.get("tp") == "UDP");
		assert(txtRecord.has("vn") && txtRecord.get("vn") == "3");

		assert(!txtRecord.has("am"));
		assert(!txtRecord.has("da"));
		assert(!txtRecord.has("ft"));
		assert(!txtRecord.has("fv"));
		assert(!txtRecord.has("pk"));
		assert(!txtRecord.has("sf"));
		assert(!txtRecord.has("vs"));
		assert(!txtRecord.has("vv"));

		return DeviceInfo::AS3;
	}
	else if ((txtRecord.has("rhd") && !txtRecord.has("rmodel"))
		|| (txtRecord.get("cn") == "0,1,2" && txtRecord.has("ft")
			&& txtRecord.has("sv") && txtRecord.get("sv") == "true"))
	{
		// AirServer

		if (txtRecord.has("am") && txtRecord.test("am", "Air[Pp]ort.*"))
			throw std::exception("Ignoring redundant AirServer service");

		assert( txtRecord.has("am") && txtRecord.test("am", "AppleTV[23],[12]"));
		assert( txtRecord.has("da") && txtRecord.get("da") == "true");
		assert(!txtRecord.has("ek") || txtRecord.get("ek") == "1");
		assert( txtRecord.has("et") && txtRecord.test("et", "0,3(,5)?"));
		assert(!txtRecord.has("ft") || txtRecord.test("ft", "0x[0-9a-f]{8}"));
		assert( txtRecord.has("md") && txtRecord.get("md") == "0,1,2");
		assert( txtRecord.has("sf") && txtRecord.get("sf") == "0x4");
		assert( txtRecord.has("tp") && txtRecord.get("tp") == "UDP");
		assert( txtRecord.has("vn") && txtRecord.get("vn") == "65537");
		assert( txtRecord.has("vs") && txtRecord.test("vs", "1\\d{2}\\.\\d{1,2}"));
		assert(!txtRecord.has("vv") || txtRecord.test("vv", "1|2"));

		assert(!txtRecord.has("fv"));
		assert(!txtRecord.has("pk"));
		assert(!txtRecord.has("sm"));

		return DeviceInfo::AS4; // (or 5)
	}
	// X-Mirage claims to support encrypted audio strem but
	// it does not, so trap it to disable stream encryption
	else if (txtRecord.has("rmodel") || (txtRecord.has("am")
		&& txtRecord.has("vv") && txtRecord.get("vv") == "1"
		&& txtRecord.has("ek") && txtRecord.get("ek") == "1"
		&& txtRecord.has("et") && txtRecord.get("et") == "0,1,3"
		&& txtRecord.has("md") && txtRecord.get("md") == "0,1,2"
		&& txtRecord.has("sm") && txtRecord.get("sm") == "false"
		&& txtRecord.has("vn") && txtRecord.get("vn") == "65537"
		&& txtRecord.has("vs") && (txtRecord.get("vs") == "150.33"
								|| txtRecord.get("vs") == "105.1")))
	{
		// X-Mirage (or AirReceiver)

		if (txtRecord.has("am") && txtRecord.test("am", "AirPort.*"))
			throw std::exception("Ignoring redundant X-Mirage service");

		if (txtRecord.has("rmodel") && txtRecord.test("rmodel", "(?!AirRecei?ver).*"))
			assert(txtRecord.has("rhd") && txtRecord.test("rhd", "\\d\\.\\d{2}\\.\\d"));

		assert( txtRecord.has("am") && txtRecord.get("am") == "AppleTV3,1");
		assert( txtRecord.has("da") && txtRecord.get("da") == "true");
		assert(!txtRecord.has("ek") || txtRecord.get("ek") == "1");
		assert( txtRecord.has("et") && txtRecord.test("et", "0,1,3|0,3,5"));
		assert(!txtRecord.has("ft") || txtRecord.test("ft", "0x[0-9a-f]{8}"));
		assert( txtRecord.has("md") && txtRecord.get("md") == "0,1,2");
		assert(!txtRecord.has("pk") || txtRecord.test("pk", "[0-9a-f]{64}"));
		assert( txtRecord.has("sf") && txtRecord.get("sf") == "0x4");
		assert(!txtRecord.has("sm") || txtRecord.get("sm") == "false");
		assert( txtRecord.has("tp") && txtRecord.get("tp") == "UDP");
		assert( txtRecord.has("vn") && txtRecord.get("vn") == "65537");
		assert( txtRecord.has("vs") && txtRecord.test("vs", "150.33|211.3"));
		assert(!txtRecord.has("vv") || txtRecord.get("vv") == "1");

		assert(!txtRecord.has("fv"));

		// make generic device with all metadatas but no encryption
		return DeviceInfo::DeviceType(MAKELONG(DeviceInfo::ANY, 7));
	}
	else if (!txtRecord.has("am") && !txtRecord.has("da") && !txtRecord.has("fv")
		  && !txtRecord.has("md") && !txtRecord.has("tp") && !txtRecord.has("vs"))
	{
		throw std::exception("AirPort Express 6.1.1 or 6.2 not supported");
	}
	else if ((txtRecord.has("am") && txtRecord.test("am", "AirPort.*") && !txtRecord.has("md"))
		 || (!txtRecord.has("am") && txtRecord.has("tp") && txtRecord.get("tp") == "TCP,UDP"))
	{
		// AirPort Express

		assert(!txtRecord.has("da") || txtRecord.get("da") == "true");
		assert(!txtRecord.has("ek") || txtRecord.get("ek") == "1");
		assert( txtRecord.has("et") && txtRecord.test("et", "0(,\\d)+"));
		assert(!txtRecord.has("fv") || txtRecord.test("fv", "7\\d{4}\\.\\d+"));
		assert(!txtRecord.has("sf") || txtRecord.test("sf", "0x\\d"));
		assert(!txtRecord.has("sm") || txtRecord.get("sm") == "false");
		assert( txtRecord.has("tp") && txtRecord.get("tp") == "TCP,UDP");
		assert( txtRecord.has("vn") && txtRecord.test("vn", "3|65537"));
		assert(!txtRecord.has("vs") || txtRecord.test("vs", "\\d{3}\\.\\d{1,2}"));

		assert(!txtRecord.has("ft"));
		assert(!txtRecord.has("md"));
		assert(!txtRecord.has("pk"));
		assert(!txtRecord.has("vv"));

		return DeviceInfo::APX;
	}
	else if (txtRecord.has("am") && txtRecord.test("am", "AppleTV.*") && !txtRecord.has("ek"))
	{
		// Apple TV

		assert(txtRecord.has("da") && txtRecord.get("da") == "true");
		assert(txtRecord.has("et") && txtRecord.test("et", "0,(2|3(,5)?)"));
		assert(txtRecord.has("md") && txtRecord.get("md") == "0,1,2");
		assert(txtRecord.has("sf") && txtRecord.test("sf", "0x[24c]{1,3}"));
		assert(txtRecord.has("tp") && txtRecord.test("tp", "(TCP,)?UDP"));
		assert(txtRecord.has("vn") && txtRecord.get("vn") == "65537");
		assert(txtRecord.has("vs") && txtRecord.test("vs", "\\d{3}\\.\\d{1,2}"));

		assert(!txtRecord.has("ft") || txtRecord.test("ft", "0x[0-9A-F]{7,8}(,0x[0-9A-F]{1,2})?"));
		assert(!txtRecord.has("pk") || txtRecord.test("pk", "[0-9a-f]{64}"));
		assert(!txtRecord.has("vv") || txtRecord.test("vv", "1|2"));

		assert(!txtRecord.has("ek"));
		assert(!txtRecord.has("fv"));
		assert(!txtRecord.has("sm"));

		return DeviceInfo::ATV;
	}
	else if (txtRecord.has("am") && txtRecord.test("am", "(PIONEER|DENON|AVR|DNP|HTR|JB2|NR|RX-|YH[AT]).*")
		&& txtRecord.has("md")) // old Klipsch G-17 states 'am=JB2 Gen' but doesn't support any metadata
	{
		// Audio-Video Receiver

		assert(txtRecord.test("md", "0,(1,)?2"));

		assert( txtRecord.has("da") && txtRecord.get("da") == "true");
		assert( txtRecord.has("et") && txtRecord.get("et") == "0,4");
		assert(!txtRecord.has("ft") || txtRecord.test("ft", "0x[0-9A-F]{7,8}"));
		assert( txtRecord.has("tp") && txtRecord.get("tp") == "UDP");
		assert(!txtRecord.has("sd") || txtRecord.get("sd") == "99");
		assert(!txtRecord.has("sf") || txtRecord.test("sf", "0x[4-6]"));
		assert( txtRecord.has("vn") && txtRecord.get("vn") == "65537");
		assert( txtRecord.has("vs") && txtRecord.test("vs", "\\d{3}\\.\\d{1,2}(\\.p\\d)?"));

		assert(!txtRecord.has("ek"));
		assert( txtRecord.has("fv"));
		assert(!txtRecord.has("pk"));
		assert(!txtRecord.has("sm"));
		assert(!txtRecord.has("vv"));

		return DeviceInfo::AVR;
	}

	int bits = 0;
	// dynamically determine encryption and metadata settings from TXT record
	if (txtRecord.has("md") && txtRecord.test("md", "0(,1)?(,2)?")) bits |= (1 << 0);
	if (txtRecord.has("md") && txtRecord.test("md", "(0,)?1(,2)?")) bits |= (1 << 1);
	if (txtRecord.has("md") && txtRecord.test("md", "(0,)?(1,)?2")) bits |= (1 << 2);
	if (txtRecord.has("ek") && txtRecord.get("ek") == "1")          bits |= (1 << 3);

	// store encryption and metadata settings with device type
	return DeviceInfo::DeviceType(MAKELONG(DeviceInfo::ANY, bits));
}
