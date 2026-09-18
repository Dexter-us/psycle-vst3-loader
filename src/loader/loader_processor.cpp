#include "loader_processor.h"

#include "loader_messages.h"
#include "loader_state.h"

#include "base/source/fstring.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmessage.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace psycle::loader {

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr char kMachinePathEnvironment[] = "PSYCLE_MACHINE_PATH";
constexpr uint32_t kParameterMessageVersion = 1;

FUnknown* createForRole(LoaderRole role) {
    return static_cast<IAudioProcessor*>(new LoaderProcessor(role));
}

void clearBuffers(ProcessData& data) {
    if (!data.outputs || data.numOutputs <= 0) {
        return;
    }
    auto& outputBus = data.outputs[0];
    for (int32 channel = 0; channel < outputBus.numChannels; ++channel) {
        auto* buffer = outputBus.channelBuffers32[channel];
        if (buffer) {
            std::fill(buffer, buffer + data.numSamples, 0.0f);
        }
    }
}

} // namespace

LoaderProcessor::LoaderProcessor(LoaderRole role)
    : role_(role),
      machine_(std::make_unique<PsycleMachineLoader>()) {
    setControllerClass(FUID(0x8D4E7A2B, 0x7D714A53, 0x9A1D4F20, 0xA1029B7F));
}

FUnknown* LoaderProcessor::createSynth(void*) {
    return createForRole(LoaderRole::Synth);
}

FUnknown* LoaderProcessor::createEffect(void*) {
    return createForRole(LoaderRole::Effect);
}

tresult PLUGIN_API LoaderProcessor::initialize(FUnknown* context) {
    const auto result = AudioEffect::initialize(context);
    if (result != kResultOk) {
        return result;
    }

    if (role_ == LoaderRole::Effect) {
        if (!addAudioInput(STR16("Audio Input"), SpeakerArr::kStereo) ||
            !addAudioOutput(STR16("Audio Output"), SpeakerArr::kStereo)) {
            return kResultFalse;
        }
    } else if (!addAudioOutput(STR16("Synth Output"), SpeakerArr::kStereo)) {
        return kResultFalse;
    }

    if (const char* path = std::getenv(kMachinePathEnvironment)) {
        machinePath_ = path;
    }
    return kResultOk;
}

tresult PLUGIN_API LoaderProcessor::terminate() {
    stopAndReleaseMachines();
    return AudioEffect::terminate();
}

tresult PLUGIN_API LoaderProcessor::setupProcessing(ProcessSetup& setup) {
    sampleRate_ = setup.sampleRate;
    maxBlockSize_ = static_cast<uint32_t>(std::max(0, setup.maxSamplesPerBlock));
    return AudioEffect::setupProcessing(setup);
}

tresult PLUGIN_API LoaderProcessor::setBusArrangements(
    SpeakerArrangement* inputs,
    int32 numIns,
    SpeakerArrangement* outputs,
    int32 numOuts
) {
    const bool stereoOutput =
        outputs && numOuts == 1 && outputs[0] == SpeakerArr::kStereo;
    const bool valid = role_ == LoaderRole::Effect
        ? inputs && numIns == 1 &&
              inputs[0] == SpeakerArr::kStereo && stereoOutput
        : numIns == 0 && stereoOutput;
    if (!valid) {
        return kResultFalse;
    }
    return AudioEffect::setBusArrangements(
        inputs,
        numIns,
        outputs,
        numOuts
    );
}

