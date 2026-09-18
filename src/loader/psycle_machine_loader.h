#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace psycle::loader {

struct MachineEvent {
    uint32_t sampleOffset;
    uint8_t type; // 0 = note-on, 1 = note-off
    uint8_t channel;
    uint8_t note;
    float velocity;
    float tuning;
};

class PsycleMachineLoader final {
public:
    PsycleMachineLoader();
    ~PsycleMachineLoader();

    PsycleMachineLoader(const PsycleMachineLoader&) = delete;
    PsycleMachineLoader& operator=(const PsycleMachineLoader&) = delete;

    bool load(const std::string& path, bool effectMode, double sampleRate, uint32_t maxBlockSize);
    void unload();
    bool isLoaded() const noexcept;
    const std::string& loadedPath() const noexcept;
    const std::string& loadedName() const noexcept;
    const std::string& lastError() const noexcept;
    bool captureState(
        std::vector<int32_t>& parameters,
        std::vector<uint8_t>& data
    );
    bool restoreState(
        const std::vector<int32_t>& parameters,
        const std::vector<uint8_t>& data
    );

    void process(
        const float* const* inputs,
        float* const* outputs,
        uint32_t channels,
        uint32_t frames,
        const MachineEvent* events,
        uint32_t eventCount
    ) noexcept;

private:
    struct Module;
    class HostCallbacks;
    std::unique_ptr<Module> module_;
    std::unique_ptr<HostCallbacks> callbacks_;
    std::string loadedPath_;
    std::string loadedName_;
    std::string lastError_;
};

} // namespace psycle::loader