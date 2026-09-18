#include "loader_state.h"

#include "base/source/fstreamer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace psycle::loader {

using namespace Steinberg;

namespace {

constexpr uint32 kStateMagic = 0x32534C50; // "PLS2"
constexpr uint32 kStateVersion = 1;
constexpr uint32 kMaximumPathBytes = 32 * 1024;
constexpr uint32 kMaximumParameters = 64 * 1024;
constexpr uint32 kMaximumMachineDataBytes = 16 * 1024 * 1024;

bool readBytes(
    IBStreamer& streamer,
    std::vector<uint8_t>& destination,
    uint32 maximumSize
) {
    uint32 size = 0;
    if (!streamer.readInt32u(size) || size > maximumSize) {
        return false;
    }
    destination.resize(size);
    return size == 0 ||
        streamer.readRaw(destination.data(), size) == static_cast<TSize>(size);
}

bool writeBytes(IBStreamer& streamer, const std::vector<uint8_t>& source) {
    if (source.size() > std::numeric_limits<uint32>::max() ||
        !streamer.writeInt32u(static_cast<uint32>(source.size()))) {
        return false;
    }
    return source.empty() ||
        streamer.writeRaw(source.data(), source.size()) ==
            static_cast<TSize>(source.size());
}

bool readCurrentState(IBStreamer& streamer, LoaderState& state) {
    uint32 version = 0;
    if (!streamer.readInt32u(version) || version != kStateVersion ||
        !streamer.readFloat(state.outputGain)) {
        return false;
    }
    if (!std::isfinite(state.outputGain)) {
        return false;
    }

    uint32 pathSize = 0;
    if (!streamer.readInt32u(pathSize) || pathSize > kMaximumPathBytes) {
        return false;
    }
    state.machinePath.resize(pathSize);
    if (pathSize > 0 &&
        streamer.readRaw(state.machinePath.data(), pathSize) !=
            static_cast<TSize>(pathSize)) {
        return false;
    }

    uint32 parameterCount = 0;
    if (!streamer.readInt32u(parameterCount) ||
        parameterCount > kMaximumParameters) {
        return false;
    }
    state.machineParameters.resize(parameterCount);
    for (auto& value : state.machineParameters) {
        if (!streamer.readInt32(value)) {
            return false;
        }
    }

    return readBytes(
        streamer,
        state.machineData,
        kMaximumMachineDataBytes
    );
}

bool readLegacyState(IBStreamer& streamer, LoaderState& state) {
    if (streamer.seek(0, kSeekSet) < 0) {
        return false;
    }

    double ignoredSampleRate = 0.0;
    if (!streamer.readDouble(ignoredSampleRate) ||
        !streamer.readFloat(state.outputGain)) {
        return false;
    }
    if (!std::isfinite(state.outputGain)) {
        return false;
    }

    int32 pathSize = 0;
    if (!streamer.readInt32(pathSize) ||
        pathSize < 0 ||
        static_cast<uint32>(pathSize) > kMaximumPathBytes) {
        return false;
    }
    if (pathSize == 0) {
        state.machinePath.clear();
        return true;
    }

    std::vector<char8> path(static_cast<size_t>(pathSize));
    if (streamer.readRaw(path.data(), pathSize) !=
        static_cast<TSize>(pathSize)) {
        return false;
    }
    const auto end = std::find(path.begin(), path.end(), '\0');
    state.machinePath.assign(path.begin(), end);
    return true;
}

} // namespace

bool readLoaderState(IBStream* stream, LoaderState& state) {
    if (!stream) {
        return false;
    }

    state = {};
    IBStreamer streamer(stream, kLittleEndian);
    uint32 magic = 0;
    if (!streamer.readInt32u(magic)) {
        return false;
    }
    return magic == kStateMagic
        ? readCurrentState(streamer, state)
        : readLegacyState(streamer, state);
}

bool writeLoaderState(IBStream* stream, const LoaderState& state) {
    if (!stream ||
        !std::isfinite(state.outputGain) ||
        state.machinePath.size() > kMaximumPathBytes ||
        state.machineParameters.size() > kMaximumParameters ||
        state.machineData.size() > kMaximumMachineDataBytes) {
        return false;
    }

    IBStreamer streamer(stream, kLittleEndian);
    if (!streamer.writeInt32u(kStateMagic) ||
        !streamer.writeInt32u(kStateVersion) ||
        !streamer.writeFloat(state.outputGain) ||
        !streamer.writeInt32u(
            static_cast<uint32>(state.machinePath.size())
        ) ||
        (!state.machinePath.empty() &&
         streamer.writeRaw(
             state.machinePath.data(),
             state.machinePath.size()
         ) != static_cast<TSize>(state.machinePath.size())) ||
        !streamer.writeInt32u(
            static_cast<uint32>(state.machineParameters.size())
        )) {
        return false;
    }

    for (const auto value : state.machineParameters) {
        if (!streamer.writeInt32(value)) {
            return false;
        }
    }
    return writeBytes(streamer, state.machineData);
}

} // namespace psycle::loader