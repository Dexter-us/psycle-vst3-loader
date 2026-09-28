#include "loader_editor.h"

#include "loader_controller.h"
#include "loader_messages.h"

#include "pluginterfaces/gui/iplugview.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <limits>
#include <utility>

#if defined(_WIN32)
#include <commdlg.h>
#include <commctrl.h>
#include <windowsx.h>
#include <string_view>
#include <vector>
#endif

namespace psycle::loader {

using namespace Steinberg;

namespace {

constexpr int kEditorWidth = 560;
constexpr int kEditorHeight = 420;
constexpr int kMinimumWidth = 460;
constexpr int kMinimumHeight = 240;

#if defined(_WIN32)
constexpr int kBrowseButtonId = 1001;
constexpr int kResizeButtonId = 1002;
constexpr int kScrollbarControlId = 1003;
constexpr int kParameterControlBase = 2000;
constexpr int kSliderControlBase = 3000;
constexpr int kPanelTop = 164;
constexpr int kParameterRowHeight = 32;
constexpr int kSliderSteps = 10000;
constexpr UINT kUiUpdateMessage = WM_APP + 0x510;

int sliderPosition(const EditorParameter& parameter) {
    const auto span = static_cast<int64_t>(parameter.maximum) -
                      parameter.minimum;
    if (span <= 0) return 0;
    const auto offset = static_cast<int64_t>(parameter.value) -
                        parameter.minimum;
    return static_cast<int>(
        (offset * kSliderSteps + span / 2) / span
    );
}

int32_t sliderValue(const EditorParameter& parameter, int position) {
    const auto span = static_cast<int64_t>(parameter.maximum) -
                      parameter.minimum;
    return static_cast<int32_t>(
        static_cast<int64_t>(parameter.minimum) +
        (static_cast<int64_t>(std::clamp(position, 0, kSliderSteps)) *
             span + kSliderSteps / 2) /
            kSliderSteps
    );
}

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
    INITCOMMONCONTROLSEX controls {};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_BAR_CLASSES;
    if (!InitCommonControlsEx(&controls)) {
        CPluginView::removed();
        return kResultFalse;
    }
    container_ = CreateWindowExW(
        0,
        L"STATIC",
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | SS_NOTIFY,
        0,
        0,
        getRect().getWidth(),
        getRect().getHeight(),
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
    layoutControls(getRect().getWidth(), getRect().getHeight());
    controller_.registerEditor(this);
    SetTimer(container_, 1, 1000, nullptr);
    RedrawWindow(
        container_, nullptr, nullptr,
        RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW
    );
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
        RedrawWindow(
            container_, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW
        );
    }
#endif
    return result;
}

tresult PLUGIN_API LoaderEditorView::onWheel(float distance) {
#if defined(_WIN32)
    if (parameterPanel_ && distance != 0.0f) {
        hostWheelRemainder_ += std::clamp(distance, -32.0f, 32.0f);
        while (hostWheelRemainder_ >= 1.0f) {
            PostMessageW(parameterPanel_, WM_VSCROLL, SB_LINEUP, 0);
            hostWheelRemainder_ -= 1.0f;
        }
        while (hostWheelRemainder_ <= -1.0f) {
            PostMessageW(parameterPanel_, WM_VSCROLL, SB_LINEDOWN, 0);
            hostWheelRemainder_ += 1.0f;
        }
        return kResultTrue;
    }
#else
    (void)distance;
#endif
    return kResultFalse;
}

tresult PLUGIN_API LoaderEditorView::canResize() {
    return kResultTrue;
}

tresult PLUGIN_API LoaderEditorView::checkSizeConstraint(ViewRect* rect) {
    if (!rect) return kInvalidArgument;
    rect->right = rect->left +
        std::max(kMinimumWidth, rect->getWidth());
    rect->bottom = rect->top +
        std::max(kMinimumHeight, rect->getHeight());
    return kResultTrue;
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

    if (self && message == WM_TIMER && wParam == 1) {
        self->controller_.refreshMachineState();
        return 0;
    }
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
        RedrawWindow(
            self->container_, nullptr, nullptr,
            RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW
        );
        return 0;
    }

