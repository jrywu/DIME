//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//
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

    _fileHandle = ::CreateFile(pFileName, desiredAccess, sharedMode,
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
   
	DWORD dwNumberOfByteRead = 0;
	if(_fileSize < sizeof(WCHAR) ) 
		return FALSE;
	// Read file in allocated buffer
	const WCHAR* pWideBuffer  = new (std::nothrow) WCHAR[ _fileSize/sizeof(WCHAR) - 1 ];
	if (!pWideBuffer)
	{
		return FALSE;
	}

	// skip unicode byte-order signature
	SetFilePointer(_fileHandle, sizeof(WCHAR), NULL, FILE_BEGIN);

	if (!ReadFile(_fileHandle, (LPVOID)pWideBuffer, (DWORD)(_fileSize - sizeof(WCHAR)), &dwNumberOfByteRead, NULL) 
		|| !IsTextUnicode(pWideBuffer, dwNumberOfByteRead, NULL))
	{
		delete [] pWideBuffer;
		_pReadBuffer = nullptr;
		return FALSE;
	}
	

	_fileSize -= sizeof(WCHAR);
	delete [] _pReadBuffer;
	_pReadBuffer = pWideBuffer;

	if (_fileHandle)
    {
        CloseHandle(_fileHandle);
        _fileHandle = nullptr;
    }

    return TRUE;
}

const WCHAR* CFile::GetReadBufferPointer(BOOL *fileReloaded)
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