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

#include "Private.h"
#include "Globals.h"
#include "DIME.h"

// from Register.cpp
BOOL RegisterProfiles();
void UnregisterProfiles();
BOOL RegisterCategories();
void UnregisterCategories();
BOOL RegisterServer();
void UnregisterServer();

void FreeGlobalObjects(void);

class CClassFactory;
static CClassFactory* classFactoryObjects[1] = { nullptr };

//+---------------------------------------------------------------------------
//
//  DllAddRef
//
//----------------------------------------------------------------------------

void DllAddRef(void)
{
	LONG refCount = InterlockedIncrement(&Global::dllRefCount);
	debugPrint(L"DllAddRef() Global::dllRefCount = %d ", refCount);
}

//+---------------------------------------------------------------------------
//
//  DllRelease
//
//----------------------------------------------------------------------------

void DllRelease(void)
{
	LONG refCount = InterlockedDecrement(&Global::dllRefCount);
	debugPrint(L"DllRelease() Global::dllRefCount = %d ", refCount);
    if (refCount < 0)
    {
        EnterCriticalSection(&Global::CS);

        if (nullptr != classFactoryObjects[0])
        {
            FreeGlobalObjects();
        }
        assert(refCount == -1); 

        LeaveCriticalSection(&Global::CS);
    }
}

//+---------------------------------------------------------------------------
//
//  CClassFactory declaration with IClassFactory Interface
//
//----------------------------------------------------------------------------

class CClassFactory : public IClassFactory
{
public:
    // IUnknown methods
    STDMETHODIMP QueryInterface(REFIID riid, _Outptr_ void **ppvObj);
    STDMETHODIMP_(ULONG) AddRef(void);
    STDMETHODIMP_(ULONG) Release(void);

    // IClassFactory methods
    STDMETHODIMP CreateInstance(_In_opt_ IUnknown *pUnkOuter, _In_ REFIID riid, _Inout_ void **ppvObj);
    STDMETHODIMP LockServer(BOOL fLock);

    // Constructor
    CClassFactory(REFCLSID rclsid, HRESULT (*pfnCreateInstance)(IUnknown *pUnkOuter, REFIID riid, void **ppvObj))
        : _rclsid(rclsid)
    {
        _pfnCreateInstance = pfnCreateInstance;
    }

public:
    REFCLSID _rclsid;
    HRESULT (*_pfnCreateInstance)(IUnknown *pUnkOuter, REFIID riid, _COM_Outptr_ void **ppvObj);
private:
	CClassFactory& operator=(const CClassFactory& rhn) {rhn;};
};

//+---------------------------------------------------------------------------
//
//  CClassFactory::QueryInterface
//
//----------------------------------------------------------------------------

STDAPI CClassFactory::QueryInterface(REFIID riid, _Outptr_ void **ppvObj)
{
	debugPrint(L"CClassFactory::QueryInterface()");
    if (IsEqualIID(riid, IID_IClassFactory) || IsEqualIID(riid, IID_IUnknown))
    {
        *ppvObj = this;
        DllAddRef();
        return NOERROR;
    }
    *ppvObj = nullptr;

    return E_NOINTERFACE;
}

//+---------------------------------------------------------------------------
//
//  CClassFactory::AddRef
//
//----------------------------------------------------------------------------

STDAPI_(ULONG) CClassFactory::AddRef()
{
	debugPrint(L"CClassFactory::AddRef()");
    DllAddRef();
    return (Global::dllRefCount + 1);
}

//+---------------------------------------------------------------------------
//
//  CClassFactory::Release
//
//----------------------------------------------------------------------------

STDAPI_(ULONG) CClassFactory::Release()
{
	debugPrint(L"CClassFactory::Release()");
    DllRelease();
    return (Global::dllRefCount + 1);
}

//+---------------------------------------------------------------------------
//
//  CClassFactory::CreateInstance
//
//----------------------------------------------------------------------------

