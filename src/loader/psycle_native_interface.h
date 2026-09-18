#pragma once

// ABI declarations mirrored from Psycle 1.12's
// psycle-plugins/src/psycle/plugin_interface.hpp.
//
// Keep this header dependency-free. The native Psycle DLLs expose these
// classes across their module boundary, so the virtual method order must stay
// aligned with the supported Psycle revision.

#include <cstdint>

namespace psycle::plugin_interface {

constexpr unsigned short MI_VERSION = 0x0013;
constexpr int NOTE_MAX = 119;

class CMachineParameter {
public:
    char const* Name;
    char const* Description;
    int MinValue;
    int MaxValue;
    int Flags;
    int DefValue;
};

class CMachineInfo {
public:
    static constexpr int EFFECT = 0;
    static constexpr int GENERATOR = 3;

    unsigned short APIVersion;
    unsigned short PlugVersion;
    int Flags;
    int numParameters;
    CMachineParameter const* const* Parameters;
    char const* Name;
    char const* ShortName;
    char const* Author;
    char const* Command;
    int numCols;
};

class CFxCallback {
public:
    virtual void MessBox(char const*, char const*, unsigned int) const = 0;
    virtual int CallbackFunc(int, int, int, void*) = 0;
    virtual float* unused0(int, int) = 0;
    virtual float* unused1(int, int) = 0;
    virtual int GetTickLength() const = 0;
    virtual int GetSamplingRate() const = 0;
    virtual int GetBPM() const = 0;
    virtual int GetTPB() const = 0;
    virtual ~CFxCallback() noexcept = default;
    virtual bool FileBox(bool, char[], char[]) = 0;
};

class CMachineInterface {
public:
    virtual ~CMachineInterface() = default;
    virtual void Init() {}
    virtual void SequencerTick() {}
    virtual void ParameterTweak(int, int) {}
    virtual void Work(float*, float*, int, int) {}
    virtual void Stop() {}
    virtual void PutData(void*) {}
    virtual void GetData(void*) {}
    virtual int GetDataSize() { return 0; }
    virtual void Command() {}
    virtual void unused0(int) {}
    virtual bool unused1(int) const { return false; }
    virtual void MidiEvent(int, int, int) {}
    virtual void unused2(unsigned int) {}
    virtual bool DescribeValue(char*, int, int) { return false; }
    virtual bool HostEvent(int, int, float) { return false; }
    virtual void SeqTick(int, int, int, int, int) {}
    virtual void unused3() {}

    int* Vals = nullptr;
    mutable CFxCallback* pCB = nullptr;
};

#if defined(_WIN32)
#define PSYCLE_NATIVE_CALL __cdecl
#else
#define PSYCLE_NATIVE_CALL
#endif

using GetInfoFn =
    CMachineInfo const* (PSYCLE_NATIVE_CALL*)();
using CreateMachineFn =
    CMachineInterface* (PSYCLE_NATIVE_CALL*)();
using DeleteMachineFn =
    void (PSYCLE_NATIVE_CALL*)(CMachineInterface&);

#undef PSYCLE_NATIVE_CALL

} // namespace psycle::plugin_interface