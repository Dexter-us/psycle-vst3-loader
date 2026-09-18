#pragma once

#include "public.sdk/source/common/pluginview.h"

#include <mutex>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace psycle::loader {

class LoaderController;

class LoaderEditorView final : public Steinberg::CPluginView {
public:
    explicit LoaderEditorView(LoaderController& controller);
    ~LoaderEditorView() override;

    Steinberg::tresult PLUGIN_API isPlatformTypeSupported(
        Steinberg::FIDString type
    ) override;
    Steinberg::tresult PLUGIN_API attached(
        void* parent,
        Steinberg::FIDString type
    ) override;
    Steinberg::tresult PLUGIN_API removed() override;
    Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect* newSize) override;

    void updateMachinePath(const std::string& path);
    void updateStatus(const std::string& status, bool success);

private:
#if defined(_WIN32)
    static LRESULT CALLBACK windowProc(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam
    );

    void createControls();
    void chooseMachine();
    void layoutControls(int width, int height);
    void destroyControls();

    HWND container_ = nullptr;
    HWND title_ = nullptr;
    HWND description_ = nullptr;
    HWND pathEdit_ = nullptr;
    HWND browseButton_ = nullptr;
    HWND status_ = nullptr;
    WNDPROC originalWindowProc_ = nullptr;
    std::mutex pendingUpdateMutex_;
    std::wstring pendingPath_;
    std::wstring pendingStatus_;
    bool pathUpdatePending_ = false;
    bool statusUpdatePending_ = false;
#endif

    LoaderController& controller_;
};

} // namespace psycle::loader