STDAPI CClassFactory::CreateInstance(_In_opt_ IUnknown *pUnkOuter, _In_ REFIID riid, _Inout_ void **ppvObj)
{
	debugPrint(L"CClassFactory::CreateInstance() riid = %d", riid);
	return _pfnCreateInstance(pUnkOuter, riid, ppvObj);
	
}

//+---------------------------------------------------------------------------
//
//  CClassFactory::LockServer
//
//----------------------------------------------------------------------------

STDAPI CClassFactory::LockServer(BOOL fLock)
{
	debugPrint(L"CClassFactory::LockServer() fLock = %d", fLock);
    if (fLock)
    {
        DllAddRef();
    }
    else
    {
        DllRelease();
    }

    return S_OK;
}

//+---------------------------------------------------------------------------
//
//  BuildGlobalObjects
//
//----------------------------------------------------------------------------

void BuildGlobalObjects(void)
{
	debugPrint(L"BuildGlobalObjects()");
    classFactoryObjects[0] = new (std::nothrow) CClassFactory(Global::DIMECLSID, CDIME::CreateInstance);
}

//+---------------------------------------------------------------------------
//
//  FreeGlobalObjects
//
//----------------------------------------------------------------------------

void FreeGlobalObjects(void)
{
	debugPrint(L"BuildGlobalObjects()");
    for (int i = 0; i < ARRAYSIZE(classFactoryObjects); i++)
    {
        if (nullptr != classFactoryObjects[i])
        {
            delete classFactoryObjects[i];
            classFactoryObjects[i] = nullptr;
        }
    }

    DeleteObject(Global::defaultlFontHandle);
}

//+---------------------------------------------------------------------------
//
//  DllGetClassObject
//
//----------------------------------------------------------------------------
_Check_return_
STDAPI  DllGetClassObject(
	_In_ REFCLSID rclsid, 
	_In_ REFIID riid, 
	_Outptr_ void** ppv)
{
	debugPrint(L"DllGetClassObject()");
    if (classFactoryObjects[0] == nullptr)
    {
        EnterCriticalSection(&Global::CS);

        // need to check ref again after grabbing mutex
        if (classFactoryObjects[0] == nullptr)
        {
            BuildGlobalObjects();
        }

        LeaveCriticalSection(&Global::CS);
    }

    if (IsEqualIID(riid, IID_IClassFactory) ||
        IsEqualIID(riid, IID_IUnknown))
    {
        for (int i = 0; i < ARRAYSIZE(classFactoryObjects); i++)
        {
            if (nullptr != classFactoryObjects[i] &&
                IsEqualGUID(rclsid, classFactoryObjects[i]->_rclsid))
            {
                *ppv = (void *)classFactoryObjects[i];
                DllAddRef();    // class factory holds DLL ref count
                return NOERROR;
            }
        }
    }

    *ppv = nullptr;

    return CLASS_E_CLASSNOTAVAILABLE;
}

//+---------------------------------------------------------------------------
//
//  DllCanUnloadNow
//
//----------------------------------------------------------------------------

STDAPI DllCanUnloadNow(void)
{
	debugPrint(L"DllCanUnloadNow()");
    if (Global::dllRefCount >= 0)
    {
        return S_FALSE;
    }

    return S_OK;
}

//+---------------------------------------------------------------------------
//
//  DllUnregisterServer
//
//----------------------------------------------------------------------------

STDAPI DllUnregisterServer(void)
{
    UnregisterProfiles();
    UnregisterCategories();
    UnregisterServer();

    return S_OK;
}

//+---------------------------------------------------------------------------
//
//  DllRegisterServer
//
//----------------------------------------------------------------------------

STDAPI DllRegisterServer(void)
{
    if ((!RegisterServer()) || (!RegisterProfiles()) || (!RegisterCategories()))
    {
        DllUnregisterServer();
        return E_FAIL;
    }
    return S_OK;
}

