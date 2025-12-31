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
#include "Globals.h"
#include "Private.h"
#include "DIME.h"

class CKeyStateCategory;

class CKeyStateCategoryFactory
{
public:
    static CKeyStateCategoryFactory* Instance();
    CKeyStateCategory* MakeKeyStateCategory(KEYSTROKE_CATEGORY keyCategory, _In_ CDIME *pTextService);
    void Release();

protected:
    CKeyStateCategoryFactory();

private:
    static CKeyStateCategoryFactory* _instance;

};

typedef struct KeyHandlerEditSessionDTO
{
    KeyHandlerEditSessionDTO::KeyHandlerEditSessionDTO(TfEditCookie tFEC, _In_ ITfContext *pTfContext, UINT virualCode, WCHAR inputChar, KEYSTROKE_FUNCTION arrowKeyFunction)
    {
        ec = tFEC;
        pContext = pTfContext;
        code = virualCode;
        wch = inputChar;
        arrowKey = arrowKeyFunction;
    }

    TfEditCookie ec;
    ITfContext* pContext;
    UINT code;
    WCHAR wch;
    KEYSTROKE_FUNCTION arrowKey;
}KeyHandlerEditSessionDTO;

class CKeyStateCategory
{
public:
    CKeyStateCategory(_In_ CDIME *pTextService);

protected:
    ~CKeyStateCategory(void);

public:
    HRESULT KeyStateHandler(KEYSTROKE_FUNCTION function, KeyHandlerEditSessionDTO dto);
    void Release(void);

protected:
    // HandleKeyInput
    virtual HRESULT HandleKeyInput(KeyHandlerEditSessionDTO dto);

    // HandleKeyInputAndConvert
    virtual HRESULT HandleKeyInputAndConvert(KeyHandlerEditSessionDTO dto);

    // HandleKeyInputAndConvertWildCard
    virtual HRESULT HandleKeyInputAndConvertWildCard(KeyHandlerEditSessionDTO dto);

    // HandleKeyFinalizeTextStoreAndInput
    virtual HRESULT HandleKeyFinalizeTextStoreAndInput(KeyHandlerEditSessionDTO dto);

    // HandleKeyFinalizeTextStore
    virtual HRESULT HandleKeyFinalizeTextStore(KeyHandlerEditSessionDTO dto);

    // HandleKeyFinalizeCandidatelistAndInput
    virtual HRESULT HandleKeyFinalizeCandidatelistAndInput(KeyHandlerEditSessionDTO dto);

    // HandleKeyFinalizeCandidatelist
    virtual HRESULT HandleKeyFinalizeCandidatelist(KeyHandlerEditSessionDTO dto);

    // HandleKeyConvert
    virtual HRESULT HandleKeyConvert(KeyHandlerEditSessionDTO dto);

    // HandleKeyConvertWild
    virtual HRESULT HandleKeyConvertWildCard(KeyHandlerEditSessionDTO dto);

    // HandleKeyConvertArrayPhrase
    virtual HRESULT HandleKeyConvertArrayPhrase(KeyHandlerEditSessionDTO dto);

    // HandleKeyConvertArrayPhrase
    virtual HRESULT HandleKeyConvertArrayPhraseWildCard(KeyHandlerEditSessionDTO dto);

    // HandleKeyCancel
    virtual HRESULT HandleKeyCancel(KeyHandlerEditSessionDTO dto);

    // HandleKeyBackspace
    virtual HRESULT HandleKeyBackspace(KeyHandlerEditSessionDTO dto);

    // HandleKeyArrow
    virtual HRESULT HandleKeyArrow(KeyHandlerEditSessionDTO dto);

    // HandleKeyDoubleSingleByte
    virtual HRESULT HandleKeyDoubleSingleByte(KeyHandlerEditSessionDTO dto);

    // HandleKeyPunctuation
    virtual HRESULT HandleKeyPunctuation(KeyHandlerEditSessionDTO dto);

    // HandleKeySelectByNumber
    virtual HRESULT HandleKeySelectByNumber(KeyHandlerEditSessionDTO dto);

	// HandleKeyAddressChar
    virtual HRESULT HandleKeyAddressChar(KeyHandlerEditSessionDTO dto);

	
protected:
    CDIME* _pTextService;
};

class CKeyStateComposing : public CKeyStateCategory
{
public:
    CKeyStateComposing(_In_ CDIME *pTextService);

protected:
    // _HandleCompositionInput
    HRESULT HandleKeyInput(KeyHandlerEditSessionDTO dto);

    // _HandleCompositionInputAndConvert
    HRESULT HandleKeyInputAndConvert(KeyHandlerEditSessionDTO dto);

    // _HandleCompositionInputAndConvertWildCard
    HRESULT HandleKeyInputAndConvertWildCard(KeyHandlerEditSessionDTO dto);

    // HandleKeyCompositionFinalizeTextStoreAndInput
    HRESULT HandleKeyFinalizeTextStoreAndInput(KeyHandlerEditSessionDTO dto);

    // HandleKeyFinalizeTextStore
    HRESULT HandleKeyFinalizeTextStore(KeyHandlerEditSessionDTO dto);

    // HandleKeyCompositionFinalizeCandidatelistAndInput
    HRESULT HandleKeyFinalizeCandidatelistAndInput(KeyHandlerEditSessionDTO dto);

    // HandleKeyCompositionFinalizeCandidatelist
    HRESULT HandleKeyFinalizeCandidatelist(KeyHandlerEditSessionDTO dto);

    // HandleCompositionConvert
    HRESULT HandleKeyConvert(KeyHandlerEditSessionDTO dto);

