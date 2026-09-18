#include "psycle_machine_loader.h"

#include "psycle_native_interface.h"
#include "windows_dependency_diagnostics.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace psycle::loader {

using psycle::plugin_interface::CMachineInfo;
using psycle::plugin_interface::CMachineInterface;
using psycle::plugin_interface::CFxCallback;
using psycle::plugin_interface::CreateMachineFn;
using psycle::plugin_interface::DeleteMachineFn;
using psycle::plugin_interface::GetInfoFn;

class PsycleMachineLoader::HostCallbacks final : public CFxCallback {
public:
    explicit HostCallbacks(double sampleRate)
        : sampleRate_(static_cast<int>(sampleRate)) {}

    void setSampleRate(double sampleRate) noexcept {
        sampleRate_ = static_cast<int>(sampleRate);
    }

    void MessBox(char const*, char const*, unsigned int) const override {}

    int CallbackFunc(int, int, int, void*) override {
        return 0;
    }

    float* unused0(int, int) override {
        return nullptr;
    }

    float* unused1(int, int) override {
        return nullptr;
    }

    int GetTickLength() const override {
        return 1;
    }

    int GetSamplingRate() const override {
        return sampleRate_;
    }

    int GetBPM() const override {
        return 120;
    }

    int GetTPB() const override {
        return 4;
    }

    bool FileBox(bool, char[], char[]) override {
        return false;
    }

private:
    int sampleRate_;
};

struct PsycleMachineLoader::Module {
#if defined(_WIN32)
    HMODULE handle = nullptr;
#endif
    GetInfoFn getInfo = nullptr;
    CreateMachineFn createMachine = nullptr;
    DeleteMachineFn deleteMachine = nullptr;
    CMachineInfo const* info = nullptr;
    CMachineInterface* machine = nullptr;
    bool effectMode = false;
};

