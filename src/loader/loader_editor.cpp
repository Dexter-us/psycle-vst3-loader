#include "loader_editor.h"

#include "loader_controller.h"

#include "pluginterfaces/gui/iplugview.h"

#include <algorithm>
#include <cstring>
#include <cwchar>

#if defined(_WIN32)
#include <commdlg.h>
#include <string_view>
#include <vector>
#endif

namespace psycle::loader {

using namespace Steinberg;

namespace {

constexpr int kEditorWidth = 560;
constexpr int kEditorHeight = 420;

#if defined(_WIN32)
constexpr int kBrowseButtonId = 1001;
constexpr int kParameterControlBase = 2000;
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
        WS_CHILD | WS_VISIBLE | SS_NOTIFY | WS_VSCROLL,
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

void LoaderEditorView::updateParameters(
    const std::vector<EditorParameter>& parameters
) {
#if defined(_WIN32)
    if (!container_) return;
    {
        std::lock_guard<std::mutex> lock(pendingUpdateMutex_);
        pendingParameters_ = parameters;
        parametersUpdatePending_ = true;
    }
    PostMessageW(container_, kUiUpdateMessage, 0, 0);
#else
    (void)parameters;
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
        bool updateParameters = false;
        std::vector<EditorParameter> parameters;
        {
            std::lock_guard<std::mutex> lock(self->pendingUpdateMutex_);
            path = self->pendingPath_;
            status = self->pendingStatus_;
            updatePath = self->pathUpdatePending_;
            updateStatus = self->statusUpdatePending_;
            updateParameters = self->parametersUpdatePending_;
            if (updateParameters) {
                parameters = std::move(self->pendingParameters_);
            }
            self->pathUpdatePending_ = false;
            self->statusUpdatePending_ = false;
            self->parametersUpdatePending_ = false;
        }
        if (updatePath && self->pathEdit_) {
            SetWindowTextW(self->pathEdit_, path.c_str());
        }
        if (updateStatus && self->status_) {
            SetWindowTextW(self->status_, status.c_str());
        }
        if (updateParameters) {
            self->parameters_ = std::move(parameters);
            self->rebuildParameterControls();
        }
        return 0;
    }

    if (self && message == WM_VSCROLL) {
        SCROLLINFO info {sizeof(info), SIF_ALL};
        GetScrollInfo(window, SB_VERT, &info);
        int position = info.nPos;
        switch (LOWORD(wParam)) {
        case SB_LINEUP: position -= 28; break;
        case SB_LINEDOWN: position += 28; break;
        case SB_PAGEUP: position -= static_cast<int>(info.nPage); break;
        case SB_PAGEDOWN: position += static_cast<int>(info.nPage); break;
        case SB_THUMBTRACK: position = info.nTrackPos; break;
        default: return 0;
        }
        info.fMask = SIF_POS;
        info.nPos = position;
        SetScrollInfo(window, SB_VERT, &info, TRUE);
        GetScrollInfo(window, SB_VERT, &info);
        self->parameterScrollOffset_ = info.nPos;
        RECT bounds {};
        GetClientRect(window, &bounds);
        self->layoutControls(
            std::max(1L, bounds.right - bounds.left),
            std::max(1L, bounds.bottom - bounds.top)
        );
        return 0;
    }
    if (self && message == WM_MOUSEWHEEL) {
        const int lines = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
        for (int index = 0; index < std::abs(lines); ++index) {
            SendMessageW(
                window,
                WM_VSCROLL,
                lines > 0 ? SB_LINEUP : SB_LINEDOWN,
                0
            );
        }
        return 0;
    }

    if (self && message == WM_COMMAND &&
        LOWORD(wParam) == kBrowseButtonId &&
        HIWORD(wParam) == BN_CLICKED) {
        self->chooseMachine();
        return 0;
    }
    if (self && message == WM_COMMAND &&
        HIWORD(wParam) == EN_KILLFOCUS &&
        LOWORD(wParam) >= kParameterControlBase &&
        LOWORD(wParam) < kParameterControlBase + 256) {
        const size_t index = LOWORD(wParam) - kParameterControlBase;
        if (index < self->parameters_.size()) {
            wchar_t text[64] {};
            GetWindowTextW(reinterpret_cast<HWND>(lParam), text, 64);
            wchar_t* end = nullptr;
            const long value = std::wcstol(text, &end, 10);
            if (end != text) self->controller_.tweakMachineParameter(index,
                static_cast<int32_t>(value));
        }
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

void LoaderEditorView::rebuildParameterControls() {
    for (auto control : parameterLabels_) DestroyWindow(control);
    for (auto control : parameterEdits_) DestroyWindow(control);
    parameterLabels_.clear();
    parameterEdits_.clear();
    parameterScrollOffset_ = 0;
    for (size_t index = 0;
         index < parameters_.size() &&
             index < kMaximumEditorParameters;
         ++index) {
        const auto labelText =
            parameters_[index].name + " [" +
            std::to_string(parameters_[index].minimum) + ".." +
            std::to_string(parameters_[index].maximum) + "]";
        auto label = CreateWindowExW(
            0, L"STATIC", utf8ToWide(labelText).c_str(),
            WS_CHILD | WS_VISIBLE, 20, 164, 300, 24, container_, nullptr,
            GetModuleHandleW(nullptr), nullptr
        );
        auto edit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            330, 164, 90, 24, container_,
            reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(kParameterControlBase + index)
            ),
            GetModuleHandleW(nullptr), nullptr
        );
        wchar_t value[32] {};
        swprintf_s(value, L"%d", parameters_[index].value);
        SetWindowTextW(edit, value);
        applyDefaultFont(label);
        applyDefaultFont(edit);
        parameterLabels_.push_back(label);
        parameterEdits_.push_back(edit);
    }
    layoutControls(kEditorWidth, currentHeight_);
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

void LoaderEditorView::layoutControls(int width, int height) {
    currentHeight_ = height;
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
    for (size_t i = 0; i < parameterLabels_.size(); ++i) {
        const int y =
            164 + static_cast<int>(i) * 28 - parameterScrollOffset_;
        MoveWindow(parameterLabels_[i], 20, y, 300, 24, TRUE);
        MoveWindow(parameterEdits_[i], 330, y, 90, 24, TRUE);
    }
    SCROLLINFO info {sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
    info.nMin = 0;
    info.nMax = std::max(
        0,
        164 + static_cast<int>(parameterLabels_.size()) * 28 - 1
    );
    info.nPage = static_cast<UINT>(std::max(1, height));
    info.nPos = parameterScrollOffset_;
    SetScrollInfo(container_, SB_VERT, &info, TRUE);
    SCROLLINFO current {sizeof(current), SIF_POS};
    GetScrollInfo(container_, SB_VERT, &current);
    parameterScrollOffset_ = current.nPos;
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
    parameterLabels_.clear();
    parameterEdits_.clear();
}
#endif

} // namespace psycle::loader