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


#include "KeyStateCategory.h"

CKeyStateCategoryFactory* CKeyStateCategoryFactory::_instance;

CKeyStateCategoryFactory::CKeyStateCategoryFactory()
{
    _instance = nullptr;
}

CKeyStateCategoryFactory* CKeyStateCategoryFactory::Instance()
{
    if (nullptr == _instance)
    {
        _instance = new (std::nothrow) CKeyStateCategoryFactory();
    }

    return _instance;
}

CKeyStateCategory* CKeyStateCategoryFactory::MakeKeyStateCategory(KEYSTROKE_CATEGORY keyCategory, _In_ CDIME *pTextService)
{
    CKeyStateCategory* pKeyState = nullptr;

    switch (keyCategory)
    {
    case KEYSTROKE_CATEGORY::CATEGORY_NONE:
        pKeyState = new (std::nothrow) CKeyStateNull(pTextService);
        break;

    case KEYSTROKE_CATEGORY::CATEGORY_COMPOSING:
        pKeyState = new (std::nothrow) CKeyStateComposing(pTextService);
        break;

    case KEYSTROKE_CATEGORY::CATEGORY_CANDIDATE:
        pKeyState = new (std::nothrow) CKeyStateCandidate(pTextService);
        break;

    case KEYSTROKE_CATEGORY::CATEGORY_PHRASE:
        pKeyState = new (std::nothrow) CKeyStatePhrase(pTextService);
        break;

    default:
        pKeyState = new (std::nothrow) CKeyStateNull(pTextService);
        break;
    }
    return pKeyState;
}

void CKeyStateCategoryFactory::Release()
{
    if (_instance)
    {
        delete _instance;
        _instance = nullptr;
    }
}

/*
class CKeyStateCategory
*/
CKeyStateCategory::CKeyStateCategory(_In_ CDIME *pTextService)
{
    _pTextService = pTextService;
}

CKeyStateCategory::~CKeyStateCategory(void)
{
}

HRESULT CKeyStateCategory::KeyStateHandler(KEYSTROKE_FUNCTION function, KeyHandlerEditSessionDTO dto)
{
    switch(function)
    {
    case KEYSTROKE_FUNCTION::FUNCTION_INPUT:
        return HandleKeyInput(dto);
    case KEYSTROKE_FUNCTION::FUNCTION_INPUT_AND_CONVERT:
        return HandleKeyInputAndConvert(dto);
    case KEYSTROKE_FUNCTION::FUNCTION_INPUT_AND_CONVERT_WILDCARD:
        return HandleKeyInputAndConvertWildCard(dto);

    case KEYSTROKE_FUNCTION::FUNCTION_FINALIZE_TEXTSTORE_AND_INPUT:
        return HandleKeyFinalizeTextStoreAndInput(dto);
    case KEYSTROKE_FUNCTION::FUNCTION_FINALIZE_TEXTSTORE:
        return HandleKeyFinalizeTextStore(dto);

    case KEYSTROKE_FUNCTION::FUNCTION_FINALIZE_CANDIDATELIST_AND_INPUT:
        return HandleKeyFinalizeCandidatelistAndInput(dto);
    case KEYSTROKE_FUNCTION::FUNCTION_FINALIZE_CANDIDATELIST:
        return HandleKeyFinalizeCandidatelist(dto);

    case KEYSTROKE_FUNCTION::FUNCTION_CONVERT:
        return HandleKeyConvert(dto);
    case KEYSTROKE_FUNCTION::FUNCTION_CONVERT_WILDCARD:
        return HandleKeyConvertWildCard(dto);

    case KEYSTROKE_FUNCTION::FUNCTION_CONVERT_ARRAY_PHRASE:
        return HandleKeyConvertArrayPhrase(dto);
    case KEYSTROKE_FUNCTION::FUNCTION_CONVERT_ARRAY_PHRASE_WILDCARD:
        return HandleKeyConvertArrayPhraseWildCard(dto);

    case KEYSTROKE_FUNCTION::FUNCTION_CANCEL:
        return HandleKeyCancel(dto);

    case KEYSTROKE_FUNCTION::FUNCTION_BACKSPACE:
        return HandleKeyBackspace(dto);

    case KEYSTROKE_FUNCTION::FUNCTION_MOVE_LEFT:
    case KEYSTROKE_FUNCTION::FUNCTION_MOVE_RIGHT:
	case KEYSTROKE_FUNCTION::FUNCTION_MOVE_UP:
    case KEYSTROKE_FUNCTION::FUNCTION_MOVE_DOWN:
        return HandleKeyArrow(dto);
 
    case KEYSTROKE_FUNCTION::FUNCTION_MOVE_PAGE_UP:
    case KEYSTROKE_FUNCTION::FUNCTION_MOVE_PAGE_DOWN:
    case KEYSTROKE_FUNCTION::FUNCTION_MOVE_PAGE_TOP:
    case KEYSTROKE_FUNCTION::FUNCTION_MOVE_PAGE_BOTTOM:
        return HandleKeyArrow(dto);

    case KEYSTROKE_FUNCTION::FUNCTION_DOUBLE_SINGLE_BYTE:
        return HandleKeyDoubleSingleByte(dto);

    case KEYSTROKE_FUNCTION::FUNCTION_ADDRESS_DIRECT_INPUT:
        return HandleKeyAddressChar(dto);

    case KEYSTROKE_FUNCTION::FUNCTION_SELECT_BY_NUMBER:
        return HandleKeySelectByNumber(dto);

    }
    return E_INVALIDARG;
}

