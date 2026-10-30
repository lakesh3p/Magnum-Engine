#include "platform/FileOps.h"

#include <windows.h>

#include <shellapi.h>
#include <shobjidl.h>

#include <string>

namespace magnum {

bool MoveToRecycleBin(const std::filesystem::path& path) {
    // SHFileOperation wants a double-null terminated list.
    std::wstring from = std::filesystem::absolute(path).wstring();
    from.push_back(L'\0');

    SHFILEOPSTRUCTW op = {};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    return SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted;
}

bool OpenWithDefaultApp(const std::filesystem::path& path) {
    const HINSTANCE result =
        ShellExecuteW(nullptr, L"open", std::filesystem::absolute(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<intptr_t>(result) > 32;
}

bool RevealInExplorer(const std::filesystem::path& path) {
    const std::wstring args = L"/select,\"" + std::filesystem::absolute(path).wstring() + L"\"";
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<intptr_t>(result) > 32;
}

std::filesystem::path PickFolder(const std::wstring& title, const std::filesystem::path& startIn) {
    // Safe to call when COM is already set up by the window system (the result is then RPC_E_CHANGED_MODE).
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::filesystem::path result;

    IFileOpenDialog* dialog = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        dialog->SetTitle(title.c_str());
        if (!startIn.empty()) {
            IShellItem* start = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(std::filesystem::absolute(startIn).c_str(), nullptr, IID_PPV_ARGS(&start)))) {
                dialog->SetFolder(start);
                start->Release();
            }
        }
        if (SUCCEEDED(dialog->Show(GetActiveWindow()))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    result = path;
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dialog->Release();
    }
    if (SUCCEEDED(init)) CoUninitialize();
    return result;
}

bool LaunchSelf(const std::wstring& arguments) {
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", exe, arguments.c_str(), std::filesystem::path(exe).parent_path().c_str(), SW_SHOWNORMAL);
    return reinterpret_cast<intptr_t>(result) > 32;
}

} // namespace magnum
