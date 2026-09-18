# Psycle VST3 Loader

Windows-first VST3 wrappers for Psycle native machines:

- **Psycle Synth Loader** — presents a Psycle generator as a VST3 instrument.
- **Psycle Effect Loader** — presents a Psycle effect as a VST3 audio effect.

Both plugins use the same real-time-safe loader core. The VST3 entry points
are deliberately separate, so hosts can classify and scan them correctly.

## Psycle 1.12 compatibility

The loader targets the Psycle 1.12 native machine ABI for 64-bit Windows. It
loads the standard Psycle exports:

```text
GetInfo
CreateMachine
DeleteMachine
```

The ABI declarations are isolated in
`src/loader/psycle_native_interface.h`. The loader validates the machine API
version and rejects generators in the effect plugin or effects in the synth
plugin.

## Build on Windows

Install Visual Studio 2022 with the C++ desktop workload and CMake. From a
Developer PowerShell:

```powershell
cmake -S psycle-vst3-loader -B psycle-vst3-loader/build `
  -A x64 `
  -DPSYCLE_FETCH_VST3_SDK=ON

cmake --build psycle-vst3-loader/build --config Release
```

The two VST3 bundles will be under the build output directory. Copy them to
the user's VST3 folder, commonly:

```text
C:\Program Files\Common Files\VST3\
```

## Selecting a machine

Open the plugin editor and select **Browse...** to choose a Psycle 1.12 x64
native machine DLL. The selected path is stored in the VST3 component state and
restored with the host project. The editor reports the loaded machine name or
the specific reason a DLL was rejected. A failed selection leaves the previous
working machine active.

The component state also stores the native machine's parameter values and
bounded `GetData` chunk. These are restored through `ParameterTweak` and
`PutData` after the machine initializes.

For unattended testing, the wrapper also reads `PSYCLE_MACHINE_PATH` when the
plugin is initialized.

## Current scope

This first build establishes the two VST3 identities, audio/MIDI-compatible
processor shells, state serialization, output gain, direct Psycle 1.12 x64
DLL loading, API validation, machine processing, and a Windows machine chooser
inside the plugin editor.

## Windows verification

The Steinberg validator can verify the VST3 component contract on other
platforms, but it cannot execute the Win32 editor or Psycle's Windows C++ ABI.
Before release, build with Visual Studio 2022 x64 and test known Psycle 1.12 x64
generator and effect DLLs in a Windows VST3 host.