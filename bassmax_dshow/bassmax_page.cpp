// bassmax_page.cpp  —  DirectShow 属性页 (播放器里右键滤镜 -> 属性)
#include "bassmax_page.h"
#include "bassmax_ui.h"
#include "resource.h"

CBassMaxPage::CBassMaxPage(LPUNKNOWN pUnk, HRESULT* phr)
    : CBasePropertyPage("BassMax property page", pUnk, IDD_BASSMAX, IDS_TITLE) {
    if (phr) *phr = S_OK;
}

CUnknown* WINAPI CBassMaxPage::CreateInstance(LPUNKNOWN pUnk, HRESULT* phr) {
    CBassMaxPage* p = new CBassMaxPage(pUnk, phr);
    if (!p && phr) *phr = E_OUTOFMEMORY;
    return p;
}

HRESULT CBassMaxPage::OnActivate() {
    if (m_Dlg) bassmax::UiLoadFromSettings(m_Dlg);
    return S_OK;
}

HRESULT CBassMaxPage::OnApplyChanges() {
    if (m_Dlg) {
        bassmax::SetSettings(bassmax::UiReadSettings(m_Dlg));
        m_bDirty = FALSE;
    }
    return S_OK;
}

INT_PTR CBassMaxPage::OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_HSCROLL:
            bassmax::UiUpdateLabels(hwnd);
            m_bDirty = TRUE;
            if (m_pPageSite) m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
            return TRUE;
        case WM_COMMAND:
            if (bassmax::UiPresetClicked(hwnd, LOWORD(wParam)) >= 0) {
                bassmax::UiUpdateLabels(hwnd);
                m_bDirty = TRUE;
                if (m_pPageSite) m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
                return TRUE;
            }
            break;
    }
    return FALSE;
}
