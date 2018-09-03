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

#include "WinampPlayer.h"
#include "Platform.inl"
#include "Debugger.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <exception>
#include <limits>
#include <string>
#include <vector>
#include <atlenc.h>
#include <comdef.h>
#include <objbase.h>
#include <windows.h>

#include <mm_h.h>
#include <mm_i.c>
#include <wa_ipc.h>
#include <wasabi/api_albumart.h>
#include <wasabi/api_memmgr.h>
#include <wasabi/api_service.h>
#include <wasabi/svc_albumArtProvider.h>
#include <wasabi/svc_imgwrite.h>
#include <wasabi/waservicefactory.h>

#pragma region support functions

using std::exception;


static buffer_t ToJPEG(ARGB32* const data, const int wdth, const int hght,
	api_service* const apiService, api_memmgr* const memMgmtService);


static std::string ToUTF8(const wchar_t* const wideCharString)
{
	assert(wideCharString != NULL); std::string multiByteString;
	Platform::Charset::toUTF8(wideCharString, multiByteString);

	return multiByteString;
}


static void SendMessageWithTimeout(HWND hWnd, UINT uMsg, LPARAM lPrm, WPARAM wPrm,
	DWORD* const pErr = NULL, DWORD_PTR* const pRet = NULL)
{
	if (pErr) *pErr = 0;
	if (pRet) *pRet = 0;

	if (!SendMessageTimeout(hWnd, uMsg, wPrm, lPrm, SMTO_NORMAL, 250, pRet))
	{
		if (pErr) *pErr = GetLastError();
		if (pRet) *pRet = 0;
	}
}


static void GetBasicFileInfo(time_t& length, const wchar_t* const filePath, HWND hWnd)
{
	basicFileInfoStructW fileInfo;
	fileInfo.filename = filePath;
	fileInfo.quickCheck = 0;
	fileInfo.title = NULL;
	fileInfo.titlelen = 0;
	fileInfo.length = 0;

	SendMessageWithTimeout(hWnd, WM_WA_IPC, IPC_GET_BASIC_FILE_INFOW, (WPARAM) &fileInfo);
	if (fileInfo.length > 0)
	{
		length = static_cast<time_t>(fileInfo.length) * 1000;
	}
}


static void GetTextualFileInfo(std::string& infoText, const wchar_t* const infoType,
	const wchar_t* const filePath, HWND hWnd)
{
	wchar_t wideCharString[128] = {L'\0'};

	extendedFileInfoStructW fileInfo;
	fileInfo.filename = filePath;
	fileInfo.metadata = infoType;
	fileInfo.ret = wideCharString;
	fileInfo.retlen = (sizeof(wideCharString) / sizeof(wchar_t)) - 1;

	SendMessageWithTimeout(hWnd, WM_WA_IPC, IPC_GET_EXTENDED_FILE_INFOW, (WPARAM) &fileInfo);
	if (fileInfo.ret[0] != L'\0')
	{
		infoText = ToUTF8(fileInfo.ret);
	}
}


static void GetVisualFileInfo(buffer_t& imageData, std::string& imageType,
	const wchar_t* const infoType, const wchar_t* const filePath,
	api_service* const apiService, api_memmgr* const memMgmtService)
{
	const size_t n = apiService->service_getNumServices(
						svc_albumArtProvider::getServiceType());
	for (size_t i = 0; i < n; ++i)
	{
		waServiceFactory* const aapServiceFactory =
			apiService->service_enumService(svc_albumArtProvider::getServiceType(), i);
		if (aapServiceFactory != NULL)
		{
			svc_albumArtProvider* const aapService =
				(svc_albumArtProvider*) aapServiceFactory->getInterface();
			if (aapService != NULL)
			{
				void*     data = 0;
				size_t    size = 0;
				wchar_t*  type = 0;
				const int code = aapService->GetAlbumArtData(
					filePath, infoType, &data, &size, &type);
				if (code == ALBUMARTPROVIDER_SUCCESS && data != 0)
				{
					if (size != 0 && type != 0)
					{
						imageData.resize(size);
						std::memcpy(&imageData[0], data, size);
						imageType = "image/" + ToUTF8(type);

						i = n; // break out of loop after cleanup
					}

					if (data) memMgmtService->sysFree(data);
					if (type) memMgmtService->sysFree(type);
				}

				aapServiceFactory->releaseInterface(aapService);
			}

			apiService->service_release(aapServiceFactory);
		}
	}
}


