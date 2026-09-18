# Psycle compatibility notes

The first supported target is the native Psycle 1.12 x64 ABI. Standard native
machine DLLs are loaded directly by the VST3 wrapper through `GetInfo`,
`CreateMachine`, and `DeleteMachine`.

This directory is reserved for compatibility shims if a future Psycle build
changes the ABI. Keep any such shim compiled with the same architecture as the
machine collection it wraps.