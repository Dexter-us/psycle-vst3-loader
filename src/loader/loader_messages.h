#pragma once

namespace psycle::loader {

inline constexpr char kMachinePathMessageId[] = "PsycleMachinePath";
inline constexpr char kMachineStatusMessageId[] = "PsycleMachineStatus";
inline constexpr char kMachinePathAttributeId[] = "Path";
inline constexpr char kMachineStatusSuccessAttributeId[] = "Success";
inline constexpr char kMachineStatusTextAttributeId[] = "Status";
inline constexpr char kMachineParametersMessageId[] = "PsycleMachineParameters";
inline constexpr char kMachineParametersAttributeId[] = "Parameters";
inline constexpr char kMachineTweakMessageId[] = "PsycleMachineTweak";
inline constexpr char kMachineTweakAttributeId[] = "Tweak";
inline constexpr uint32_t kMaximumEditorParameters = 256;

} // namespace psycle::loader