static void GetVisualFileInfo2(buffer_t& imageData, std::string& imageType,
	const wchar_t* const infoType, const wchar_t* const filePath,
	api_service* const apiService, api_memmgr* const memMgmtService)
{
	waServiceFactory* const aaServiceFactory =
		apiService->service_getServiceByGuid(albumArtGUID);
	if (aaServiceFactory != NULL)
	{
		api_albumart* const aaService =
			(api_albumart*) aaServiceFactory->getInterface();
		if (aaService != NULL)
		{
			int       wdth = 0;
			int       hght = 0;
			ARGB32*   data = 0;
			const int code = aaService->GetAlbumArt(
				filePath, infoType, &wdth, &hght, &data);
			if (code == ALBUMART_SUCCESS && data != 0)
			{
				if (wdth > 0 && hght > 0)
				{
					imageData = ToJPEG(data, wdth, hght, apiService, memMgmtService);
					imageType = "image/jpeg";
				}

				if (data) memMgmtService->sysFree(data);
			}

			aaServiceFactory->releaseInterface(aaService);
		}

		apiService->service_release(aaServiceFactory);
	}
}


static buffer_t ToJPEG(ARGB32* const data, const int wdth, const int hght,
	api_service* const apiService, api_memmgr* const memMgmtService)
{
	buffer_t jpeg(0);

	const size_t n = apiService->service_getNumServices(
						svc_imageWriter::getServiceType());
	for (size_t i = 0; i < n; ++i)
	{
		waServiceFactory* const imgServiceFactory =
			apiService->service_enumService(svc_imageWriter::getServiceType(), i);
		if (imgServiceFactory != NULL)
		{
			svc_imageWriter* const imgService =
				(svc_imageWriter*) imgServiceFactory->getInterface();
			if (imgService != NULL)
			{
				if (imgService->bitDepthSupported(32)
					&& imgService->getImageTypeName() != NULL
					&& imgService->getImageTypeName() == std::wstring(L"JPEG"))
				{
					int   jpegSize = 0;
					void* jpegData = imgService->convert(data, 32, wdth, hght, &jpegSize);
					if (jpegData != NULL && jpegSize > 0)
					{
						jpeg.resize(jpegSize);
						std::memcpy(&jpeg[0], jpegData, jpegSize);
					}

					if (jpegData) memMgmtService->sysFree(jpegData);
				}

				imgServiceFactory->releaseInterface(imgService);
			}

			apiService->service_release(imgServiceFactory);
		}
	}

	return jpeg;
}


static inline bool IsMediaMonkey()
{
	return (FindWindow(TEXT("TFMainWindow"), NULL) != NULL);
}


static inline HWND GetMusicBeeIPC()
{
	return FindWindow(NULL, TEXT("MusicBee IPC Interface"));
}


static inline bool IsMusicBee()
{
	HWND hwnd = GetMusicBeeIPC();
	if (hwnd != NULL)
	{
		// check if the MusicBee IPC window is in the same process as the player

		HINSTANCE exeInstance = GetModuleHandle(NULL);
		HINSTANCE ipcInstance = (HINSTANCE) GetWindowLongPtr(hwnd, GWLP_HINSTANCE);

		return (exeInstance != NULL && exeInstance == ipcInstance);
	}
	return false;
}


static DWORD_PTR SendMusicBeeIPC(WPARAM wParam, LPARAM lParam = 0, HWND hWnd = GetMusicBeeIPC())
{
	DWORD error;
	DWORD_PTR result;

	assert(wParam > 0);
	assert(IsMusicBee());

	SendMessageWithTimeout(hWnd, WM_USER, lParam, wParam, &error, &result);

	if (error == ERROR_TIMEOUT)
	{
		Debugger::printf("%s(%i, %li) timed out", __FUNCTION__, wParam, lParam);
	}
	else if (error)
	{
		Debugger::printf("%s(%i, %li) failed with error: %s", __FUNCTION__, wParam, lParam, Platform::Error::describe(error).c_str());
	}

	return result;
}