void CKeyStateCategory::Release()
{
    delete this;
}

// _HandleCompositionInput
HRESULT CKeyStateCategory::HandleKeyInput(KeyHandlerEditSessionDTO dto)
{
	dto;
    return E_NOTIMPL;
}

// _HandleCompositionInputAndConvert
HRESULT CKeyStateCategory::HandleKeyInputAndConvert(KeyHandlerEditSessionDTO dto)
{
    dto;
    return E_NOTIMPL;
}

// _HandleCompositionInputAndConvertWildCard
HRESULT CKeyStateCategory::HandleKeyInputAndConvertWildCard(KeyHandlerEditSessionDTO dto)
{
    dto;
    return E_NOTIMPL;
}


// HandleKeyFinalizeTextStore
HRESULT CKeyStateCategory::HandleKeyFinalizeTextStore(KeyHandlerEditSessionDTO dto)
{
	dto;
    return E_NOTIMPL;
}
// HandleKeyCompositionFinalizeTextStoreAndInput
HRESULT CKeyStateCategory::HandleKeyFinalizeTextStoreAndInput(KeyHandlerEditSessionDTO dto)
{
	dto;
    return E_NOTIMPL;
}

// HandleKeyCompositionFinalizeCandidatelistAndInput
HRESULT CKeyStateCategory::HandleKeyFinalizeCandidatelistAndInput(KeyHandlerEditSessionDTO dto)
{
	dto;
    return E_NOTIMPL;
}

// HandleKeyCompositionFinalizeCandidatelist
HRESULT CKeyStateCategory::HandleKeyFinalizeCandidatelist(KeyHandlerEditSessionDTO dto)
{
	dto;
    return E_NOTIMPL;
}

// HandleKeyConvert
HRESULT CKeyStateCategory::HandleKeyConvert(KeyHandlerEditSessionDTO dto)
{
	dto;
    return E_NOTIMPL;
}

// HandleKeyConvertWildCard
HRESULT CKeyStateCategory::HandleKeyConvertWildCard(KeyHandlerEditSessionDTO dto)
{
	dto;
    return E_NOTIMPL;
}

// HandleKeyConvertArrayPhrase
HRESULT CKeyStateCategory::HandleKeyConvertArrayPhrase(KeyHandlerEditSessionDTO dto)
{
    dto;
    return E_NOTIMPL;
}

// HandleKeyConvertArrayPhraseWildCard
HRESULT CKeyStateCategory::HandleKeyConvertArrayPhraseWildCard(KeyHandlerEditSessionDTO dto)
{
    dto;
    return E_NOTIMPL;
}

//_HandleCancel
HRESULT CKeyStateCategory::HandleKeyCancel(KeyHandlerEditSessionDTO dto)
{
	dto;
    return E_NOTIMPL;
}

//_HandleCompositionBackspace
HRESULT CKeyStateCategory::HandleKeyBackspace(KeyHandlerEditSessionDTO dto)
{
	dto;
    return E_NOTIMPL;
}

//_HandleCompositionArrowKey
HRESULT CKeyStateCategory::HandleKeyArrow(KeyHandlerEditSessionDTO dto)
{
	dto;
    return E_NOTIMPL;
}

//_HandleCompositionDoubleSingleByte
HRESULT CKeyStateCategory::HandleKeyDoubleSingleByte(KeyHandlerEditSessionDTO dto)
{
	dto;
    return E_NOTIMPL;
}

//_HandleCompositionPunctuation
HRESULT CKeyStateCategory::HandleKeyPunctuation(KeyHandlerEditSessionDTO dto)
{
	dto;
    return E_NOTIMPL;
}

