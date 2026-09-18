#include "loader_controller.h"

#include "loader_editor.h"
#include "loader_messages.h"
#include "loader_state.h"

#include "base/source/fstring.h"
#include "pluginterfaces/vst/ivstmessage.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace psycle::loader {

using namespace Steinberg;
using namespace Steinberg::Vst;

FUnknown* LoaderController::createInstance(void*) {
    return static_cast<IEditController*>(new LoaderController());
}

tresult PLUGIN_API LoaderController::initialize(FUnknown* context) {
    const auto result = EditControllerEx1::initialize(context);
    if (result != kResultOk) {
        return result;
    }

    parameters.addParameter(
        STR16("Output Gain"),
        STR16("Gain"),
        0,
        1.0,
        ParameterInfo::kCanAutomate,
        0
    );
    return kResultOk;
}

tresult PLUGIN_API LoaderController::setComponentState(IBStream* state) {
    LoaderState loadedState;
    if (!readLoaderState(state, loadedState)) {
        return kResultFalse;
    }

    setParamNormalized(
        0,
        std::clamp<double>(loadedState.outputGain, 0.0, 1.0)
    );
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        machinePath_ = std::move(loadedState.machinePath);
    }
    return kResultOk;
}

tresult PLUGIN_API LoaderController::notify(IMessage* message) {
    if (message && FIDStringsEqual(
            message->getMessageID(), kMachineParametersMessageId)) {
        const void* raw = nullptr; uint32 size = 0;
        auto* attrs = message->getAttributes();
        if (!attrs || attrs->getBinary(kMachineParametersAttributeId, raw, size) != kResultOk)
            return kResultFalse;
        const auto* bytes = static_cast<const uint8_t*>(raw);
        size_t offset = 0;
        auto read = [&](void* out, size_t n) {
            if (offset + n > size) return false;
            std::memcpy(out, bytes + offset, n); offset += n; return true;
        };
        uint32_t version = 0, count = 0; uint64_t generation = 0;
        if (!read(&version, 4) || version != 1 || !read(&generation, 8) ||
            !read(&count, 4) || count > kMaximumEditorParameters)
            return kResultFalse;
        std::vector<EditorParameter> parsed;
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t ns = 0, ds = 0; EditorParameter p;
            if (!read(&ns, 4) || ns > 1024 || offset + ns > size) return kResultFalse;
            p.name.assign(reinterpret_cast<const char*>(bytes + offset), ns); offset += ns;
            if (!read(&ds, 4) || ds > 1024 || offset + ds > size) return kResultFalse;
            p.description.assign(reinterpret_cast<const char*>(bytes + offset), ds); offset += ds;
            if (!read(&p.minimum, 4) || !read(&p.maximum, 4) || !read(&p.value, 4) ||
                p.minimum > p.maximum) return kResultFalse;
            p.value = std::clamp(p.value, p.minimum, p.maximum);
            parsed.push_back(std::move(p));
        }
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (generation < machineGeneration_) {
                return kResultOk;
            }
            parameters_ = parsed;
            machineGeneration_ = generation;
        }
        std::lock_guard<std::mutex> editorLock(editorsMutex_);
        {
            std::lock_guard<std::mutex> stateLock(stateMutex_);
            if (generation != machineGeneration_) {
                return kResultOk;
            }
        }
        for (auto* editor : editors_) {
            if (editor) {
                editor->updateParameters(parsed);
            }
        }
        return kResultOk;
    }
    if (!message ||
        !FIDStringsEqual(message->getMessageID(), kMachineStatusMessageId)) {
        return EditControllerEx1::notify(message);
    }

    int64 successValue = 0;
    std::array<TChar, 4096> statusBuffer {};
    std::array<TChar, 4096> pathBuffer {};
    auto* attributes = message->getAttributes();
    if (!attributes ||
        attributes->getInt(
            kMachineStatusSuccessAttributeId,
            successValue
        ) != kResultOk ||
        attributes->getString(
            kMachineStatusTextAttributeId,
            statusBuffer.data(),
            static_cast<uint32>(sizeof(statusBuffer))
        ) != kResultOk ||
        attributes->getString(
            kMachinePathAttributeId,
            pathBuffer.data(),
            static_cast<uint32>(sizeof(pathBuffer))
        ) != kResultOk) {
        return kResultFalse;
    }

    String statusString(statusBuffer.data());
    statusString.toMultiByte(kCP_Utf8);
    String pathString(pathBuffer.data());
    pathString.toMultiByte(kCP_Utf8);

    const bool success = successValue != 0;
    std::string activePath;
    if (success) {
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            machinePath_ = pathString.text8();
            activePath = machinePath_;
        }
        setDirty(true);
    } else {
        activePath = machinePath();
    }

    {
        std::lock_guard<std::mutex> lock(editorsMutex_);
        for (auto* editor : editors_) {
            if (!editor) {
                continue;
            }
            editor->updateMachinePath(activePath);
            editor->updateStatus(statusString.text8(), success);
        }
    }
    return kResultOk;
}