static std::string ReadIpcText(LRESULT result, HWND window)
{
	std::string string;

	BYTE idx = LOBYTE(result), off = HIBYTE(result);
	char mmf[32]; sprintf_s(mmf, 32, "mbipc_mmf_%d", (int) idx);

	HANDLE mmfh = OpenFileMappingA(FILE_MAP_READ, FALSE, mmf);
	if (mmfh != NULL)
	{
		LPVOID view = MapViewOfFile(mmfh, FILE_MAP_READ, 0, 0, 0);
		if (view != NULL)
		{
			char* mptr = (char*) view;

			mptr += 8;
			mptr += off;
			assert(off == 0);

			int32_t bytes;
			std::memcpy(&bytes, mptr, 4);
			if (bytes > 0 && bytes < 1000000)
			{
				std::wstring wchars((bytes / 2) + 1, 0);
				std::memcpy(&wchars[0], mptr + 4, bytes);

				string = ToUTF8(&wchars[0]);
			}

			UnmapViewOfFile(view);
		}

		CloseHandle(mmfh);
	}

	// release the memory-mapped file
	SendMusicBeeIPC(900, result, window);

	return string;
}


static DWORD mmCookie = 0;
extern "C" __declspec(dllexport)
void MMSetInterfaceCookie(DWORD cookie)
{
	mmCookie = cookie;
}


struct hresult_error : exception
{
	hresult_error(HRESULT result)
	: exception(hresult_string(result).c_str())
	{
	}
	hresult_error(HRESULT result, const char* const context)
	: exception(hresult_string(result).append(" from ").append(context).c_str())
	{
	}
	static std::string hresult_string(HRESULT result)
	{
		return ToUTF8(_com_error(result).ErrorMessage());
	}
};

#define DO(exp) {HRESULT hr(exp); if (FAILED(hr)) throw hresult_error(hr,#exp);}

#pragma endregion

//------------------------------------------------------------------------------


WinampPlayer::WinampPlayer(HWND& playerWindow)
:
	_apiService(NULL),
	_memMgmtService(NULL),
	_memMgmtServiceFactory(NULL),
	_playerWindow(playerWindow)
{
}


void WinampPlayer::play()
{
	if (!IsMusicBee())
	{
		sendCommand(40045);
	}
	else
	{
		SendMusicBeeIPC(101);
	}
}


void WinampPlayer::pause()
{
	if (!IsMusicBee())
	{
		sendCommand(40046);
	}
	else
	{
		SendMusicBeeIPC(102);
	}
}


void WinampPlayer::stop()
{
	if (!IsMusicBee())
	{
		sendCommand(40047);
	}
	else
	{
		SendMusicBeeIPC(103);
	}
}


void WinampPlayer::restart()
{
	if (!IsMusicBee())
	{
		// 1 is playing, 3 is paused, 0 is stopped
		const uint8_t status = sendMessage<uint8_t>(IPC_ISPLAYING);

		// if player is playing, seek to start of track
		if (status > 0) sendMessage<void>(IPC_JUMPTOTIME, 0);

		// if player is paused, resume playback
		if (status == 3) play();
	}
	else
	{
		DWORD_PTR result = SendMusicBeeIPC(109);
		if (result > 1)
		{
			// 3 is playing, 6 is paused, 7 is stopped
			const uint8_t status = static_cast<uint8_t>(result);

			// if player is playing, seek to start of track
			if (status == 3 || status == 6) SendMusicBeeIPC(111, 0);

			// if playing, pause so position metadata is updated when resuming
			if (status == 3) pause();

			// resume playback
			play();
		}
	}
}


void WinampPlayer::startNext()
{
	if (!IsMusicBee())
	{
		sendCommand(40048);
	}
	else
	{
		SendMusicBeeIPC(106);
	}
}


void WinampPlayer::startPrev()
{
	if (!IsMusicBee())
	{
		sendCommand(40044);
	}
	else
	{
		SendMusicBeeIPC(105);
	}
}


void WinampPlayer::increaseVolume()
{
	if (!IsMusicBee())
	{
		// each command raises volume by ~2%
		sendCommand(40058); sendCommand(40058);
	}
	else
	{
		DWORD_PTR result = SendMusicBeeIPC(112);
		if (result <= 100)
		{
			SendMusicBeeIPC(113, std::min(int(result) + 4, 100));
		}
	}
}