    // HandleKeyCompositionConvertWildCard
    HRESULT HandleKeyConvertWildCard(KeyHandlerEditSessionDTO dto);
    
    // HandleCompositionConvertArrayPhrase
    HRESULT HandleKeyConvertArrayPhrase(KeyHandlerEditSessionDTO dto);

    // HandleCompositionConvertArrayPhrase
    HRESULT HandleKeyConvertArrayPhraseWildCard(KeyHandlerEditSessionDTO dto);

    // HandleCancel
    HRESULT HandleKeyCancel(KeyHandlerEditSessionDTO dto);

    // HandleCompositionBackspace
    HRESULT HandleKeyBackspace(KeyHandlerEditSessionDTO dto);

    // HandleArrowKey
    HRESULT HandleKeyArrow(KeyHandlerEditSessionDTO dto);

    // HandleKeyDoubleSingleByte
    HRESULT HandleKeyDoubleSingleByte(KeyHandlerEditSessionDTO dto);

	// HandleKeyAddressChar
    HRESULT HandleKeyAddressChar(KeyHandlerEditSessionDTO dto);
	

};

class CKeyStateCandidate : public CKeyStateCategory
{
public:
    CKeyStateCandidate(_In_ CDIME *pTextService);

protected:
    // HandleKeyFinalizeCandidatelist
    HRESULT HandleKeyFinalizeCandidatelist(KeyHandlerEditSessionDTO dto);

    // HandleKeyFinalizeCandidatelistAndInput
    HRESULT HandleKeyFinalizeCandidatelistAndInput(KeyHandlerEditSessionDTO dto);

    //_HandleCandidateConvert
    HRESULT HandleKeyConvert(KeyHandlerEditSessionDTO dto);

    //_HandleCancel
    HRESULT HandleKeyCancel(KeyHandlerEditSessionDTO dto);

    //_HandleCandidateArrowKey
    HRESULT HandleKeyArrow(KeyHandlerEditSessionDTO dto);

    //_HandleCandidateSelectByNumber
    HRESULT HandleKeySelectByNumber(KeyHandlerEditSessionDTO dto);
};

class CKeyStatePhrase : public CKeyStateCategory
{
public:
    CKeyStatePhrase(_In_ CDIME *pTextService);

protected:
    //_HandleCancel
    HRESULT HandleKeyFinalizeCandidatelist(KeyHandlerEditSessionDTO dto);

    //_HandleCancel
    HRESULT HandleKeyCancel(KeyHandlerEditSessionDTO dto);

    //_HandlePhraseArrowKey
    HRESULT HandleKeyArrow(KeyHandlerEditSessionDTO dto);

    //_HandlePhraseSelectByNumber
    HRESULT HandleKeySelectByNumber(KeyHandlerEditSessionDTO dto);
};

//degeneration class
class CKeyStateNull : public CKeyStateCategory
{
public:
    CKeyStateNull(_In_ CDIME *pTextService) : CKeyStateCategory(pTextService) {};

protected:
    // _HandleNullInput
    HRESULT HandleKeyInput(KeyHandlerEditSessionDTO dto) { return __super::HandleKeyInput(dto); };

    // HandleKeyNullFinalizeTextStoreAndInput
    HRESULT HandleKeyFinalizeTextStoreAndInput(KeyHandlerEditSessionDTO dto) { return __super::HandleKeyFinalizeTextStoreAndInput(dto); };

    // HandleKeyFinalizeTextStore
    HRESULT HandleKeyFinalizeTextStore(KeyHandlerEditSessionDTO dto) { return __super::HandleKeyFinalizeTextStore(dto); };

    // HandleKeyNullFinalizeCandidatelistAndInput
    HRESULT HandleKeyFinalizeCandidatelistAndInput(KeyHandlerEditSessionDTO dto) { return __super::HandleKeyFinalizeCandidatelistAndInput(dto); };

    // HandleKeyNullFinalizeCandidatelist
    HRESULT HandleKeyFinalizeCandidatelist(KeyHandlerEditSessionDTO dto) { return __super::HandleKeyFinalizeCandidatelist(dto); };

    //_HandleNullConvert
    HRESULT HandleKeyConvert(KeyHandlerEditSessionDTO dto) { return __super::HandleKeyConvert(dto); };

    //_HandleNullCancel
    HRESULT HandleKeyCancel(KeyHandlerEditSessionDTO dto) { return __super::HandleKeyCancel(dto); };

    // HandleKeyNullConvertWild
    HRESULT HandleKeyConvertWildCard(KeyHandlerEditSessionDTO dto) { return __super::HandleKeyConvertWildCard(dto); };

    //_HandleNullBackspace
    HRESULT HandleKeyBackspace(KeyHandlerEditSessionDTO dto) { return __super::HandleKeyBackspace(dto); };

    //_HandleNullArrowKey
    HRESULT HandleKeyArrow(KeyHandlerEditSessionDTO dto) { return __super::HandleKeyArrow(dto); };

    // HandleKeyDoubleSingleByte
    HRESULT HandleKeyDoubleSingleByte(KeyHandlerEditSessionDTO dto) { return __super::HandleKeyDoubleSingleByte(dto); };

    // HandleKeyPunctuation
    HRESULT HandleKeyPunctuation(KeyHandlerEditSessionDTO dto) { return __super::HandleKeyPunctuation(dto); };

    //_HandleNullCandidateSelectByNumber
    HRESULT HandleKeySelectByNumber(KeyHandlerEditSessionDTO dto) { return __super::HandleKeySelectByNumber(dto); };
};