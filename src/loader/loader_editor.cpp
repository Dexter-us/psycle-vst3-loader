#include "loader_editor.h"

#include "loader_controller.h"

#include "pluginterfaces/gui/iplugview.h"

#include <algorithm>
#include <cstring>

#if defined(_WIN32)
#include <commdlg.h>
#include <string_view>
#include <vector>
#endif

namespace psycle::loader {

using namespace Steinberg;

namespace {

constexpr int kEditorWidth = 560;
constexpr int kEditorHeight = 184;

#if defined(_WIN32)
constexpr int kBrowseButtonId = 1001;
constexpr UINT kUiUpdateMessage = WM_APP + 0x510;

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }

    const int length = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.c_str(),
        static_cast<int>(text.size()),
        nullptr,
        0
    );
    if (length <= 0) {
        return {};
    }

    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.c_str(),
        static_cast<int>(text.size()),
        result.data(),
        length
    );
    return result;
}

std::string wideToUtf8(const wchar_t* text) {
    if (!text || !*text) {
        return {};
    }

    const int length = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        text,
        -1,
        nullptr,
        0,
        nullptr,
        nullptr
    );
    if (length <= 1) {
        return {};
    }

    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        text,
        -1,
        result.data(),
        length,
        nullptr,
        nullptr
    );
    result.pop_back();
    return result;
}

void applyDefaultFont(HWND control) {
    if (control) {
        SendMessageW(
            control,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),
            TRUE
        );
    }
}
#endif

} // namespace

LoaderEditorView::LoaderEditorView(LoaderController& controller)
    : CPluginView([] {
          static ViewRect rect(0, 0, kEditorWidth, kEditorHeight);
          return &rect;
      }()),
      controller_(controller) {}

LoaderEditorView::~LoaderEditorView() {
    controller_.unregisterEditor(this);
#if defined(_WIN32)
    destroyControls();
#endif
}

tresult PLUGIN_API LoaderEditorView::isPlatformTypeSupported(FIDString type) {
#if defined(_WIN32)
    return FIDStringsEqual(type, kPlatformTypeHWND) ? kResultTrue : kResultFalse;
#else
    (void)type;
    return kResultFalse;
#endif
}

tresult PLUGIN_API LoaderEditorView::attached(void* parent, FIDString type) {
    if (isPlatformTypeSupported(type) != kResultTrue || !parent) {
        return kResultFalse;
    }

    const auto result = CPluginView::attached(parent, type);
    if (result != kResultOk) {
        return result;
    }

#if defined(_WIN32)
    container_ = CreateWindowExW(
        0,
        L"STATIC",
        L"",
        WS_CHILD | WS_VISIBLE | SS_NOTIFY,
        0,
        0,
        kEditorWidth,
        kEditorHeight,
        static_cast<HWND>(parent),
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr
    );
    if (!container_) {
        CPluginView::removed();
        return kResultFalse;
    }

    SetWindowLongPtrW(
        container_,
        GWLP_USERDATA,
        reinterpret_cast<LONG_PTR>(this)
    );
    originalWindowProc_ = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        container_,
        GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(&LoaderEditorView::windowProc)
    ));
    createControls();
    controller_.registerEditor(this);
#endif
    return kResultOk;
}

tresult PLUGIN_API LoaderEditorView::removed() {
    controller_.unregisterEditor(this);
#if defined(_WIN32)
    destroyControls();
#endif
    return CPluginView::removed();
}

tresult PLUGIN_API LoaderEditorView::onSize(ViewRect* newSize) {
    const auto result = CPluginView::onSize(newSize);
#if defined(_WIN32)
    if (newSize && container_) {
        const int width = std::max(1, newSize->getWidth());
        const int height = std::max(1, newSize->getHeight());
        MoveWindow(container_, 0, 0, width, height, TRUE);
        layoutControls(width, height);
    }
#endif
    return result;
}

void LoaderEditorView::updateMachinePath(const std::string& path) {
#if defined(_WIN32)
    if (!pathEdit_ || !container_) {
        return;
    }
    const auto widePath = utf8ToWide(path);
    const std::wstring text =
        widePath.empty() ? L"No machine selected" : widePath;
    {
        std::lock_guard<std::mutex> lock(pendingUpdateMutex_);
        pendingPath_ = text;
        pathUpdatePending_ = true;
    }
    PostMessageW(container_, kUiUpdateMessage, 0, 0);
#else
    (void)path;
#endif
}

void LoaderEditorView::updateStatus(
    const std::string& status,
    bool success
) {
#if defined(_WIN32)
    if (!status_ || !container_) {
        return;
    }
    const auto wideStatus = utf8ToWide(status);
    const std::wstring message =
        (success ? L"Loaded: " : L"Could not load: ") + wideStatus;
    {
        std::lock_guard<std::mutex> lock(pendingUpdateMutex_);
        pendingStatus_ = message;
        statusUpdatePending_ = true;
    }
    PostMessageW(container_, kUiUpdateMessage, 0, 0);
#else
    (void)status;
    (void)success;
#endif
}

