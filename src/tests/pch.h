// pch.h: This is a precompiled header file.
// Files listed below are compiled only once, improving build performance for future builds.
// This also affects IntelliSense performance, including code completion and many code browsing features.
// However, files listed here are ALL re-compiled if any one of them is updated between builds.
// Do not add files here that you will be updating frequently as this negates the performance advantage.

#ifndef PCH_H
#define PCH_H

// Windows Headers
#include <windows.h>
#include <ole2.h>
#include <olectl.h>
#include <assert.h>
#include <strsafe.h>
#include <shlobj.h>
#include <sys/stat.h>

// Standard Library
#include <vector>
#include <thread>
#include <atomic>
#include <string>

// Microsoft C++ Unit Test Framework
#include "CppUnitTest.h"

// DIME Headers - include paths via AdditionalIncludeDirectories
#include "../define.h"
#include "../BaseStructure.h"
#include "../Globals.h"
#include "../Config.h"

#endif //PCH_H
