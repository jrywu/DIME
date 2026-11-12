// TestAppData.cpp — see TestAppData.h.
#include "pch.h"
#include "TestAppData.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace
{
    WCHAR g_default[MAX_PATH] = L"";

    void RemoveTree(const std::wstring& dir)
    {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                std::wstring name = fd.cFileName;
                if (name == L"." || name == L"..") continue;
                std::wstring path = dir + L"\\" + name;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    RemoveTree(path);
                } else {
                    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
                    DeleteFileW(path.c_str());
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        RemoveDirectoryW(dir.c_str());
    }
}

namespace DIMETestAppData {

const wchar_t* Default() { return g_default; }

void Restore()
{
    StringCchCopyW(Global::testAppDataOverride, MAX_PATH, g_default);
}

} // namespace DIMETestAppData

TEST_MODULE_INITIALIZE(IsolateAppData)
{
    WCHAR temp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, temp);
    StringCchPrintfW(g_default, MAX_PATH, L"%sDIMETests_AppData_%lu", temp, GetCurrentProcessId());
    RemoveTree(g_default);
    CreateDirectoryW(g_default, nullptr);
    DIMETestAppData::Restore();
}

TEST_MODULE_CLEANUP(RemoveAppData)
{
    Global::testAppDataOverride[0] = L'\0';
    RemoveTree(g_default);
}
