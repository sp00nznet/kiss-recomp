# Bring-up

What KISS Psycho Circus needed, in the order it turned up. Shogo (LithTech
1.0) was the starting point; [lithtech.md](lithtech.md) says what carried over.

## 1. The build and the modules

The US CD (`KISS Psycho Circus - The Nightmare Child (USA)`, a single
MODE2/2352 track) carries the game as a plain `Psycho\` folder; no installer
has to run. The last official US patches are on archive.org, in the Game
Patches and Fixes Archive (`video-game-patches_k`, letter K):

| Patch | File | Takes it to |
|---|---|---|
| 1.1 (build 110), Sept 2000 | `patch 1.1/US/kiss11us.zip` (`kpcptch11.exe`) | 1.10 |
| 1.10 to 1.13, Oct 2001 | `patch 1.10 to 1.13/US/kpc110us113.zip` (`kpcptch_US_110_to_113.exe`) | 1.13 |

Both are RTPatch (Pocket Soft) binary diffs, so the only way to apply them is
to run the patcher. Two things trip it up:

- Its name makes Windows' installer detection ask for elevation, and the
  launch fails silently without it. `__COMPAT_LAYER=RunAsInvoker` runs it as
  the user.
- It looks for the game through a registry key the CD install would have
  written, and without one it patches **its working directory**. Start it
  from inside the game folder.

1.13 replaces `client.exe`, `server.dll`, `cshell.dll`, `object.lto` and
adds `KPCPatch.rez`, `Dedicated.exe` and the Miles 3D providers.

The engine is `client.exe` (linked at `0x01CA0000`, no relocations); the
launcher `psycho.exe` only writes the command line. Every other Monolith or
Third Law module was linked at `0x10000000` (`cshell.dll` at `0x20000000`),
and **unlike Shogo they sit beside the exe, not inside a rez**:

| Module | Base here | Functions |
|---|---|---|
| `client.exe` | `0x01CA0000` | 4,857 |
| `server.dll` | `0x10000000` | 2,813 |
| `d3d.ren` | `0x10100000` | 1,481 |
| `cres.dll` | `0x10300000` | 100 |
| `object.lto` | `0x10400000` | 9,846 |
| `sres.dll` | `0x10600000` | 100 |
| `cdaudio.dll` | `0x10700000` | 821 |
| `cshell.dll` | `0x20000000` | 8,476 |

28,494 functions, 0 lift errors, cataloged by pcrecomp's own `rtti.py` and
`disasm32.py` (no IDA). `de_msg.dll`, `ima.dll` (Interactive Music),
`smackw32.dll` and `mss32.dll` are third-party and load natively;
`Dedicated.exe` and `psycho.exe` are not lifted.

## 2. The command line and the game folder as a tree

`psycho.exe` starts `CLIENT.EXE -windowtitle "The Nightmare Child" -rez
psycho.rez -rez KPCPatch.rez ++RenderDll d3d.ren`, plus the display mode.
With just that, the engine stops with "Error copying file cshell.dll": it
finds the game DLLs only through its resource trees, never on disk directly.
The host adds `-rez .`, the game folder itself as a tree.

## 3. DirectInput: the heap corruption that wasn't ours

The engine then died in `IDirectInput::EnumDevices` with heap corruption,
inside `dinput.dll`, after its twelfth callback. Bisected: with `client.exe`
running its own machine code (`--native client.exe`) it still crashed, and
a 20-line C program that only calls `DirectInputCreateA(0x700)` and
`EnumDevices(0, ...)` crashes the same way on the development machine. The
walk of DIDEVTYPE_DEVICE ("other" HID devices: RGB keyboards, headsets) is
what corrupts the heap; mice, keyboards and joysticks are fine. pcrecomp's
DirectInput shim now turns an all-types enumeration into those three.
(It patches the slot in dinput's own vtable. Swapping the object's vtable for
a copy crashed in dinput's `Release`, which tells its interfaces apart by
vtable pointer.)

## 4. The image that lost its tail

A minute in, the engine read its own IAT from freed memory (`MEM_FREE` from
`0x01D37000`). `client.exe`'s base, `0x01CA0000`, is where Windows puts heap
segments before `main()`. The loader found the range reserved, assumed the
reservation was its own and committed into it, and the heap's owner later
released it. pcrecomp's loader now commits only into a reservation that
starts at the image base and covers the whole image. The host holds the range
from process start (`native32_hold_image`), which means a 256 KB first-thread
stack and the lifted code on a 16 MB thread of its own.

## 5. Direct3D

`d3d.ren` finds no hardware device for DirectDraw surfaces here, and the mode
list comes back empty (the engine calls `LoadLibraryA("")` and quits).
`+RGBRast 1` selects Direct3D's RGB software rasterizer, as in Shogo, whose
span code runs from the heap (`native32_allow_heap_code`).

Windowed, the surfaces take the desktop's format and the renderer draws
16-bit only (half-width, speckled images on a 32-bit desktop), so the host
runs itself again under Windows' `16BITCOLOR` layer, as Shogo's does.

## 6. Headless

`--headless` (`native32_headless`) runs the host again on a private desktop
that is never shown: nothing reaches the real screens, over RDP or not. There
a GDI copy of the window is black, so `--shot` locks `d3d.ren`'s back buffer
(`[0x10266740]`) right after its windowed present (`0x1012267E`, called
through the renderer's function table at +0xA8), and `--record` pipes the
same frames to ffmpeg. `--key` only posts `WM_KEYDOWN`/`WM_KEYUP` there:
`SendInput` would type into the user's own session.

## 7. In the level: DirectInput with no keyboard

The menus read window messages, but the level's controls read the DirectInput
keyboard, and on the private desktop it sees nothing. pcrecomp's
`pcrecomp-ingame` branch gives native32 a synthetic DirectInput keyboard
(`native32_input_key`, both the state and the buffered `GetDeviceData`
events); under `--headless` each `--key` goes into it as a DIK scan code as
well as a posted message, held for `:MS` milliseconds (a tap of 250 ms moves
the player a step; a turn needs a second or more). Menu timing drifts with
machine load, so in-level times can be written `L<S>`, counted from the first
`LoadLibrary` of `object.lto`.

## Debugging aids

- `--heap-check`: every guest heap call validates every heap in the
  process first and names the lifted caller when one is broken.
- `--native MODULE`: run a module's original machine code instead of its lift
  (for bisecting a lift bug to a module).
- The crash report names the page state of a faulting address and scans the
  native stack for return addresses into loaded modules.
