// bassmax_page.h  —  DirectShow 属性页
#pragma once
#include "bassmax_filter.h"

class CBassMaxPage : public CBasePropertyPage {
public:
    static CUnknown* WINAPI CreateInstance(LPUNKNOWN pUnk, HRESULT* phr);
    CBassMaxPage(LPUNKNOWN pUnk, HRESULT* phr);

    HRESULT OnActivate() override;
    HRESULT OnApplyChanges() override;
    INT_PTR OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
};