tresult PLUGIN_API LoaderProcessor::canProcessSampleSize(
    int32 symbolicSampleSize
) {
    return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API LoaderProcessor::setActive(TBool state) {
    active_.store(state != 0);
    if (state) {
        loadSelectedMachine();
    } else {
        stopAndReleaseMachines();
    }
    return AudioEffect::setActive(state);
}

tresult PLUGIN_API LoaderProcessor::process(ProcessData& data) {
    if (!data.outputs || data.numOutputs <= 0 || data.numSamples <= 0) {
        return kResultOk;
    }

    if (data.symbolicSampleSize == kSample32) {
        auto& outputBus = data.outputs[0];
        const uint32_t channels = static_cast<uint32_t>(
            std::max(0, outputBus.numChannels)
        );
        std::array<const float*, 16> inputs {};
        if (data.inputs && data.numInputs > 0) {
            const auto& inputBus = data.inputs[0];
            for (uint32_t channel = 0;
                 channel < channels &&
                 channel < static_cast<uint32_t>(inputBus.numChannels) &&
                 channel < inputs.size();
                 ++channel) {
                inputs[channel] = inputBus.channelBuffers32[channel];
            }
        }

        std::array<MachineEvent, 128> events {};
        uint32_t eventCount = 0;
        if (data.inputEvents) {
            const int32 inputEventCount = data.inputEvents->getEventCount();
            for (int32 index = 0;
                 index < inputEventCount && eventCount < events.size();
                 ++index) {
                Event event {};
                if (data.inputEvents->getEvent(index, event) != kResultOk) {
                    continue;
                }
                if (event.type == Event::kNoteOnEvent) {
                    events[eventCount++] = {
                        static_cast<uint32_t>(std::max(0, event.sampleOffset)),
                        0,
                        static_cast<uint8_t>(std::clamp<int16>(event.noteOn.channel, 0, 15)),
                        static_cast<uint8_t>(std::clamp<int16>(event.noteOn.pitch, 0, 127)),
                        event.noteOn.velocity,
                        event.noteOn.tuning,
                    };
                } else if (event.type == Event::kNoteOffEvent) {
                    events[eventCount++] = {
                        static_cast<uint32_t>(std::max(0, event.sampleOffset)),
                        1,
                        static_cast<uint8_t>(std::clamp<int16>(event.noteOff.channel, 0, 15)),
                        static_cast<uint8_t>(std::clamp<int16>(event.noteOff.pitch, 0, 127)),
                        event.noteOff.velocity,
                        event.noteOff.tuning,
                    };
                }
            }
        }
        for (uint32_t index = 0; index < eventCount; ++index) {
            events[index].sampleOffset = std::min(
                events[index].sampleOffset,
                static_cast<uint32_t>(data.numSamples)
            );
        }
        for (uint32_t index = 1; index < eventCount; ++index) {
            auto event = events[index];
            uint32_t insertion = index;
            while (
                insertion > 0 &&
                events[insertion - 1].sampleOffset > event.sampleOffset
            ) {
                events[insertion] = events[insertion - 1];
                --insertion;
            }
            events[insertion] = event;
        }

        if (data.inputParameterChanges) {
            const int32 parameterCount = data.inputParameterChanges->getParameterCount();
            for (int32 index = 0; index < parameterCount; ++index) {
                auto* queue = data.inputParameterChanges->getParameterData(index);
                if (!queue || queue->getParameterId() != kOutputGainParam) {
                    continue;
                }

                const int32 pointCount = queue->getPointCount();
                if (pointCount <= 0) {
                    continue;
                }

                int32 sampleOffset = 0;
                ParamValue value = outputGain_.load(
                    std::memory_order_relaxed
                );
                if (queue->getPoint(pointCount - 1, sampleOffset, value) == kResultOk) {
                    outputGain_.store(
                        static_cast<float>(
                            std::clamp(value, 0.0, 2.0)
                        ),
                        std::memory_order_relaxed
                    );
                }
            }
        }

        auto* machine = acquireMachineForAudio();
        if (machine) {
            applyPendingTweaks(*machine);
            machine->process(
                inputs.data(),
                outputBus.channelBuffers32,
                channels,
                static_cast<uint32_t>(data.numSamples),
                events.data(),
                eventCount
            );
        } else {
            clearBuffers(data);
        }
        releaseMachineFromAudio();

        const float outputGain = outputGain_.load(
            std::memory_order_relaxed
        );
        if (outputGain != 1.0f) {
            for (int32 channel = 0; channel < outputBus.numChannels; ++channel) {
                auto* buffer = outputBus.channelBuffers32[channel];
                if (!buffer) {
                    continue;
                }
                for (int32 sample = 0; sample < data.numSamples; ++sample) {
                    buffer[sample] *= outputGain;
                }
            }
        }
    } else {
        clearBuffers(data);
    }

    return kResultOk;
}

tresult PLUGIN_API LoaderProcessor::notify(IMessage* message) {
    if (!message) {
        return AudioEffect::notify(message);
    }
    if (FIDStringsEqual(message->getMessageID(), kMachineTweakMessageId)) {
        const void* raw = nullptr; uint32 size = 0;
        auto* attrs = message->getAttributes();
        if (!attrs || attrs->getBinary(kMachineTweakAttributeId, raw, size) != kResultOk ||
            size != sizeof(uint64_t) + sizeof(int32_t) * 2) return kResultFalse;
        const auto* bytes = static_cast<const uint8_t*>(raw);
        uint64_t generation = 0; int32_t index = 0; int32_t value = 0;
        std::memcpy(&generation, bytes, sizeof(generation));
        std::memcpy(&index, bytes + sizeof(generation), sizeof(index));
        std::memcpy(&value, bytes + sizeof(generation) + sizeof(index), sizeof(value));
        std::lock_guard<std::mutex> lock(ownershipMutex_);
        if (generation != machineGeneration_.load(std::memory_order_acquire)) {
            return kResultFalse;
        }
        const auto writeIndex = tweakWriteIndex_.load(
            std::memory_order_relaxed
        );
        const auto readIndex = tweakReadIndex_.load(
            std::memory_order_acquire
        );
        if (writeIndex - readIndex >= kTweakQueueCapacity) {
            return kResultFalse;
        }
        pendingTweaks_[writeIndex % kTweakQueueCapacity] = {
            generation,
            index,
            value
        };
        tweakWriteIndex_.store(writeIndex + 1, std::memory_order_release);
        return kResultOk;
    }
    if (!FIDStringsEqual(message->getMessageID(), kMachinePathMessageId))
        return AudioEffect::notify(message);

    std::array<TChar, 4096> pathBuffer {};
    if (!message->getAttributes() ||
        message->getAttributes()->getString(
            kMachinePathAttributeId,
            pathBuffer.data(),
            static_cast<uint32>(sizeof(pathBuffer))
        ) != kResultOk) {
        return kResultFalse;
    }

    String pathString(pathBuffer.data());
    pathString.toMultiByte(kCP_Utf8);
    const std::string requestedPath = pathString.text8();
    if (active_.load()) {
        loadMachinePath(requestedPath);
    } else {
        {
            std::lock_guard<std::mutex> lock(ownershipMutex_);
            machinePath_ = requestedPath;
        }
        sendMachineStatus(
            true,
            "Selection saved; the machine will load when the plugin activates."
        );
    }
    return kResultOk;
}

tresult PLUGIN_API LoaderProcessor::setState(IBStream* state) {
    LoaderState loadedState;
    if (!readLoaderState(state, loadedState)) {
        return kResultFalse;
    }

    {
        std::lock_guard<std::mutex> lock(ownershipMutex_);
        outputGain_.store(
            std::clamp(loadedState.outputGain, 0.0f, 2.0f),
            std::memory_order_relaxed
        );
        machinePath_ = loadedState.machinePath;
        nativeStatePath_ = loadedState.machinePath;
        savedMachineParameters_ =
            std::move(loadedState.machineParameters);
        savedMachineData_ = std::move(loadedState.machineData);
    }
    if (active_.load()) {
        loadSelectedMachine();
    }
    return kResultOk;
}

tresult PLUGIN_API LoaderProcessor::getState(IBStream* state) {
    LoaderState savedState;
    {
        std::lock_guard<std::mutex> lock(ownershipMutex_);
        savedState.outputGain = outputGain_.load(
            std::memory_order_relaxed
        );
        savedState.machinePath = machinePath_;
        savedState.machineParameters = savedMachineParameters_;
        savedState.machineData = savedMachineData_;
        auto* captureTarget = machine_.get();
        if (captureTarget && captureTarget->isLoaded()) {
            publishedMachine_.store(nullptr, std::memory_order_seq_cst);
            while (
                audioHazard_.load(std::memory_order_seq_cst) != nullptr
            ) {
                std::this_thread::yield();
            }
            applyPendingTweaks(*captureTarget);

            std::vector<int32_t> capturedParameters;
            std::vector<uint8_t> capturedData;
            if (captureTarget->captureState(
                    capturedParameters,
                    capturedData
                )) {
                savedState.machineParameters =
                    std::move(capturedParameters);
                savedState.machineData = std::move(capturedData);
                nativeStatePath_ = machinePath_;
                savedMachineParameters_ =
                    savedState.machineParameters;
                savedMachineData_ = savedState.machineData;
            }

            if (active_.load(std::memory_order_seq_cst) &&
                machine_.get() == captureTarget) {
                publishedMachine_.store(
                    captureTarget,
                    std::memory_order_seq_cst
                );
            }
        }
    }
    return writeLoaderState(state, savedState) ? kResultOk : kResultFalse;
}

void LoaderProcessor::loadSelectedMachine() {
    std::string selectedPath;
    {
        std::lock_guard<std::mutex> lock(ownershipMutex_);
        selectedPath = machinePath_;
    }
    if (selectedPath.empty()) {
        return;
    }
    loadMachinePath(selectedPath);
}

bool LoaderProcessor::loadMachinePath(const std::string& path) {
    auto replacement = std::make_unique<PsycleMachineLoader>();
    if (!replacement->load(
            path,
            role_ == LoaderRole::Effect,
            sampleRate_,
            maxBlockSize_
        )) {
        sendMachineStatus(false, replacement->lastError());
        return false;
    }

    const std::string loadedName = replacement->loadedName();
    std::vector<MachineParameterSnapshot> parameterSnapshot;
    bool stateRestored = true;
    bool deactivated = false;
    bool parameterSnapshotFailed = false;
    uint64_t publishedGeneration = 0;
    {
        std::lock_guard<std::mutex> lock(ownershipMutex_);
        if (path == nativeStatePath_ &&
            (!savedMachineParameters_.empty() ||
             !savedMachineData_.empty())) {
            stateRestored = replacement->restoreState(
                savedMachineParameters_,
                savedMachineData_
            );
        } else if (path != nativeStatePath_) {
            savedMachineParameters_.clear();
            savedMachineData_.clear();
            nativeStatePath_ = path;
        }

        if (!active_.load()) {
            deactivated = true;
        } else {
            if (!replacement->parameterSnapshot(parameterSnapshot)) {
                parameterSnapshotFailed = true;
            } else {
                if (machine_) {
                    retiredMachines_.push_back(std::move(machine_));
                }
                publishedGeneration =
                    machineGeneration_.fetch_add(
                        1,
                        std::memory_order_acq_rel
                    ) + 1;
                replacement->setGeneration(publishedGeneration);
                machine_ = std::move(replacement);
                machinePath_ = path;
                publishedMachine_.store(
                    machine_.get(),
                    std::memory_order_seq_cst
                );
                reclaimRetiredMachines();
            }
        }
    }
    if (deactivated) {
        sendMachineStatus(
            false,
            "The host deactivated the plugin before loading completed."
        );
        return false;
    }
    if (parameterSnapshotFailed) {
        sendMachineStatus(
            false,
            "The machine parameter table could not be read."
        );
        return false;
    }
    sendMachineStatus(
        true,
        stateRestored
            ? loadedName
            : loadedName + " (saved machine data could not be restored)"
    );
    sendMachineParameters(parameterSnapshot, publishedGeneration);
    return true;
}

void LoaderProcessor::sendMachineParameters(
    const std::vector<MachineParameterSnapshot>& parameters,
    uint64_t generation
) {
    std::vector<uint8_t> payload;
    auto append = [&payload](const void* data, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        payload.insert(payload.end(), bytes, bytes + size);
    };
    const uint32_t version = kParameterMessageVersion;
    const uint32_t count = static_cast<uint32_t>(
        std::min<size_t>(parameters.size(), kMaximumEditorParameters)
    );
    append(&version, sizeof(version)); append(&generation, sizeof(generation));
    append(&count, sizeof(count));
    for (uint32_t index = 0; index < count; ++index) {
        const auto& parameter = parameters[index];
        const uint32_t nameSize = static_cast<uint32_t>(
            std::min<size_t>(parameter.name.size(), 1024));
        const uint32_t descriptionSize = static_cast<uint32_t>(
            std::min<size_t>(parameter.description.size(), 1024));
        append(&nameSize, sizeof(nameSize)); append(parameter.name.data(), nameSize);
        append(&descriptionSize, sizeof(descriptionSize));
        append(parameter.description.data(), descriptionSize);
        append(&parameter.minimum, sizeof(parameter.minimum));
        append(&parameter.maximum, sizeof(parameter.maximum));
        append(&parameter.value, sizeof(parameter.value));
    }
    IMessage* message = allocateMessage();
    if (!message) return;
    message->setMessageID(kMachineParametersMessageId);
    if (message->getAttributes()->setBinary(
            kMachineParametersAttributeId, payload.data(),
            static_cast<uint32>(payload.size())) == kResultOk) {
        sendMessage(message);
    }
    message->release();
}

void LoaderProcessor::applyPendingTweaks(
    PsycleMachineLoader& machine
) noexcept {
    auto readIndex = tweakReadIndex_.load(std::memory_order_relaxed);
    const auto writeIndex = tweakWriteIndex_.load(std::memory_order_acquire);
    while (readIndex != writeIndex) {
        const auto& tweak =
            pendingTweaks_[readIndex % kTweakQueueCapacity];
        if (tweak.generation == machine.generation()) {
            machine.applyParameter(tweak.index, tweak.value);
        }
        ++readIndex;
    }
    tweakReadIndex_.store(readIndex, std::memory_order_release);
}

void LoaderProcessor::sendMachineStatus(
    bool success,
    const std::string& status
) {
    IMessage* message = allocateMessage();
    if (!message) {
        return;
    }

    message->setMessageID(kMachineStatusMessageId);
    auto* attributes = message->getAttributes();
    if (attributes) {
        std::string activePath;
        {
            std::lock_guard<std::mutex> lock(ownershipMutex_);
            activePath = machinePath_;
        }
        String statusString(status.c_str(), kCP_Utf8);
        String pathString(activePath.c_str(), kCP_Utf8);
        attributes->setInt(
            kMachineStatusSuccessAttributeId,
            success ? 1 : 0
        );
        attributes->setString(
            kMachineStatusTextAttributeId,
            statusString.text16()
        );
        attributes->setString(
            kMachinePathAttributeId,
            pathString.text16()
        );
    }
    sendMessage(message);
    message->release();
}

PsycleMachineLoader* LoaderProcessor::acquireMachineForAudio() noexcept {
    if (!active_.load(std::memory_order_seq_cst)) {
        return nullptr;
    }

    PsycleMachineLoader* machine = nullptr;
    do {
        machine = publishedMachine_.load(std::memory_order_seq_cst);
        audioHazard_.store(machine, std::memory_order_seq_cst);
    } while (
        machine != publishedMachine_.load(std::memory_order_seq_cst)
    );

    if (!active_.load(std::memory_order_seq_cst)) {
        audioHazard_.store(nullptr, std::memory_order_seq_cst);
        return nullptr;
    }
    return machine;
}

void LoaderProcessor::releaseMachineFromAudio() noexcept {
    audioHazard_.store(nullptr, std::memory_order_seq_cst);
}

void LoaderProcessor::reclaimRetiredMachines() {
    const auto* hazard = audioHazard_.load(std::memory_order_seq_cst);
    retiredMachines_.erase(
        std::remove_if(
            retiredMachines_.begin(),
            retiredMachines_.end(),
            [hazard](const auto& retired) {
                return retired.get() != hazard;
            }
        ),
        retiredMachines_.end()
    );
}

void LoaderProcessor::stopAndReleaseMachines() {
    active_.store(false, std::memory_order_seq_cst);
    publishedMachine_.store(nullptr, std::memory_order_seq_cst);
    while (audioHazard_.load(std::memory_order_seq_cst) != nullptr) {
        std::this_thread::yield();
    }

    std::lock_guard<std::mutex> lock(ownershipMutex_);
    machine_.reset();
    retiredMachines_.clear();
}

} // namespace psycle::loader