HRESULT CKeyStateCategory::HandleKeySelectByNumber(KeyHandlerEditSessionDTO dto)
{
	dto;
    return E_NOTIMPL;
}

HRESULT CKeyStateCategory::HandleKeyAddressChar(KeyHandlerEditSessionDTO dto)
{
	dto;
    return E_NOTIMPL;
}



/*
class CKeyStateComposing
*/
CKeyStateComposing::CKeyStateComposing(_In_ CDIME *pTextService) : CKeyStateCategory(pTextService)
{
}

HRESULT CKeyStateComposing::HandleKeyInput(KeyHandlerEditSessionDTO dto)
{
	if(_pTextService == nullptr) return E_FAIL;
    return _pTextService->_HandleCompositionInput(dto.ec, dto.pContext, dto.wch);
}

HRESULT CKeyStateComposing::HandleKeyInputAndConvert(KeyHandlerEditSessionDTO dto)
{
    if (_pTextService == nullptr) return E_FAIL;
    _pTextService->_HandleCompositionInput(dto.ec, dto.pContext, dto.wch);
    return _pTextService->_HandleCompositionConvert(dto.ec, dto.pContext, FALSE, FALSE);
}

HRESULT CKeyStateComposing::HandleKeyInputAndConvertWildCard(KeyHandlerEditSessionDTO dto)
{
    if (_pTextService == nullptr) return E_FAIL;
    _pTextService->_HandleCompositionInput(dto.ec, dto.pContext, dto.wch);
    return _pTextService->_HandleCompositionConvert(dto.ec, dto.pContext, TRUE, FALSE);
}

HRESULT CKeyStateComposing::HandleKeyFinalizeTextStoreAndInput(KeyHandlerEditSessionDTO dto)
{
    if(_pTextService == nullptr) return E_FAIL;
	_pTextService->_HandleCompositionFinalize(dto.ec, dto.pContext, FALSE);
    return _pTextService->_HandleCompositionInput(dto.ec, dto.pContext, dto.wch);
}

HRESULT CKeyStateComposing::HandleKeyFinalizeTextStore(KeyHandlerEditSessionDTO dto)
{
	if(_pTextService == nullptr) return E_FAIL;
    return _pTextService->_HandleCompositionFinalize(dto.ec, dto.pContext, FALSE);
}

HRESULT CKeyStateComposing::HandleKeyFinalizeCandidatelistAndInput(KeyHandlerEditSessionDTO dto)
{
	if(_pTextService == nullptr) return E_FAIL;
    _pTextService->_HandleCompositionFinalize(dto.ec, dto.pContext, TRUE);
    return _pTextService->_HandleCompositionInput(dto.ec, dto.pContext, dto.wch);
}

HRESULT CKeyStateComposing::HandleKeyFinalizeCandidatelist(KeyHandlerEditSessionDTO dto)
{
	if(_pTextService == nullptr) return E_FAIL;
    return _pTextService->_HandleCompositionFinalize(dto.ec, dto.pContext, TRUE);
}

HRESULT CKeyStateComposing::HandleKeyConvert(KeyHandlerEditSessionDTO dto)
{
	if(_pTextService == nullptr) return E_FAIL;
    return _pTextService->_HandleCompositionConvert(dto.ec, dto.pContext, FALSE, FALSE);
}

HRESULT CKeyStateComposing::HandleKeyConvertWildCard(KeyHandlerEditSessionDTO dto)
{
	if(_pTextService == nullptr) return E_FAIL;
    return _pTextService->_HandleCompositionConvert(dto.ec, dto.pContext, TRUE, FALSE);
}

HRESULT CKeyStateComposing::HandleKeyConvertArrayPhrase(KeyHandlerEditSessionDTO dto)
{
    if (_pTextService == nullptr) return E_FAIL;
    return _pTextService->_HandleCompositionConvert(dto.ec, dto.pContext, FALSE, TRUE);
}

HRESULT CKeyStateComposing::HandleKeyConvertArrayPhraseWildCard(KeyHandlerEditSessionDTO dto)
{
    if (_pTextService == nullptr) return E_FAIL;
    return _pTextService->_HandleCompositionConvert(dto.ec, dto.pContext, TRUE, TRUE);
}


HRESULT CKeyStateComposing::HandleKeyCancel(KeyHandlerEditSessionDTO dto)
{
	if(_pTextService == nullptr) return E_FAIL;
    return _pTextService->_HandleCancel(dto.ec, dto.pContext);
}

HRESULT CKeyStateComposing::HandleKeyBackspace(KeyHandlerEditSessionDTO dto)
{
	if(_pTextService == nullptr) return E_FAIL;
    return _pTextService->_HandleCompositionBackspace(dto.ec, dto.pContext);
}

