// Internal protocol regression tests. No native DLL is loaded on Linux.
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <cassert>
#include <iostream>
#define private public
#include "loader/loader_processor.h"
#include "loader/loader_controller.h"
#undef private
#include "loader/loader_messages.h"

using namespace psycle::loader;
using namespace Steinberg;
using namespace Steinberg::Vst;

int main() {
    HostApplication host;
    LoaderProcessor processor(LoaderRole::Synth);
    LoaderController controller;
    assert(processor.initialize(&host) == kResultOk);
    assert(controller.initialize(&host) == kResultOk);
    assert(processor.connect(&controller) == kResultOk);
    assert(controller.connect(&processor) == kResultOk);
    processor.machinePath_ = "test-shakers";
    processor.machineGeneration_ = 2;
    processor.editorSnapshotReady_ = true;
    processor.editorMachineName_ = "Test Shakers";
    processor.editorParameters_ = {{"Volume", "Volume", 0, 32767, 32767}};
    // A controller with complete but obsolete controls must still refresh.
    controller.machineGeneration_ = 1;
    controller.waitingForParameters_ = false;
    controller.refreshMachineState();
    assert(controller.machineGeneration_ == 2);
    assert(controller.parameters_.size() == 1);
    // Inactive edits must become the parameters restored on next activation.
    controller.tweakMachineParameter(0, 0);
    assert(processor.savedMachineParameters_.at(0) == 0);
    assert(processor.editorParameters_.at(0).value == 0);
    // Active edits must reach the audio queue, bounded to the native range.
    processor.active_ = true;
    controller.tweakMachineParameter(0, 99999);
    assert(processor.tweakWriteIndex_ == 1);
    assert(processor.pendingTweaks_[0].value == 32767);
    assert(processor.pendingTweaks_[0].generation == 2);
    // Rejected stale edits must schedule recovery, not silently succeed.
    controller.machineGeneration_ = 1;
    controller.tweakMachineParameter(0, 0);
    assert(controller.waitingForParameters_);
    assert(processor.tweakWriteIndex_ == 1);
    controller.refreshMachineState();
    assert(controller.machineGeneration_ == 2);
    assert(!controller.waitingForParameters_);
    // A periodic check with the same generation must not reset edited controls.
    controller.parameters_[0].value = 123;
    controller.refreshMachineState();
    assert(controller.parameters_[0].value == 123);
    processor.active_ = false;
    controller.disconnect(&processor);
    processor.disconnect(&controller);
    controller.terminate();
    processor.terminate();
    std::cout << "Parameter delivery regression checks passed\n";
}