# LithTech 1 to LithTech 2: what carried over from Shogo

This recomp started as a copy of the [Shogo recomp](https://github.com/sp00nznet/shogo-recomp)
(LithTech 1.0, 1998). KISS Psycho Circus is LithTech 2.x (2000). The pieces:

| Piece | Shogo (LithTech 1.0) | KISS (LithTech 2.x) | Carried over? |
|---|---|---|---|
| Module set | `Client.exe`, `server.dll`, `d3d.ren`, `soft.ren`, `CSHELL.DLL`, `CRES.DLL`, `OBJECT.LTO`, `SRES.DLL` | the same names (lower case), no `soft.ren`, plus Third Law's `cdaudio.dll` | yes, the list changes |
| Engine base | `0x00400000` | `0x01CA0000`, inside the range Windows fills with heaps before `main()` | no: needs `native32_hold_image` (bringup section 4) |
| Game DLLs | inside `SHOGOP3.REZ` | files beside `client.exe` | the `rebase.py` rez step is gone |
| REZ format | "RezMgr Version 1" | the same | yes: now pcrecomp `tools/formats/lithrez` |
| Loading game DLLs | copied from the rez to `%TEMP%`, matched by PE timestamp | loaded from the game folder through the `-rez .` tree | yes, the host's `LoadLibraryA` handles both |
| Command line | `-rez SHOGO.REZ ... ShogoP4.rez` | `-windowtitle ... -rez psycho.rez -rez KPCPatch.rez -rez . ++RenderDll d3d.ren` | rewritten |
| Catalog and lift (`tools/catalog.py`, `run_lift.py`) | rtti.py + disasm32.py, one gen dir per module | unchanged | yes, as is |
| Renderer | DirectX 6 (`IDirect3DDevice3`) | DirectX 7 era DirectDraw (`IDirectDrawSurface4/7`, `DDSURFACEDESC2`) | the hooks were rewritten at new VAs |
| `+RGBRast 1`, heap code | needed | needed | yes: `native32_allow_heap_code` in pcrecomp now |
| `D3DDP_DONOTCLIP` workaround | needed (wild span writes) | not needed so far | not yet |
| 16-bit relaunch (`16BITCOLOR`) | needed | needed | yes |
| Early `AIL_startup` | needed | not needed; behind `--early-miles` | kept as an option |
| DirectInput `HINSTANCE` shim | needed | needed | yes, in pcrecomp |
| DirectInput `DISCL_FOREGROUND` patch | patched in `Client.exe` | not needed headless (input is posted) | not yet |
| `EnumDevices` heap corruption | not seen (Shogo asks DirectInput 3) | crashed every run | new pcrecomp fix |
| Screenshots | back buffer after present | the same idea, new VAs | yes |
| Headless | none | `native32_headless` + back-buffer `--shot` / `--record` | new |

In short, the toolchain side (rez reader, rebase, catalog, lift, the loader
shims) carried over almost unchanged; what changed is everything tied to an
address or a DirectX version, which is all in `src/runtime/host.c`.