IPlugView* PLUGIN_API LoaderController::createView(FIDString name) {
    if (FIDStringsEqual(name, ViewType::kEditor)) {
        return new LoaderEditorView(*this);
    }
    return nullptr;
}

void LoaderController::selectMachinePath(const std::string& path) {
    IMessage* message = allocateMessage();
    if (!message) {
        {
            std::lock_guard<std::mutex> lock(editorsMutex_);
            for (auto* editor : editors_) {
                if (editor) {
                    editor->updateStatus(
                        "The host could not create a loader message.",
                        false
                    );
                }
            }
        }
        return;
    }

    message->setMessageID(kMachinePathMessageId);
    String pathString(path.c_str(), kCP_Utf8);
    message->getAttributes()->setString(
        kMachinePathAttributeId,
        pathString.text16()
    );
    const auto result = sendMessage(message);
    message->release();
    if (result != kResultOk) {
        const auto activePath = machinePath();
        {
            std::lock_guard<std::mutex> lock(editorsMutex_);
            for (auto* editor : editors_) {
                if (editor) {
                    editor->updateMachinePath(activePath);
                    editor->updateStatus(
                        "The host did not connect the editor to the audio processor.",
                        false
                    );
                }
            }
        }
    }
}

void LoaderController::tweakMachineParameter(size_t index, int32_t value) {
    EditorParameter parameter;
    uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (index >= parameters_.size()) return;
        parameter = parameters_[index];
        value = std::clamp(value, parameter.minimum, parameter.maximum);
        parameters_[index].value = value;
        generation = machineGeneration_;
    }
    IMessage* message = allocateMessage();
    if (!message) return;
    message->setMessageID(kMachineTweakMessageId);
    // The processor validates the index/value; use a compact, generation-neutral
    // payload for compatibility with hosts that replay editor messages.
    struct Tweak { uint64_t generation; int32_t index; int32_t value; }
        tweak {generation, static_cast<int32_t>(index), value};
    message->getAttributes()->setBinary(
        kMachineTweakAttributeId, &tweak, static_cast<uint32>(sizeof(tweak)));
    sendMessage(message);
    message->release();
}

std::string LoaderController::machinePath() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return machinePath_;
}

void LoaderController::registerEditor(LoaderEditorView* editor) {
    std::vector<EditorParameter> parameterSnapshot;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        parameterSnapshot = parameters_;
    }
    std::lock_guard<std::mutex> lock(editorsMutex_);
    if (editor &&
        std::find(editors_.begin(), editors_.end(), editor) == editors_.end()) {
        editors_.push_back(editor);
        if (!parameterSnapshot.empty()) {
            editor->updateParameters(parameterSnapshot);
        }
    }
}

void LoaderController::unregisterEditor(LoaderEditorView* editor) {
    std::lock_guard<std::mutex> lock(editorsMutex_);
    editors_.erase(
        std::remove(editors_.begin(), editors_.end(), editor),
        editors_.end()
    );
}

} // namespace psycle::loader