#if defined(_WIN32)
LRESULT CALLBACK LoaderEditorView::windowProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    auto* self = reinterpret_cast<LoaderEditorView*>(
        GetWindowLongPtrW(window, GWLP_USERDATA)
    );

    if (self && message == kUiUpdateMessage) {
        std::wstring path;
        std::wstring status;
        bool updatePath = false;
        bool updateStatus = false;
        {
            std::lock_guard<std::mutex> lock(self->pendingUpdateMutex_);
            path = self->pendingPath_;
            status = self->pendingStatus_;
            updatePath = self->pathUpdatePending_;
            updateStatus = self->statusUpdatePending_;
            self->pathUpdatePending_ = false;
            self->statusUpdatePending_ = false;
        }
        if (updatePath && self->pathEdit_) {
            SetWindowTextW(self->pathEdit_, path.c_str());
        }
        if (updateStatus && self->status_) {
            SetWindowTextW(self->status_, status.c_str());
        }
        return 0;
    }

    if (self && message == WM_COMMAND &&
        LOWORD(wParam) == kBrowseButtonId &&
        HIWORD(wParam) == BN_CLICKED) {
        self->chooseMachine();
        return 0;
    }

    if (self && self->originalWindowProc_) {
        return CallWindowProcW(
            self->originalWindowProc_,
            window,
            message,
            wParam,
            lParam
        );
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void LoaderEditorView::createControls() {
    title_ = CreateWindowExW(
        0,
        L"STATIC",
        L"Dexter U.S.  |  Psycle Machine Loader",
        WS_CHILD | WS_VISIBLE,
        20,
        16,
        520,
        22,
        container_,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr
    );
    description_ = CreateWindowExW(
        0,
        L"STATIC",
        L"Choose a Psycle 1.12 x64 native machine DLL.",
        WS_CHILD | WS_VISIBLE,
        20,
        44,
        520,
        20,
        container_,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr
    );
    pathEdit_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_READONLY,
        20,
        76,
        416,
        26,
        container_,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr
    );
    browseButton_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Browse...",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        448,
        76,
        92,
        26,
        container_,
        reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(kBrowseButtonId)
        ),
        GetModuleHandleW(nullptr),
        nullptr
    );
    status_ = CreateWindowExW(
        0,
        L"STATIC",
        L"The synth/effect loader validates the selected machine type.",
        WS_CHILD | WS_VISIBLE,
        20,
        118,
        520,
        36,
        container_,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr
    );

    applyDefaultFont(title_);
    applyDefaultFont(description_);
    applyDefaultFont(pathEdit_);
    applyDefaultFont(browseButton_);
    applyDefaultFont(status_);

    const auto currentMachinePath = controller_.machinePath();
    updateMachinePath(currentMachinePath);
    SetWindowTextW(
        status_,
        currentMachinePath.empty()
            ? L"Choose a machine to load."
            : L"Restored selection; waiting for host activation."
    );
}

void LoaderEditorView::chooseMachine() {
    std::vector<wchar_t> fileName(32768, L'\0');
    const auto currentPath = utf8ToWide(controller_.machinePath());
    if (!currentPath.empty() && currentPath.size() < fileName.size()) {
        std::copy(currentPath.begin(), currentPath.end(), fileName.begin());
    }

    OPENFILENAMEW dialog {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = container_;
    dialog.lpstrFilter =
        L"Psycle machine DLLs (*.dll)\0*.dll\0All files (*.*)\0*.*\0\0";
    dialog.lpstrFile = fileName.data();
    dialog.nMaxFile = static_cast<DWORD>(fileName.size());
    dialog.lpstrTitle = L"Choose a Psycle 1.12 x64 machine";
    dialog.Flags =
        OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER |
        OFN_HIDEREADONLY;

    if (!GetOpenFileNameW(&dialog)) {
        return;
    }

    const auto selectedPath = wideToUtf8(fileName.data());
    if (selectedPath.empty()) {
        return;
    }

    SetWindowTextW(pathEdit_, fileName.data());
    SetWindowTextW(status_, L"Machine selected. Loading and validating...");
    controller_.selectMachinePath(selectedPath);
}

void LoaderEditorView::layoutControls(int width, int) {
    const int contentWidth = std::max(220, width - 40);
    const int buttonWidth = 92;
    const int gap = 12;
    const int editWidth = std::max(100, contentWidth - buttonWidth - gap);
    MoveWindow(pathEdit_, 20, 76, editWidth, 26, TRUE);
    MoveWindow(
        browseButton_,
        20 + editWidth + gap,
        76,
        buttonWidth,
        26,
        TRUE
    );
    MoveWindow(title_, 20, 16, contentWidth, 22, TRUE);
    MoveWindow(description_, 20, 44, contentWidth, 20, TRUE);
    MoveWindow(status_, 20, 118, contentWidth, 36, TRUE);
}

void LoaderEditorView::destroyControls() {
    if (!container_) {
        return;
    }

    if (originalWindowProc_) {
        SetWindowLongPtrW(
            container_,
            GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(originalWindowProc_)
        );
        originalWindowProc_ = nullptr;
    }
    SetWindowLongPtrW(container_, GWLP_USERDATA, 0);
    DestroyWindow(container_);
    container_ = nullptr;
    title_ = nullptr;
    description_ = nullptr;
    pathEdit_ = nullptr;
    browseButton_ = nullptr;
    status_ = nullptr;
}
#endif

} // namespace psycle::loader