    if (self && message == WM_PAINT) {
        PAINTSTRUCT paint {};
        HDC dc = BeginPaint(window, &paint);
        FillRect(dc, &paint.rcPaint, GetSysColorBrush(COLOR_BTNFACE));
        EndPaint(window, &paint);
        return 0;
    }
    if (self && message == WM_ERASEBKGND) return 1;
    if (self && message == WM_CTLCOLORSTATIC) {
        auto dc = reinterpret_cast<HDC>(wParam);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
    }

    if (self && message == WM_MOUSEWHEEL && self->parameterPanel_) {
        SendMessageW(self->parameterPanel_, message, wParam, lParam);
        return 0;
    }
    if (self && message == WM_VSCROLL &&
        reinterpret_cast<HWND>(lParam) == self->parameterScrollbar_) {
        SCROLLINFO info {};
        info.cbSize = sizeof(info);
        info.fMask = SIF_TRACKPOS;
        GetScrollInfo(self->parameterScrollbar_, SB_CTL, &info);
        self->scrollParameters(LOWORD(wParam), info.nTrackPos);
        return 0;
    }

    if (self && message == WM_COMMAND &&
        LOWORD(wParam) == kBrowseButtonId &&
        HIWORD(wParam) == BN_CLICKED) {
        self->chooseMachine();
        return 0;
    }
    if (self && message == WM_COMMAND &&
        LOWORD(wParam) == kResizeButtonId &&
        HIWORD(wParam) == BN_CLICKED) {
        self->requestLargerView();
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

LRESULT CALLBACK LoaderEditorView::parameterPanelProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    auto* self = reinterpret_cast<LoaderEditorView*>(
        GetWindowLongPtrW(window, GWLP_USERDATA)
    );
    if (self && message == WM_VSCROLL) {
        self->scrollParameters(LOWORD(wParam));
        return 0;
    }
    if (self && message == WM_PAINT) {
        PAINTSTRUCT paint {};
        HDC dc = BeginPaint(window, &paint);
        FillRect(dc, &paint.rcPaint, GetSysColorBrush(COLOR_BTNFACE));
        EndPaint(window, &paint);
        return 0;
    }
    if (self && message == WM_ERASEBKGND) return 1;
    if (self && message == WM_CTLCOLORSTATIC) {
        auto dc = reinterpret_cast<HDC>(wParam);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
    }
    if (self && message == WM_MOUSEWHEEL) {
        self->wheelRemainder_ += GET_WHEEL_DELTA_WPARAM(wParam);
        while (self->wheelRemainder_ >= WHEEL_DELTA) {
            self->scrollParameters(SB_LINEUP);
            self->wheelRemainder_ -= WHEEL_DELTA;
        }
        while (self->wheelRemainder_ <= -WHEEL_DELTA) {
            self->scrollParameters(SB_LINEDOWN);
            self->wheelRemainder_ += WHEEL_DELTA;
        }
        return 0;
    }
    if (self && message == WM_HSCROLL && lParam) {
        const int id = GetDlgCtrlID(reinterpret_cast<HWND>(lParam));
        if (id >= kSliderControlBase &&
            id < kSliderControlBase + static_cast<int>(self->parameterSliders_.size())) {
            const auto index = static_cast<size_t>(id - kSliderControlBase);
            const auto position = static_cast<int>(SendMessageW(
                self->parameterSliders_[index], TBM_GETPOS, 0, 0
            ));
            self->setParameterValue(
                index, sliderValue(self->parameters_[index], position), true
            );
        }
        return 0;
    }
    if (self && !self->rebuildingControls_ && message == WM_COMMAND &&
        HIWORD(wParam) == EN_KILLFOCUS &&
        LOWORD(wParam) >= kParameterControlBase &&
        LOWORD(wParam) < kParameterControlBase +
                            static_cast<int>(self->parameterEdits_.size())) {
        self->commitParameterEdit(
            static_cast<size_t>(LOWORD(wParam) - kParameterControlBase)
        );
        return 0;
    }
    return self && self->originalPanelProc_
        ? CallWindowProcW(self->originalPanelProc_, window, message, wParam, lParam)
        : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK LoaderEditorView::parameterChildProc(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR id, DWORD_PTR data
) {
    auto* self = reinterpret_cast<LoaderEditorView*>(data);
    if (message == WM_MOUSEWHEEL && self && self->parameterPanel_) {
        SendMessageW(self->parameterPanel_, message, wParam, lParam);
        return 0;
    }
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, parameterChildProc, id);
    }
    return DefSubclassProc(window, message, wParam, lParam);
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
    resizeButton_ = CreateWindowExW(
        0, L"BUTTON", L"Larger view",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        432, 12, 108, 27, container_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kResizeButtonId)),
        GetModuleHandleW(nullptr), nullptr
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
    parameterPanel_ = CreateWindowExW(
        0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_NOTIFY | WS_CLIPCHILDREN,
        0, kPanelTop, kEditorWidth, kEditorHeight - kPanelTop,
        container_, nullptr, GetModuleHandleW(nullptr), nullptr
    );
    parameterScrollbar_ = CreateWindowExW(
        0, L"SCROLLBAR", L"",
        WS_CHILD | SBS_VERT,
        kEditorWidth - 30, kPanelTop, 18, kEditorHeight - kPanelTop,
        container_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kScrollbarControlId)),
        GetModuleHandleW(nullptr), nullptr
    );
    if (parameterScrollbar_) {
        SetWindowSubclass(
            parameterScrollbar_, parameterChildProc, 1,
            reinterpret_cast<DWORD_PTR>(this)
        );
    }
    if (parameterPanel_) {
        SetWindowLongPtrW(
            parameterPanel_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this)
        );
        originalPanelProc_ = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
            parameterPanel_, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(&LoaderEditorView::parameterPanelProc)
        ));
    }

    applyDefaultFont(title_);
    applyDefaultFont(description_);
    applyDefaultFont(pathEdit_);
    applyDefaultFont(browseButton_);
    applyDefaultFont(resizeButton_);
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
    rebuildingControls_ = true;
    for (auto control : parameterLabels_) DestroyWindow(control);
    for (auto control : parameterEdits_) DestroyWindow(control);
    for (auto control : parameterSliders_) DestroyWindow(control);
    parameterLabels_.clear();
    parameterEdits_.clear();
    parameterSliders_.clear();
    parameterScrollOffset_ = 0;
    if (!parameterPanel_) {
        rebuildingControls_ = false;
        return;
    }
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
            WS_CHILD | WS_VISIBLE, 8, 0, 180, 24,
            parameterPanel_, nullptr,
            GetModuleHandleW(nullptr), nullptr
        );
        auto edit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            300, 0, 84, 24, parameterPanel_,
            reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(kParameterControlBase + index)
            ),
            GetModuleHandleW(nullptr), nullptr
        );
        auto slider = CreateWindowExW(
            0, TRACKBAR_CLASSW, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS,
            190, 0, 104, 26, parameterPanel_,
            reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(kSliderControlBase + index)
            ),
            GetModuleHandleW(nullptr), nullptr
        );
        wchar_t value[32] {};
        swprintf_s(
            value,
            _countof(value),
            L"%d",
            static_cast<int>(parameters_[index].value)
        );
        SetWindowTextW(edit, value);
        if (slider) {
            SendMessageW(slider, TBM_SETRANGEMIN, FALSE, 0);
            SendMessageW(slider, TBM_SETRANGEMAX, FALSE, kSliderSteps);
            SendMessageW(
                slider, TBM_SETPOS, TRUE,
                sliderPosition(parameters_[index])
            );
        }
        applyDefaultFont(label);
        applyDefaultFont(edit);
        applyDefaultFont(slider);
        if (edit) {
            SetWindowSubclass(
                edit, parameterChildProc, 1, reinterpret_cast<DWORD_PTR>(this)
            );
        }
        if (label) {
            SetWindowSubclass(
                label, parameterChildProc, 1, reinterpret_cast<DWORD_PTR>(this)
            );
        }
        if (slider) {
            SetWindowSubclass(
                slider, parameterChildProc, 1, reinterpret_cast<DWORD_PTR>(this)
            );
        }
        parameterLabels_.push_back(label);
        parameterEdits_.push_back(edit);
        parameterSliders_.push_back(slider);
    }
    rebuildingControls_ = false;
    layoutParameterPanel();
}

