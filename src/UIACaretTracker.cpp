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
#include "UIACaretTracker.h"

CUIACaretTracker::CUIACaretTracker()
	: _pAutomation(nullptr)
{
}

CUIACaretTracker::~CUIACaretTracker()
{
	if (_pAutomation)
	{
		_pAutomation->Release();
		_pAutomation = nullptr;
	}
}

HRESULT CUIACaretTracker::Initialize()
{
	if (_pAutomation)
		return S_OK;

	HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, nullptr,
		CLSCTX_INPROC_SERVER, IID_IUIAutomation, (void**)&_pAutomation);
	debugPrint(L"CUIACaretTracker::Initialize() hr=0x%08X", hr);
	return hr;
}

HRESULT CUIACaretTracker::GetCaretRect(_Out_ RECT* pRect)
{
	if (pRect == nullptr)
		return E_INVALIDARG;

	*pRect = { 0, 0, 0, 0 };

	if (_pAutomation == nullptr)
		return E_FAIL;

	HRESULT hr = E_FAIL;

	IUIAutomationElement* pFocused = nullptr;
	hr = _pAutomation->GetFocusedElement(&pFocused);
	debugPrint(L"UIA GetFocusedElement hr=0x%08X pFocused=%p", hr, pFocused);
	if (FAILED(hr) || pFocused == nullptr)
		return E_FAIL;

	IUIAutomationTextPattern* pTextPattern = nullptr;
	hr = pFocused->GetCurrentPatternAs(UIA_TextPatternId,
		IID_IUIAutomationTextPattern, (void**)&pTextPattern);
	debugPrint(L"UIA GetCurrentPatternAs(TextPattern) hr=0x%08X pTextPattern=%p", hr, pTextPattern);

	if (SUCCEEDED(hr) && pTextPattern)
	{
		IUIAutomationTextRangeArray* pSelection = nullptr;
		hr = pTextPattern->GetSelection(&pSelection);
		debugPrint(L"UIA GetSelection hr=0x%08X pSelection=%p", hr, pSelection);
		if (SUCCEEDED(hr) && pSelection)
		{
			int length = 0;
			pSelection->get_Length(&length);
			debugPrint(L"UIA selection length=%d", length);
			if (length > 0)
			{
				IUIAutomationTextRange* pRange = nullptr;
				if (SUCCEEDED(pSelection->GetElement(0, &pRange)) && pRange)
				{
					// First try collapsed selection — has correct x (caret position)
					SAFEARRAY* pRects = nullptr;
					hr = pRange->GetBoundingRectangles(&pRects);
					ULONG cElements = (pRects && pRects->rgsabound) ? pRects->rgsabound[0].cElements : 0;
					debugPrint(L"UIA GetBoundingRectangles hr=0x%08X cElements=%d", hr, cElements);

					// Collapsed selection (caret) returns empty rects in WPF.
					// Expand to enclosing character — gives the rect of the char
					// at the caret position, with correct x and y.
					if (SUCCEEDED(hr) && cElements == 0)
					{
						if (pRects) { SafeArrayDestroy(pRects); pRects = nullptr; }
						IUIAutomationTextRange* pCharRange = nullptr;
						if (SUCCEEDED(pRange->Clone(&pCharRange)) && pCharRange)
						{
							pCharRange->ExpandToEnclosingUnit(TextUnit_Character);
							hr = pCharRange->GetBoundingRectangles(&pRects);
							cElements = (pRects && pRects->rgsabound) ? pRects->rgsabound[0].cElements : 0;
							debugPrint(L"UIA ExpandToChar GetBoundingRectangles hr=0x%08X cElements=%d", hr, cElements);
							pCharRange->Release();
						}
					}

					// If character also empty, try line as last resort (correct y, x=line start)
					if (SUCCEEDED(hr) && cElements == 0)
					{
						if (pRects) { SafeArrayDestroy(pRects); pRects = nullptr; }
						IUIAutomationTextRange* pLineRange = nullptr;
						if (SUCCEEDED(pRange->Clone(&pLineRange)) && pLineRange)
						{
							pLineRange->ExpandToEnclosingUnit(TextUnit_Line);
							hr = pLineRange->GetBoundingRectangles(&pRects);
							cElements = (pRects && pRects->rgsabound) ? pRects->rgsabound[0].cElements : 0;
							debugPrint(L"UIA ExpandToLine GetBoundingRectangles hr=0x%08X cElements=%d", hr, cElements);
							pLineRange->Release();
						}
					}

					if (SUCCEEDED(hr) && pRects && cElements >= 4)
					{
						double* data = nullptr;
						if (SUCCEEDED(SafeArrayAccessData(pRects, (void**)&data)))
						{
							pRect->left   = (LONG)data[0];
							pRect->top    = (LONG)data[1];
							pRect->right  = (LONG)(data[0] + data[2]);
							pRect->bottom = (LONG)(data[1] + data[3]);
							SafeArrayUnaccessData(pRects);
							debugPrint(L"UIA raw rect=(%d,%d,%d,%d)",
								pRect->left, pRect->top, pRect->right, pRect->bottom);

							// Check if rect is client-relative (buggy WPF UIA providers).
							// Compare against the focused element's screen bounding rect.
							RECT rcElement = {0};
							if (SUCCEEDED(pFocused->get_CurrentBoundingRectangle(&rcElement)))
							{
								debugPrint(L"UIA element bounding=(%d,%d,%d,%d)",
									rcElement.left, rcElement.top, rcElement.right, rcElement.bottom);
								LONG elemW = rcElement.right - rcElement.left;
								LONG elemH = rcElement.bottom - rcElement.top;
								if (pRect->right <= elemW && pRect->bottom <= elemH
									&& pRect->left < rcElement.left)
								{
									debugPrint(L"UIA offsetting by element origin (%d,%d)",
										rcElement.left, rcElement.top);
									pRect->left   += rcElement.left;
									pRect->top    += rcElement.top;
									pRect->right  += rcElement.left;
									pRect->bottom += rcElement.top;
								}
							}

							debugPrint(L"CUIACaretTracker::GetCaretRect() final rect=(%d,%d,%d,%d)",
								pRect->left, pRect->top, pRect->right, pRect->bottom);
							hr = S_OK;
						}
					}
					else
					{
						hr = E_FAIL;
					}
					if (pRects) SafeArrayDestroy(pRects);
					pRange->Release();
				}
			}
			else
			{
				hr = E_FAIL;
			}
			pSelection->Release();
		}
		pTextPattern->Release();
	}
	else
	{
		hr = E_FAIL;
	}

	pFocused->Release();
	return hr;
}