void WinampPlayer::decreaseVolume()
{
	if (!IsMusicBee())
	{
		// each command lowers volume by ~2%
		sendCommand(40059); sendCommand(40059);
	}
	else
	{
		DWORD_PTR result = SendMusicBeeIPC(112);
		if (result <= 100)
		{
			SendMusicBeeIPC(113, std::max(int(result) - 4, 0));
		}
	}
}


void WinampPlayer::toggleMute()
{
	if (!IsMusicBee())
	{
		static uint8_t previousVolume = 0;

		// send 'get volume' message to player
		const uint8_t volume = sendMessage<uint8_t>(IPC_SETVOLUME, -666);

		if (volume > 0) sendMessage<void>(IPC_SETVOLUME, 0);
		else sendMessage<void>(IPC_SETVOLUME, previousVolume);

		previousVolume = volume;
	}
	else
	{
		SendMusicBeeIPC(119, !SendMusicBeeIPC(118));
	}
}


void WinampPlayer::toggleShuffle()
{
	if (!IsMusicBee())
	{
		sendMessage<void>(IPC_SET_SHUFFLE, !sendMessage<int>(IPC_GET_SHUFFLE));
	}
	else
	{
		SendMusicBeeIPC(121, !SendMusicBeeIPC(120));
	}
}


//------------------------------------------------------------------------------


