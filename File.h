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
    const WCHAR *GetReadBufferPointer(_Inout_opt_ BOOL *fileReloaded = nullptr);
    DWORD_PTR GetFileSize() { return _fileSize;}

    LPCWSTR GetFileName() { return _pFileName;}
	BOOL IsFileUpdated();

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