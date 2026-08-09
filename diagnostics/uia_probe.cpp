#include <taskbar_audio_spectrum/platform.h>

#include <uiautomation.h>

#include <iostream>
#include <string>

int wmain() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT initializeResult =
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initializeResult)) {
        std::wcerr << L"COM initialization failed: 0x" << std::hex
                   << initializeResult << L"\n";
        return 1;
    }

    IUIAutomation* automation = nullptr;
    IUIAutomationElement* root = nullptr;
    IUIAutomationCondition* all = nullptr;
    IUIAutomationElementArray* elements = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&automation));
    const HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (SUCCEEDED(hr) && !taskbar) hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    if (SUCCEEDED(hr)) hr = automation->ElementFromHandle(taskbar, &root);
    if (SUCCEEDED(hr)) hr = automation->CreateTrueCondition(&all);
    if (SUCCEEDED(hr)) {
        hr = root->FindAll(TreeScope_Descendants, all, &elements);
    }

    int length = 0;
    if (SUCCEEDED(hr)) hr = elements->get_Length(&length);
    for (int index = 0; SUCCEEDED(hr) && index < length; ++index) {
        IUIAutomationElement* element = nullptr;
        if (FAILED(elements->GetElement(index, &element)) || !element) continue;

        BSTR id = nullptr;
        BSTR name = nullptr;
        CONTROLTYPEID type = 0;
        RECT rect{};
        element->get_CurrentAutomationId(&id);
        element->get_CurrentName(&name);
        element->get_CurrentControlType(&type);
        element->get_CurrentBoundingRectangle(&rect);
        const std::wstring idText = id ? id : L"";
        const std::wstring nameText = name ? name : L"";
        if (idText.find(L"Search") != std::wstring::npos ||
            idText.find(L"search") != std::wstring::npos ||
            nameText.find(L"搜索") != std::wstring::npos ||
            nameText.find(L"Search") != std::wstring::npos) {
            std::wcout << L"id=" << idText << L" name=" << nameText
                       << L" type=" << type << L" rect=" << rect.left << L","
                       << rect.top << L"," << rect.right << L"," << rect.bottom
                       << L"\n";
        }
        SysFreeString(id);
        SysFreeString(name);
        element->Release();
    }

    tas::SafeRelease(elements);
    tas::SafeRelease(all);
    tas::SafeRelease(root);
    tas::SafeRelease(automation);
    CoUninitialize();
    if (FAILED(hr)) {
        std::wcerr << L"UI Automation probe failed: 0x" << std::hex << hr
                   << L"\n";
        return 1;
    }
    return 0;
}