time_t WinampPlayer::getPlaybackMetadata(std::string& title, std::string& album,
	std::string& artist, buffer_t& artworkData, std::string& artworkType, shorts_t& listpos) const
{
	time_t length = 0; listpos = shorts_t(0,0);
	title.clear(), album.clear(), artist.clear();
	artworkData.resize(0), artworkType.assign("image/none");

	const wchar_t* const filePath = sendMessage<wchar_t*>(IPC_GET_PLAYING_FILENAME);
	if (filePath != NULL && filePath[0] != L'\0')
	{
		GetBasicFileInfo(length, filePath, window());
		GetTextualFileInfo(title, L"Title", filePath, window());
		GetTextualFileInfo(album, L"Album", filePath, window());
		GetTextualFileInfo(artist, L"Artist", filePath, window());

		if (_apiService == NULL)
		{
			// try to initialize player service interfaces
			const_cast<WinampPlayer*>(this)->initServices();
		}
		if (_apiService != NULL && _memMgmtService != NULL)
		{
			GetVisualFileInfo(artworkData, artworkType, L"cover",
							filePath, _apiService, _memMgmtService);

			if (artworkData.empty())
			{
				// try an alternate API for getting artwork
				GetVisualFileInfo2(artworkData, artworkType, L"cover",
								filePath, _apiService, _memMgmtService);
			}
		}
	}

	if (length == 0)
	{
		// try an alternate API for getting track length
		length = sendMessage<time_t>(IPC_GETOUTPUTTIME, 2);
	}

	// query player for the current playlist's length and cursor position
	static const unsigned short_max = std::numeric_limits<short>::max();
	const unsigned len = sendMessage<unsigned>(IPC_GETLISTLENGTH);
	if (len > 0 && len <= short_max)
	{
		const unsigned pos = sendMessage<unsigned>(IPC_GETLISTPOS);
		if (pos <= short_max)
		{
			assert(pos < len);
			listpos = std::make_pair(pos + 1, len);
		}
	}

	if (title.empty())
	{
		// try an alternate API for getting track title
		const wchar_t* const wideCharTitle = sendMessage<wchar_t*>(IPC_GET_PLAYING_TITLE);
		if (wideCharTitle != NULL && wideCharTitle[0] != L'\0')
		{
			title = ToUTF8(wideCharTitle);
		}
	}

	if ((length == 0 || title.empty()) && IsMediaMonkey())
	{
		// try MediaMonkey's COM API for getting track metadata

		CComPtr<ISDBApplication > sdbApplication  = NULL;
		CComPtr<ISDBPlayer      > sdbPlayer       = NULL;
		CComPtr<ISDBSongData    > sdbSongData     = NULL;
		CComPtr<ISDBAlbumArtList> sdbAlbumArtList = NULL;
		CComPtr<ISDBAlbumArtItem> sdbAlbumArtItem = NULL;
		CComPtr<ISDBImage       > sdbImage        = NULL;

		BSTR imageType  = NULL;
		BSTR songTitle  = NULL;
		BSTR songAlbum  = NULL;
		BSTR songArtist = NULL;

		CoInitialize(NULL);

		try
		{
			if (mmCookie > 0)
			{
				try { CComPtr<IGlobalInterfaceTable> pGIT;
				DO (pGIT.CoCreateInstance(CLSID_StdGlobalInterfaceTable, NULL, CLSCTX_INPROC_SERVER));
				DO (pGIT->GetInterfaceFromGlobal(mmCookie, IID_ISDBApplication, (LPVOID*) &sdbApplication));
				} CATCH_ALL
			}
			if (sdbApplication == NULL)
			{
				DO (sdbApplication.CoCreateInstance(CLSID_SDBApplication, NULL, CLSCTX_LOCAL_SERVER));
			}

			DO (sdbApplication->get_Player(&sdbPlayer));

			DO (sdbPlayer->get_CurrentSong(&sdbSongData));

			long songLength = 0;
			DO (sdbSongData->get_SongLength(&songLength));
			if (songLength > 0) length = songLength;

			DO (sdbSongData->get_Title(&songTitle));
			if (songTitle != NULL && songTitle[0] != L'\0') title = ToUTF8(songTitle);

			if (title.empty())
			{
				BSTR songPath = NULL;
				DO (sdbSongData->get_Path(&songPath));
				if (songPath != NULL && songPath[0] != L'\0')
				{
					const std::string path = ToUTF8(songPath);  char file[MAX_PATH];
					title = (_splitpath_s(path.c_str(), NULL, 0, NULL, 0, file, sizeof(file), NULL, 0) ? path : file);
				}
				if (songPath != NULL) SysFreeString(songPath);
			}

			DO (sdbSongData->get_AlbumName(&songAlbum));
			if (songAlbum != NULL && songAlbum[0] != L'\0') album = ToUTF8(songAlbum);

			DO (sdbSongData->get_ArtistName(&songArtist));
			if (songArtist != NULL && songArtist[0] != L'\0') artist = ToUTF8(songArtist);

			DO (sdbSongData->get_AlbumArt(&sdbAlbumArtList));

			long imageCount = 0;
			DO (sdbAlbumArtList->get_Count(&imageCount));
			if (imageCount > 0)
			{
				DO (sdbAlbumArtList->get_Item(0, &sdbAlbumArtItem));

				DO (sdbAlbumArtItem->get_Image(&sdbImage));

				long imageData = 0;
				DO (sdbImage->get_ImageData(&imageData));

				long imageSize = 0;
				DO (sdbImage->get_ImageDataLen(&imageSize));

				DO (sdbImage->get_ImageFormat(&imageType));
				if (imageType != NULL && imageType[0] != L'\0')
				{
					artworkData.resize(imageSize);
					std::memcpy(&artworkData[0], (void*) imageData, imageSize);
					artworkType = ToUTF8(imageType);
				}
			}
		}
		CATCH_ALL

		if (imageType != NULL)  SysFreeString(imageType);
		if (songTitle != NULL)  SysFreeString(songTitle);
		if (songAlbum != NULL)  SysFreeString(songAlbum);
		if (songArtist != NULL) SysFreeString(songArtist);

		CoUninitialize();
	}
	else if ((length == 0 || title.empty()) && IsMusicBee())
	{
		// try MusicBee's IPC API for getting track metadata

		DWORD_PTR result; HWND window = GetMusicBeeIPC();

		result = SendMusicBeeIPC(139, 00, window);
		if (result > 0) length = (time_t) result;

		result = SendMusicBeeIPC(154, 00, window);
		if (result >= 0 && result < short_max) listpos.first = 1 + (short) result;

		result = SendMusicBeeIPC(208, 00, window);
		if (result > 0 && result <= short_max) listpos.second = (short) result;

		result = SendMusicBeeIPC(142, 65, window);
		if (result > 0) title = ReadIpcText(result, window);

		if (title.empty())
		{
			result = SendMusicBeeIPC(140, 00, window);
			if (result > 0)
			{
				const std::string path = ReadIpcText(result, window);  char file[MAX_PATH];
				title = (_splitpath_s(path.c_str(), NULL, 0, NULL, 0, file, sizeof(file), NULL, 0) ? path : file);
			}
		}

		result = SendMusicBeeIPC(142, 30, window);
		if (result > 0) album = ReadIpcText(result, window);

		result = SendMusicBeeIPC(142, 32, window);
		if (result > 0) artist = ReadIpcText(result, window);

		result = SendMusicBeeIPC(145, 00, window);
		if (result > 0) {
			const std::string data(ReadIpcText(result, window));
			if (data.size() > 25) { // some minimum image size required
				int size = (data.size() * 3) / 4; artworkData.resize(size);
				if (Base64Decode(&data[0], data.size(), &artworkData[0], &size)
					&& std::memcmp(&artworkData[6], "JFIF", 4) == 0) // JPEG tag
				{
					artworkData.resize(size);
					artworkType = "image/jpeg";
				}
				else artworkData.clear();
			}
		}
	}

	return std::max<time_t>(length, 0);
}


