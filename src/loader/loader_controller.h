#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"

#include <mutex>
#include <string>
#include <vector>

namespace psycle::loader {

class LoaderEditorView;
struct EditorParameter {
    std::string name;
    std::string description;
    int32_t minimum = 0, maximum = 0, value = 0;
};

class LoaderController final : public Steinberg::Vst::EditControllerEx1 {
public:
    static Steinberg::FUnknown* createInstance(void*);

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::IPlugView* PLUGIN_API createView(
        Steinberg::FIDString name
    ) override;
    Steinberg::tresult PLUGIN_API setComponentState(
        Steinberg::IBStream* state
    ) override;
    Steinberg::tresult PLUGIN_API notify(
        Steinberg::Vst::IMessage* message
    ) override;

    void selectMachinePath(const std::string& path);
    std::string machinePath() const;
    void registerEditor(LoaderEditorView* editor);
    void unregisterEditor(LoaderEditorView* editor);
    void tweakMachineParameter(size_t index, int32_t value);

private:
    mutable std::mutex stateMutex_;
    std::string machinePath_;
    std::mutex editorsMutex_;
    std::vector<LoaderEditorView*> editors_;
    std::vector<EditorParameter> parameters_;
    uint64_t machineGeneration_ = 0;
};

} // namespace psycle::loader