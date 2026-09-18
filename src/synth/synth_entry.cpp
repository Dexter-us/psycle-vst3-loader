#include "../loader/loader_processor.h"
#include "../loader/loader_controller.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

using namespace Steinberg;
using namespace Steinberg::Vst;
using psycle::loader::LoaderProcessor;
using psycle::loader::LoaderController;

static const FUID kSynthProcessorUID(0x1C7E8A64, 0x96A74DF8, 0xA7D8C42B, 0x5A3D1901);
static const FUID kSynthControllerUID(0x8D4E7A2B, 0x7D714A53, 0x9A1D4F20, 0xA1029B7F);

BEGIN_FACTORY_DEF("Dexter U.S.", "https://github.com/psycle-vst3-loader", "support@example.invalid")

DEF_CLASS2(
    INLINE_UID_FROM_FUID(kSynthProcessorUID),
    PClassInfo::kManyInstances,
    kVstAudioEffectClass,
    "Psycle Synth Loader",
    Vst::kDistributable,
    Vst::PlugType::kInstrumentSynth,
    "0.1.0",
    kVstVersionString,
    LoaderProcessor::createSynth
)

DEF_CLASS2(
    INLINE_UID_FROM_FUID(kSynthControllerUID),
    PClassInfo::kManyInstances,
    kVstComponentControllerClass,
    "Psycle Synth Loader Controller",
    0,
    "",
    "0.1.0",
    kVstVersionString,
    LoaderController::createInstance
)

END_FACTORY