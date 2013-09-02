//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//

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
	struct _stat timeStamp;
	if (_wstat(_pFileName, &timeStamp) || //error for retrieving timestamp
		(long(timeStamp.st_mtime>>32) != long(_timeStamp.st_mtime>>32)) ||  // or the timestamp not match previous saved one.
		(long(timeStamp.st_mtime) != long(_timeStamp.st_mtime)) )			// then load the config. skip otherwise
	{
		debugPrint(L"the file is updated");
	}


    const WCHAR* pWideBuffer = nullptr;

    _pReadBuffer = (const WCHAR *) new (std::nothrow) BYTE[ _fileSize ];
    if (!_pReadBuffer)
    {
        return FALSE;
    }

    DWORD dwNumberOfByteRead = 0;
    if (!ReadFile(_fileHandle, (LPVOID)_pReadBuffer, (DWORD)256, &dwNumberOfByteRead, NULL))
    {
        delete [] _pReadBuffer;
        _pReadBuffer = nullptr;
        return FALSE;
    }

    if (IsTextUnicode(_pReadBuffer, dwNumberOfByteRead, NULL) && _fileSize > sizeof(WCHAR))
    {
        // Read file in allocated buffer
        pWideBuffer = new (std::nothrow) WCHAR[ _fileSize/sizeof(WCHAR) - 1 ];
        if (!pWideBuffer)
        {
            delete [] _pReadBuffer;
            _pReadBuffer = nullptr;
            return FALSE;
        }

        // skip unicode byte-order signature
        SetFilePointer(_fileHandle, sizeof(WCHAR), NULL, FILE_BEGIN);

        if (!ReadFile(_fileHandle, (LPVOID)pWideBuffer, (DWORD)(_fileSize - sizeof(WCHAR)), &dwNumberOfByteRead, NULL))
        {
            delete [] pWideBuffer;
            delete [] _pReadBuffer;
            _pReadBuffer = nullptr;
            return FALSE;
        }

        _fileSize -= sizeof(WCHAR);
        delete [] _pReadBuffer;
        _pReadBuffer = pWideBuffer;
    }
    else
    {
        return FALSE;
    }

	 if (_fileHandle)
    {
        CloseHandle(_fileHandle);
        _fileHandle = nullptr;
    }

    return TRUE;
}

