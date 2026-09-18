#include "../loader/loader_processor.h"
#include "../loader/loader_controller.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

using namespace Steinberg;
using namespace Steinberg::Vst;
using psycle::loader::LoaderProcessor;
using psycle::loader::LoaderController;

static const FUID kEffectProcessorUID(0xC2E6A5D1, 0xF74B4D2E, 0xB3B8A9C0, 0x1F45D672);
static const FUID kEffectControllerUID(0x8D4E7A2B, 0x7D714A53, 0x9A1D4F20, 0xA1029B7F);

BEGIN_FACTORY_DEF("Dexter U.S.", "https://github.com/psycle-vst3-loader", "support@example.invalid")

DEF_CLASS2(
    INLINE_UID_FROM_FUID(kEffectProcessorUID),
    PClassInfo::kManyInstances,
    kVstAudioEffectClass,
    "Psycle Effect Loader",
    Vst::kDistributable,
    Vst::PlugType::kFx,
    "0.1.0",
    kVstVersionString,
    LoaderProcessor::createEffect
)

DEF_CLASS2(
    INLINE_UID_FROM_FUID(kEffectControllerUID),
    PClassInfo::kManyInstances,
    kVstComponentControllerClass,
    "Psycle Effect Loader Controller",
    0,
    "",
    "0.1.0",
    kVstVersionString,
    LoaderController::createInstance
)

END_FACTORY