namespace {

constexpr size_t kMaximumParameters = 64 * 1024;
constexpr size_t kMaximumMachineDataBytes = 16 * 1024 * 1024;
constexpr float kPsycleAudioRange = 32768.0f;
constexpr int kPsycleNoteOff = psycle::plugin_interface::NOTE_MAX + 1;

#if defined(_WIN32)
template <typename Function>
Function findSymbol(HMODULE handle, const char* name) {
    return reinterpret_cast<Function>(GetProcAddress(handle, name));
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
#endif

void clearOutputs(float* const* outputs, uint32_t channels, uint32_t frames) noexcept {
    if (!outputs) {
        return;
    }

    for (uint32_t channel = 0; channel < channels; ++channel) {
        if (outputs[channel]) {
            std::fill(outputs[channel], outputs[channel] + frames, 0.0f);
        }
    }
}

} // namespace

PsycleMachineLoader::PsycleMachineLoader()
    : module_(std::make_unique<Module>()),
      callbacks_(std::make_unique<HostCallbacks>(44100.0)) {}

PsycleMachineLoader::~PsycleMachineLoader() {
    unload();
}

bool PsycleMachineLoader::load(
    const std::string& path,
    bool effectMode,
    double sampleRate,
    uint32_t
) {
    unload();
    lastError_.clear();

#if !defined(_WIN32)
    (void)path;
    (void)effectMode;
    (void)sampleRate;
    lastError_ = "Psycle native machine loading is currently Windows-only.";
    return false;
#else
    const auto widePath = utf8ToWide(path);
    if (widePath.empty()) {
        lastError_ = "The selected machine path is not valid UTF-8.";
        return false;
    }

    DWORD errorCode = ERROR_SUCCESS;
    std::string dependencyDiagnostic;
    module_->handle = loadWindowsMachineModule(
        widePath,
        errorCode,
        dependencyDiagnostic
    );
    if (!module_->handle) {
        lastError_ = errorCode == ERROR_MOD_NOT_FOUND
            ? "Windows could not find a DLL required by this Psycle machine "
              "(error 126)." + dependencyDiagnostic
            : "Windows could not load the selected Psycle machine (error " +
                std::to_string(errorCode) + ").";
        return false;
    }

    module_->getInfo = findSymbol<GetInfoFn>(module_->handle, "GetInfo");
    module_->createMachine =
        findSymbol<CreateMachineFn>(module_->handle, "CreateMachine");
    module_->deleteMachine =
        findSymbol<DeleteMachineFn>(module_->handle, "DeleteMachine");

    if (!module_->getInfo || !module_->createMachine || !module_->deleteMachine) {
        lastError_ = "The DLL does not expose the Psycle 1.12 native machine ABI.";
        unload();
        return false;
    }

    module_->info = module_->getInfo();
    if (!module_->info) {
        lastError_ = "The Psycle machine returned no descriptor.";
        unload();
        return false;
    }

    const auto apiVersion = module_->info->APIVersion;
    if ((apiVersion & 0xFFF0) !=
        (psycle::plugin_interface::MI_VERSION & 0xFFF0)) {
        lastError_ = "The Psycle machine uses an unsupported API version.";
        unload();
        return false;
    }

    const bool isEffect = module_->info->Flags == CMachineInfo::EFFECT;
    const bool isGenerator = module_->info->Flags == CMachineInfo::GENERATOR;
    if (!isEffect && !isGenerator) {
        lastError_ = "The Psycle machine is not a generator or audio effect.";
        unload();
        return false;
    }
    if (effectMode != isEffect) {
        lastError_ = effectMode
            ? "The selected Psycle machine is a generator, not an effect."
            : "The selected Psycle machine is an effect, not a generator.";
        unload();
        return false;
    }

    module_->machine = module_->createMachine();
    if (!module_->machine) {
        lastError_ = "The Psycle machine could not create its runtime instance.";
        unload();
        return false;
    }

    module_->effectMode = effectMode;
    callbacks_->setSampleRate(sampleRate);
    module_->machine->pCB = callbacks_.get();
    module_->machine->Init();
    loadedPath_ = path;
    loadedName_ = module_->info->Name ? module_->info->Name : "Psycle machine";
    return true;
#endif
}

void PsycleMachineLoader::unload() {
#if defined(_WIN32)
    if (module_ && module_->machine) {
        module_->machine->Stop();
        if (module_->deleteMachine) {
            module_->deleteMachine(*module_->machine);
        }
        module_->machine = nullptr;
    }

    if (module_ && module_->handle) {
        FreeLibrary(module_->handle);
        module_->handle = nullptr;
    }
#else
    if (module_) {
        module_->machine = nullptr;
    }
#endif

    if (module_) {
        module_->getInfo = nullptr;
        module_->createMachine = nullptr;
        module_->deleteMachine = nullptr;
        module_->info = nullptr;
        module_->effectMode = false;
    }
    loadedPath_.clear();
    loadedName_.clear();
}

bool PsycleMachineLoader::isLoaded() const noexcept {
    return module_ && module_->machine != nullptr;
}

const std::string& PsycleMachineLoader::loadedPath() const noexcept {
    return loadedPath_;
}

const std::string& PsycleMachineLoader::loadedName() const noexcept {
    return loadedName_;
}

const std::string& PsycleMachineLoader::lastError() const noexcept {
    return lastError_;
}

bool PsycleMachineLoader::captureState(
    std::vector<int32_t>& parameters,
    std::vector<uint8_t>& data
) {
    parameters.clear();
    data.clear();
    if (!module_ || !module_->machine || !module_->info) {
        return false;
    }

    const int parameterCount = module_->info->numParameters;
    if (parameterCount < 0 ||
        static_cast<size_t>(parameterCount) > kMaximumParameters) {
        return false;
    }
    if (parameterCount > 0 && !module_->machine->Vals) {
        return false;
    }
    if (parameterCount > 0) {
        parameters.assign(
            module_->machine->Vals,
            module_->machine->Vals + parameterCount
        );
    }

    const int dataSize = module_->machine->GetDataSize();
    if (dataSize < 0 ||
        static_cast<size_t>(dataSize) > kMaximumMachineDataBytes) {
        parameters.clear();
        return false;
    }
    data.resize(static_cast<size_t>(dataSize));
    if (!data.empty()) {
        module_->machine->GetData(data.data());
    }
    return true;
}

bool PsycleMachineLoader::restoreState(
    const std::vector<int32_t>& parameters,
    const std::vector<uint8_t>& data
) {
    if (!module_ || !module_->machine || !module_->info ||
        parameters.size() > kMaximumParameters ||
        data.size() > kMaximumMachineDataBytes) {
        return false;
    }

    const auto availableParameters = static_cast<size_t>(
        std::max(0, module_->info->numParameters)
    );
    const auto restoreCount = std::min(
        parameters.size(),
        availableParameters
    );
    for (size_t index = 0; index < restoreCount; ++index) {
        module_->machine->ParameterTweak(
            static_cast<int>(index),
            parameters[index]
        );
    }
    if (!data.empty()) {
        module_->machine->PutData(
            const_cast<uint8_t*>(data.data())
        );
    }
    return true;
}

void PsycleMachineLoader::process(
    const float* const* inputs,
    float* const* outputs,
    uint32_t channels,
    uint32_t frames,
    const MachineEvent* events,
    uint32_t eventCount
) noexcept {
    if (!module_ || !module_->machine || !outputs || channels == 0) {
        clearOutputs(outputs, channels, frames);
        return;
    }

    float* left = outputs[0];
    float* right = channels > 1 && outputs[1] ? outputs[1] : left;
    if (!left || !right) {
        clearOutputs(outputs, channels, frames);
        return;
    }

    if (module_->effectMode) {
        if (inputs && inputs[0]) {
            for (uint32_t sample = 0; sample < frames; ++sample) {
                left[sample] = inputs[0][sample] * kPsycleAudioRange;
            }
        } else {
            std::fill(left, left + frames, 0.0f);
        }
        if (right != left) {
            if (inputs && inputs[1]) {
                for (uint32_t sample = 0; sample < frames; ++sample) {
                    right[sample] =
                        inputs[1][sample] * kPsycleAudioRange;
                }
            } else {
                std::fill(right, right + frames, 0.0f);
            }
        }
    } else {
        std::fill(left, left + frames, 0.0f);
        if (right != left) {
            std::fill(right, right + frames, 0.0f);
        }
    }

    uint32_t cursor = 0;
    uint32_t eventIndex = 0;
    while (eventIndex < eventCount) {
        const uint32_t eventOffset = std::min(
            events[eventIndex].sampleOffset,
            frames
        );
        if (eventOffset > cursor) {
            module_->machine->Work(
                left + cursor,
                right + cursor,
                static_cast<int>(eventOffset - cursor),
                16
            );
            cursor = eventOffset;
        }

        do {
            const auto& event = events[eventIndex];
            module_->machine->SeqTick(
                static_cast<int>(event.channel),
                event.type == 0
                    ? static_cast<int>(event.note)
                    : kPsycleNoteOff,
                0,
                0,
                0
            );
            ++eventIndex;
        } while (
            eventIndex < eventCount &&
            std::min(events[eventIndex].sampleOffset, frames) == eventOffset
        );
    }
    if (cursor < frames) {
        module_->machine->Work(
            left + cursor,
            right + cursor,
            static_cast<int>(frames - cursor),
            16
        );
    }

    const float vstScale = 1.0f / kPsycleAudioRange;
    for (uint32_t sample = 0; sample < frames; ++sample) {
        left[sample] *= vstScale;
        if (right != left) {
            right[sample] *= vstScale;
        }
    }

    for (uint32_t channel = 2; channel < channels; ++channel) {
        if (outputs[channel]) {
            std::fill(outputs[channel], outputs[channel] + frames, 0.0f);
        }
    }
}

} // namespace psycle::loader