void LoaderEditorView::setParameterValue(
    size_t index, int32_t value, bool sendTweak
) {
    if (index >= parameters_.size() || index >= parameterEdits_.size()) return;
    auto& parameter = parameters_[index];
    value = std::clamp(value, parameter.minimum, parameter.maximum);
    const bool changed = value != parameter.value;
    parameter.value = value;
    wchar_t text[32] {};
    swprintf_s(text, _countof(text), L"%d", static_cast<int>(value));
    SetWindowTextW(parameterEdits_[index], text);
    if (parameterSliders_[index]) {
        SendMessageW(
            parameterSliders_[index], TBM_SETPOS, TRUE,
            sliderPosition(parameter)
        );
    }
    if (sendTweak && changed) {
        controller_.tweakMachineParameter(index, value);
    }
}

void LoaderEditorView::commitParameterEdit(size_t index) {
    if (index >= parameterEdits_.size()) return;
    wchar_t text[64] {};
    GetWindowTextW(parameterEdits_[index], text, _countof(text));
    wchar_t* end = nullptr;
    const auto parsed = std::wcstoll(text, &end, 10);
    if (end != text && *end == L'\0') {
        const auto safeValue = std::clamp(
            parsed,
            static_cast<long long>(std::numeric_limits<int32_t>::min()),
            static_cast<long long>(std::numeric_limits<int32_t>::max())
        );
        setParameterValue(index, static_cast<int32_t>(safeValue), true);
    } else {
        setParameterValue(index, parameters_[index].value, false);
    }
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

void LoaderEditorView::requestLargerView() {
    if (!plugFrame) {
        SetWindowTextW(status_, L"This host does not offer plugin-window resizing. Use the parameter scrollbar.");
        return;
    }
    const bool expanded = getRect().getWidth() >= 740 && getRect().getHeight() >= 580;
    ViewRect requested(0, 0, expanded ? kEditorWidth : 760,
                       expanded ? kEditorHeight : 600);
    if (plugFrame->resizeView(this, &requested) != kResultTrue) {
        SetWindowTextW(status_, L"MuseScore did not allow resizing. Use the parameter scrollbar.");
    }
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
    MoveWindow(title_, 20, 16, std::max(1, contentWidth - 124), 22, TRUE);
    MoveWindow(resizeButton_, std::max(20, width - 128), 12, 108, 27, TRUE);
    SetWindowTextW(
        resizeButton_,
        width >= 740 && height >= 580 ? L"Smaller view" : L"Larger view"
    );
    MoveWindow(description_, 20, 44, contentWidth, 20, TRUE);
    MoveWindow(status_, 20, 118, contentWidth, 36, TRUE);
    if (parameterPanel_) {
        MoveWindow(
            parameterPanel_, 12, kPanelTop, std::max(1, width - 48),
            std::max(1, height - kPanelTop - 8), TRUE
        );
        if (parameterScrollbar_) {
            MoveWindow(
                parameterScrollbar_, std::max(1, width - 32), kPanelTop,
                18, std::max(1, height - kPanelTop - 8), TRUE
            );
        }
        layoutParameterPanel();
    }
}

void LoaderEditorView::layoutParameterPanel() {
    if (!parameterPanel_) return;
    RECT bounds {};
    GetClientRect(parameterPanel_, &bounds);
    const int height = std::max(1, static_cast<int>(bounds.bottom - bounds.top));
    const int contentHeight =
        static_cast<int>(parameterLabels_.size()) * kParameterRowHeight;
    parameterScrollOffset_ = std::clamp(
        parameterScrollOffset_, 0, std::max(0, contentHeight - height)
    );
    if (parameterScrollbar_) {
        SCROLLINFO info {};
        info.cbSize = sizeof(info);
        info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        info.nMin = 0;
        info.nMax = std::max(0, contentHeight - 1);
        info.nPage = static_cast<UINT>(height);
        info.nPos = parameterScrollOffset_;
        SetScrollInfo(parameterScrollbar_, SB_CTL, &info, TRUE);
        ShowWindow(
            parameterScrollbar_, contentHeight > height ? SW_SHOW : SW_HIDE
        );
    }
    const int width = static_cast<int>(bounds.right - bounds.left);
    const int editWidth = 84;
    const int labelWidth = std::max(140, (width - editWidth - 24) * 45 / 100);
    const int sliderX = 8 + labelWidth + 8;
    const int editX = std::max(sliderX + 80, width - editWidth - 8);
    for (size_t i = 0; i < parameterLabels_.size(); ++i) {
        const int y = static_cast<int>(i) * kParameterRowHeight -
                      parameterScrollOffset_;
        MoveWindow(parameterLabels_[i], 8, y, labelWidth, 24, TRUE);
        MoveWindow(
            parameterSliders_[i], sliderX, y,
            std::max(80, editX - sliderX - 8), 26, TRUE
        );
        MoveWindow(parameterEdits_[i], editX, y, editWidth, 24, TRUE);
    }
}

void LoaderEditorView::scrollParameters(
    int command, int trackPosition
) {
    if (!parameterPanel_) return;
    RECT bounds {};
    GetClientRect(parameterPanel_, &bounds);
    const int height = std::max(1, static_cast<int>(bounds.bottom - bounds.top));
    const int contentHeight =
        static_cast<int>(parameterLabels_.size()) * kParameterRowHeight;
    int position = parameterScrollOffset_;
    switch (command) {
    case SB_LINEUP: position -= kParameterRowHeight; break;
    case SB_LINEDOWN: position += kParameterRowHeight; break;
    case SB_PAGEUP: position -= height; break;
    case SB_PAGEDOWN: position += height; break;
    case SB_THUMBPOSITION:
    case SB_THUMBTRACK: position = trackPosition; break;
    case SB_TOP: position = 0; break;
    case SB_BOTTOM: position = contentHeight - height; break;
    default: return;
    }
    parameterScrollOffset_ = std::clamp(
        position, 0, std::max(0, contentHeight - height)
    );
    layoutParameterPanel();
}

void LoaderEditorView::destroyControls() {
    if (!container_) {
        return;
    }

    KillTimer(container_, 1);
    if (parameterPanel_ && originalPanelProc_) {
        SetWindowLongPtrW(
            parameterPanel_, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(originalPanelProc_)
        );
        SetWindowLongPtrW(parameterPanel_, GWLP_USERDATA, 0);
        originalPanelProc_ = nullptr;
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
    parameterPanel_ = nullptr;
    parameterScrollbar_ = nullptr;
    title_ = nullptr;
    description_ = nullptr;
    pathEdit_ = nullptr;
    browseButton_ = nullptr;
    resizeButton_ = nullptr;
    status_ = nullptr;
    parameterLabels_.clear();
    parameterEdits_.clear();
    parameterSliders_.clear();
}
#endif

} // namespace psycle::loader