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


#pragma once

class CRegKey
{
public:
    CRegKey();
    ~CRegKey();

    HKEY GetHKEY();

    LONG Create(_In_ HKEY hKeyPresent, _In_ LPCWSTR pwszKeyName,
        _In_reads_opt_(255) LPWSTR pwszClass = REG_NONE,
        DWORD dwOptions = REG_OPTION_NON_VOLATILE,
        REGSAM samDesired = KEY_READ | KEY_WRITE,
        _Inout_ LPSECURITY_ATTRIBUTES lpSecAttr = nullptr,
        _Out_opt_ LPDWORD lpdwDisposition = nullptr);

    LONG Open(_In_ HKEY hKeyParent, _In_ LPCWSTR pwszKeyName,
        REGSAM samDesired = KEY_READ | KEY_WRITE);

    LONG Close();

    LONG DeleteSubKey(_In_ LPCWSTR pwszSubKey);
    LONG RecurseDeleteKey(_In_ LPCWSTR pwszSubKey);
    LONG DeleteValue(_In_ LPCWSTR pwszValue);

    LONG QueryStringValue(_In_opt_ LPCWSTR pwszValueName, _Out_writes_opt_(*pnChars) LPWSTR pwszValue, _Inout_ ULONG *pnChars);
    LONG SetStringValue(_In_opt_ LPCWSTR pwszValueName, _In_ LPCWSTR pwszValue, DWORD dwType = REG_SZ);

    LONG QueryDWORDValue(_In_opt_ LPCWSTR pwszValueName, _Out_ DWORD &dwValue);
    LONG SetDWORDValue(_In_opt_ LPCWSTR pwszValueName, DWORD dwValue);

    LONG QueryBinaryValue(_In_opt_ LPCWSTR pwszValueName, _Out_writes_opt_(cbData) BYTE* lpData, DWORD cbData);
    LONG SetBinaryValue(_In_opt_ LPCWSTR pwszValueName, _In_reads_(cbData) BYTE* lpData, DWORD cbData);

private:
    HKEY  _keyHandle;
};
