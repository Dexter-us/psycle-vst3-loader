#pragma once

#include "psycle_machine_loader.h"

#include "public.sdk/source/vst/vstaudioeffect.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace psycle::loader {

enum class LoaderRole {
    Synth,
    Effect,
};

class LoaderProcessor final : public Steinberg::Vst::AudioEffect {
public:
    explicit LoaderProcessor(LoaderRole role);
    ~LoaderProcessor() override = default;

    static Steinberg::FUnknown* createSynth(void*);
    static Steinberg::FUnknown* createEffect(void*);

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setupProcessing(
        Steinberg::Vst::ProcessSetup& setup
    ) override;
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* inputs,
        Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs,
        Steinberg::int32 numOuts
    ) override;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(
        Steinberg::int32 symbolicSampleSize
    ) override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
    Steinberg::tresult PLUGIN_API notify(
        Steinberg::Vst::IMessage* message
    ) override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;

private:
    static constexpr Steinberg::Vst::ParamID kOutputGainParam = 0;

    LoaderRole role_;
    double sampleRate_ = 44100.0;
    uint32_t maxBlockSize_ = 0;
    std::atomic<float> outputGain_ {1.0f};
    std::string machinePath_;
    std::atomic_bool active_ {false};
    std::mutex ownershipMutex_;
    std::unique_ptr<PsycleMachineLoader> machine_;
    std::vector<std::unique_ptr<PsycleMachineLoader>> retiredMachines_;
    std::atomic<PsycleMachineLoader*> publishedMachine_ {nullptr};
    std::atomic<PsycleMachineLoader*> audioHazard_ {nullptr};
    std::string nativeStatePath_;
    std::vector<int32_t> savedMachineParameters_;
    std::vector<uint8_t> savedMachineData_;
    struct PendingTweak { uint64_t generation; int32_t index; int32_t value; };
    static constexpr uint32_t kTweakQueueCapacity = 1024;
    std::array<PendingTweak, kTweakQueueCapacity> pendingTweaks_ {};
    std::atomic<uint32_t> tweakWriteIndex_ {0};
    std::atomic<uint32_t> tweakReadIndex_ {0};
    std::atomic<uint64_t> machineGeneration_ {0};

    void loadSelectedMachine();
    bool loadMachinePath(const std::string& path);
    void sendMachineStatus(bool success, const std::string& status);
    void sendMachineParameters(
        const std::vector<MachineParameterSnapshot>& parameters,
        uint64_t generation
    );
    void applyPendingTweaks(PsycleMachineLoader& machine) noexcept;
    PsycleMachineLoader* acquireMachineForAudio() noexcept;
    void releaseMachineFromAudio() noexcept;
    void reclaimRetiredMachines();
    void stopAndReleaseMachines();
};

} // namespace psycle::loader