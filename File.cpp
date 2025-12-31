/* DIME IME for Windows 7/8/10/11

BSD 3-Clause License

Copyright (c) 2022, Jeremy Wu
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
//#define DEBUG_PRINT
#include "Globals.h"
#include "Private.h"
#include "File.h"
#include "BaseStructure.h"

//---------------------------------------------------------------------
//
// ctor
//
//---------------------------------------------------------------------

CFile::CFile(UINT codePage)
{
    _codePage = codePage;
    _fileHandle = nullptr;
    _pReadBuffer = nullptr;
    _fileSize = 0;
    _filePosPointer = 0;
    _pFileName = nullptr;

}

//---------------------------------------------------------------------
//
// dtor
//
//---------------------------------------------------------------------

CFile::~CFile()
{
    if (_pReadBuffer)
    {
        delete [] _pReadBuffer;
        _pReadBuffer = nullptr;
    }
    if (_fileHandle)
    {
        CloseHandle(_fileHandle);
        _fileHandle = nullptr;
    }
    if (_pFileName)
    {
        delete [] _pFileName;
        _pFileName = nullptr;
    }
}

//---------------------------------------------------------------------
//
// CreateFile
//
//---------------------------------------------------------------------

BOOL CFile::CreateFile(_In_ PCWSTR pFileName, DWORD desiredAccess,
    DWORD creationDisposition,
    DWORD sharedMode, _Inout_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD flagsAndAttributes, _Inout_opt_ HANDLE templateFileHandle)
{
	debugPrint(L" CFile::CreateFile() filename = %s", pFileName);
    size_t fullPathLen = wcslen(pFileName);
    if (!_pFileName)
    {
        _pFileName = new (std::nothrow) WCHAR[ fullPathLen + 1 ];
    }
    if (!_pFileName)
    {
        return FALSE;
    }

    StringCchCopyN(_pFileName, fullPathLen + 1, pFileName, fullPathLen);

	_fileHandle = ::CreateFile(_pFileName, desiredAccess, sharedMode,
        lpSecurityAttributes, creationDisposition, flagsAndAttributes, templateFileHandle);

    if (_fileHandle == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }

	_wstat(pFileName, & _timeStamp);
    _fileSize = ::GetFileSize(_fileHandle, NULL);

    return TRUE;
}

//---------------------------------------------------------------------
//
// SetupReadBuffer
//
//---------------------------------------------------------------------

BOOL CFile::SetupReadBuffer()
{
	
	debugPrint(L" CFile::CreateFile()");
   
	BOOL ret = TRUE;
	const WCHAR* pWideBuffer = new (std::nothrow) WCHAR[_fileSize / sizeof(WCHAR) - 1];

	DWORD dwNumberOfByteRead = 0;
	if(_fileSize < sizeof(WCHAR) ) 
	{
		ret = FALSE;
		goto errorExit;
	}
	// Read file in allocated buffer
	
	if (!pWideBuffer)
	{
		ret = FALSE;
		goto errorExit;
	}

	// skip unicode byte-order signature
	SetFilePointer(_fileHandle, sizeof(WCHAR), NULL, FILE_BEGIN);

	if (!ReadFile(_fileHandle, (LPVOID)pWideBuffer, (DWORD)(_fileSize - sizeof(WCHAR)), &dwNumberOfByteRead, NULL) 
		|| !IsTextUnicode(pWideBuffer, dwNumberOfByteRead, NULL))
	{
		delete [] pWideBuffer;
		ret = FALSE;
		goto errorExit;
	}
	

	_fileSize -= sizeof(WCHAR);
	delete [] _pReadBuffer;
	_pReadBuffer = pWideBuffer;

errorExit:
	if (_fileHandle)
    {
        CloseHandle(_fileHandle);
        _fileHandle = nullptr;
    }

    return ret;
}

_Ret_maybenull_
const WCHAR* CFile::GetReadBufferPointer(_Inout_opt_ BOOL *fileReloaded)
{
	debugPrint(L" CFile::GetReadBufferPointer()");
	if(fileReloaded) *fileReloaded = FALSE;
	if (!_pReadBuffer)
	{
		if (!SetupReadBuffer())
		{
			return nullptr;
		}
	}
	else
	{
		struct _stat timeStamp;
		if (_wstat(_pFileName, &timeStamp) || //error for retrieving timestamp
			(long(timeStamp.st_mtime>>32) != long(_timeStamp.st_mtime>>32)) ||  // or the timestamp not match previous saved one.
			(long(timeStamp.st_mtime) != long(_timeStamp.st_mtime)) )			// then load the config. skip otherwise
		{
			debugPrint(L"the file is updated and need to reload");
			if (_fileHandle)
			{
				CloseHandle(_fileHandle);
				_fileHandle = nullptr;
			}
			if (CreateFile(_pFileName, GENERIC_READ, OPEN_EXISTING, FILE_SHARE_READ))	
			{
				delete [] _pReadBuffer;
				_pReadBuffer = nullptr;
				if (!SetupReadBuffer())
				{
					return nullptr;
				}
				if(fileReloaded) *fileReloaded = TRUE;
			}
		}
	}
	return _pReadBuffer;
}

BOOL CFile::IsFileUpdated()
{
	debugPrint(L" CFile::IsFileUpdated()");
	struct _stat timeStamp;
	if (_wstat(_pFileName, &timeStamp) || //error for retrieving timestamp
		(long(timeStamp.st_mtime>>32) != long(_timeStamp.st_mtime>>32)) ||  // or the timestamp not match previous saved one.
		(long(timeStamp.st_mtime) != long(_timeStamp.st_mtime)) )			// then load the config. skip otherwise
		return TRUE;
	else 
		return FALSE;
}