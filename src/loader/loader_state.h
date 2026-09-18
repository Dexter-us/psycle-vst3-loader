#pragma once

#include "pluginterfaces/base/ibstream.h"

#include <cstdint>
#include <string>
#include <vector>

namespace psycle::loader {

struct LoaderState {
    float outputGain = 1.0f;
    std::string machinePath;
    std::vector<int32_t> machineParameters;
    std::vector<uint8_t> machineData;
};

bool readLoaderState(Steinberg::IBStream* stream, LoaderState& state);
bool writeLoaderState(Steinberg::IBStream* stream, const LoaderState& state);

} // namespace psycle::loader