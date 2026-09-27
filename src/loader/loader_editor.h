#pragma once

#include "public.sdk/source/common/pluginview.h"
#include "loader_controller.h"

#include <mutex>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace psycle::loader {

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
    Steinberg::tresult PLUGIN_API onWheel(float distance) override;
    Steinberg::tresult PLUGIN_API canResize() override;
    Steinberg::tresult PLUGIN_API checkSizeConstraint(
        Steinberg::ViewRect* rect
    ) override;

    void updateMachinePath(const std::string& path);
    void updateStatus(const std::string& status, bool success);
    void updateParameters(const std::vector<EditorParameter>& parameters);

private:
#if defined(_WIN32)
    static LRESULT CALLBACK windowProc(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam
    );
    static LRESULT CALLBACK parameterPanelProc(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam
    );
    static LRESULT CALLBACK parameterChildProc(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam,
        UINT_PTR id,
        DWORD_PTR data
    );

    void createControls();
    void rebuildParameterControls();
    void commitParameterEdit(size_t index);
    void setParameterValue(size_t index, int32_t value, bool sendTweak);
    void chooseMachine();
    void requestLargerView();
    void layoutControls(int width, int height);
    void layoutParameterPanel();
    void scrollParameters(int command, int trackPosition = 0);
    void destroyControls();

    HWND container_ = nullptr;
    HWND parameterPanel_ = nullptr;
    HWND title_ = nullptr;
    HWND description_ = nullptr;
    HWND pathEdit_ = nullptr;
    HWND browseButton_ = nullptr;
    HWND resizeButton_ = nullptr;
    HWND status_ = nullptr;
    std::vector<HWND> parameterLabels_;
    std::vector<HWND> parameterEdits_;
    std::vector<HWND> parameterSliders_;
    std::vector<EditorParameter> parameters_;
    int parameterScrollOffset_ = 0;
    int currentHeight_ = 420;
    int wheelRemainder_ = 0;
    float hostWheelRemainder_ = 0.0f;
    WNDPROC originalWindowProc_ = nullptr;
    WNDPROC originalPanelProc_ = nullptr;
    bool rebuildingControls_ = false;
    std::mutex pendingUpdateMutex_;
    std::wstring pendingPath_;
    std::wstring pendingStatus_;
    std::vector<EditorParameter> pendingParameters_;
    bool pathUpdatePending_ = false;
    bool statusUpdatePending_ = false;
    bool parametersUpdatePending_ = false;
#endif

    LoaderController& controller_;
};

} // namespace psycle::loader