time_t WinampPlayer::getPlaybackPosition() const
{
	time_t offset = 0;

	if (!IsMusicBee())
	{
		offset = sendMessage<time_t>(IPC_GETOUTPUTTIME);
	}
	else
	{
		offset = static_cast<time_t>(SendMusicBeeIPC(110));
	}

	return std::max<time_t>(offset, 0);
}


//------------------------------------------------------------------------------


void WinampPlayer::freeServices()
{
	if (_apiService != NULL)
	{
		if (_memMgmtServiceFactory != NULL)
		{
			if (_memMgmtService != NULL)
			{
				_memMgmtServiceFactory->releaseInterface(_memMgmtService);
				_memMgmtService = NULL;
			}

			_apiService->service_release(_memMgmtServiceFactory);
			_memMgmtServiceFactory = NULL;
		}

		_apiService = NULL;
	}
}


void WinampPlayer::initServices()
{
	_apiService = sendMessage<api_service*>(IPC_GET_API_SERVICE);
	if (_apiService == reinterpret_cast<api_service*>(1))
	{
		// service interface not supported
		_apiService = NULL;
	}
	else if (_apiService != NULL)
	{
		_memMgmtServiceFactory = _apiService->service_getServiceByGuid(memMgrApiServiceGuid);
		if (_memMgmtServiceFactory == NULL)
		{
			freeServices();
			return;
		}

		_memMgmtService = static_cast<api_memmgr*>(_memMgmtServiceFactory->getInterface());
		if (_memMgmtService == NULL)
		{
			freeServices();
			return;
		}
	}
}


void WinampPlayer::sendCommand(const int param) const
{
	DWORD error;
	DWORD_PTR result;

	assert(param > 0);
	assert(window() > 0);

	SendMessageWithTimeout(window(), WM_COMMAND, (LPARAM) 0, (WPARAM) param, &error, &result);

	if (error == ERROR_TIMEOUT)
	{
		Debugger::printf("%s(%i) timed out", __FUNCTION__, param);
	}
	else if (error)
	{
		Debugger::printf("%s(%i) failed with error: %s", __FUNCTION__, param, Platform::Error::describe(error).c_str());
	}
	else if (result)
	{
		Debugger::printf("%s(%i) not accepted by target", __FUNCTION__, param);
	}
}

template <typename type>
type WinampPlayer::sendMessage(const long lParam, const int wParam) const
{
	DWORD error;
	DWORD_PTR result;

	assert(lParam > 0);
	assert(window() > 0);

	SendMessageWithTimeout(window(), WM_WA_IPC, (LPARAM) lParam, (WPARAM) wParam, &error, &result);

	if (error == ERROR_TIMEOUT)
	{
		Debugger::printf("%s(%li, %i) timed out", __FUNCTION__, lParam, wParam);
	}
	else if (error)
	{
		Debugger::printf("%s(%li, %i) failed with error: %s", __FUNCTION__, lParam, wParam, Platform::Error::describe(error).c_str());
	}

	return (type) result;
}
