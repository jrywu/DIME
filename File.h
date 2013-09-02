//
//
// Derived from Microsoft Sample IME by Jeremy '13,7,17
//
//
#ifndef FILE_H
#define FILE_H
#pragma once

#include <sys/types.h> 
#include <wchar.h>

class CFile
{
public:
    CFile(UINT codePage = CP_ACP);
    virtual ~CFile();

    BOOL CreateFile(_In_ PCWSTR pFileName, DWORD desiredAccess, DWORD creationDisposition,
        DWORD sharedMode = 0, _Inout_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes = nullptr, DWORD flagsAndAttributes = 0, _Inout_opt_ HANDLE templateFileHandle = nullptr);

    _Ret_maybenull_
    const WCHAR *GetReadBufferPointer(BOOL *fileReloaded = nullptr);
    DWORD_PTR GetFileSize() { return _fileSize;}

    LPCWSTR GetFileName() { return _pFileName;}
	_stat GetTimeStamp() { return _timeStamp;}

protected:
    virtual BOOL SetupReadBuffer();


protected:
    const WCHAR* _pReadBuffer;   // read buffer memory.
    DWORD_PTR _fileSize;         // in byte.
    HANDLE _fileHandle;          // file handle for CreateFile

private:
    DWORD_PTR GetBufferInWCharLength()
    {
        if (_filePosPointer == 0 && _fileSize > 0)
        {
            // skip Unicode byte order mark
            GetReadBufferPointer();
        }
        return(_fileSize - _filePosPointer) / sizeof(WCHAR);    // in char count as a returned length.
    }

    const WCHAR *GetBufferInWChar()
    {
        const WCHAR *pwch = GetReadBufferPointer();
        return(const WCHAR*)((BYTE*)pwch + _filePosPointer);
    }

private:
    UINT _codePage;             // used for MultiByteToWideChar
    DWORD_PTR _filePosPointer;  // in byte. Always point start of line.
    LPWSTR _pFileName;

	struct _stat _timeStamp;
};
#endif