HRESULT CKeyStateComposing::HandleKeyArrow(KeyHandlerEditSessionDTO dto)
{
	if(_pTextService == nullptr) return E_FAIL;
    return _pTextService->_HandleCompositionArrowKey(dto.ec, dto.pContext, dto.arrowKey);
}

HRESULT CKeyStateComposing::HandleKeyDoubleSingleByte(KeyHandlerEditSessionDTO dto)
{
	if(_pTextService == nullptr) return E_FAIL;
    return _pTextService->_HandleCompositionDoubleSingleByte(dto.ec, dto.pContext, dto.wch);
}

HRESULT CKeyStateComposing::HandleKeyAddressChar(KeyHandlerEditSessionDTO dto)
{
	if(_pTextService == nullptr) return E_FAIL;
	return _pTextService->_HandleCompositionAddressChar(dto.ec, dto.pContext, dto.wch);
}


/*
class CKeyStateCandidate
*/
CKeyStateCandidate::CKeyStateCandidate(_In_ CDIME *pTextService) : CKeyStateCategory(pTextService)
{
}

// _HandleCandidateInput
HRESULT CKeyStateCandidate::HandleKeyFinalizeCandidatelist(KeyHandlerEditSessionDTO dto)
{
	if(_pTextService == nullptr) return E_FAIL;
    return _pTextService->_HandleCandidateFinalize(dto.ec, dto.pContext);
}

// HandleKeyFinalizeCandidatelistAndInput
HRESULT CKeyStateCandidate::HandleKeyFinalizeCandidatelistAndInput(KeyHandlerEditSessionDTO dto)
{
	if(_pTextService == nullptr) return E_FAIL;
    _pTextService->_HandleCandidateFinalize(dto.ec, dto.pContext);
    return _pTextService->_HandleCompositionInput(dto.ec, dto.pContext, dto.wch);
}

//_HandleCandidateConvert
HRESULT CKeyStateCandidate::HandleKeyConvert(KeyHandlerEditSessionDTO dto)
{
	if(_pTextService == nullptr) return E_FAIL;
    return _pTextService->_HandleCandidateConvert(dto.ec, dto.pContext);
}

//_HandleCancel
HRESULT CKeyStateCandidate::HandleKeyCancel(KeyHandlerEditSessionDTO dto)    
{
	if(_pTextService == nullptr) return E_FAIL;
    return _pTextService->_HandleCancel(dto.ec, dto.pContext);
}

//_HandleCandidateArrowKey
HRESULT CKeyStateCandidate::HandleKeyArrow(KeyHandlerEditSessionDTO dto)
{
	if(_pTextService == nullptr) return E_FAIL;
    return _pTextService->_HandleCandidateArrowKey(dto.ec, dto.pContext, dto.arrowKey);
}

//_HandleCandidateSelectByNumber
HRESULT CKeyStateCandidate::HandleKeySelectByNumber(KeyHandlerEditSessionDTO dto)
{
	if(_pTextService == nullptr) return E_FAIL;
    return _pTextService->_HandleCandidateSelectByNumber(dto.ec, dto.pContext, dto.code, dto.wch);
}

/*
class CKeyStatePhrase
*/

CKeyStatePhrase::CKeyStatePhrase(_In_ CDIME *pTextService) : CKeyStateCategory(pTextService)
{
}

//HandleKeyFinalizeCandidatelist
HRESULT CKeyStatePhrase::HandleKeyFinalizeCandidatelist(KeyHandlerEditSessionDTO dto)
{
	if(_pTextService == nullptr) return E_FAIL;
    return _pTextService->_HandlePhraseFinalize(dto.ec, dto.pContext);
}

//HandleKeyCancel
HRESULT CKeyStatePhrase::HandleKeyCancel(KeyHandlerEditSessionDTO dto)
{
	if(_pTextService == nullptr) return E_FAIL;
    return _pTextService->_HandleCancel(dto.ec, dto.pContext);
}

//HandleKeyArrow
HRESULT CKeyStatePhrase::HandleKeyArrow(KeyHandlerEditSessionDTO dto)
{
	if(_pTextService == nullptr) return E_FAIL;
    return _pTextService->_HandlePhraseArrowKey(dto.ec, dto.pContext, dto.arrowKey);
}

//HandleKeySelectByNumber
HRESULT CKeyStatePhrase::HandleKeySelectByNumber(KeyHandlerEditSessionDTO dto)
{
	if(_pTextService == nullptr) return E_FAIL;
    return _pTextService->_HandlePhraseSelectByNumber(dto.ec, dto.pContext, dto.code, dto.wch);
}