# Sheep, Dog 'n' Wolf: Rewooled

An enhanced edition of the 2001 PC game *Sheep, Dog 'n' Wolf* (released in North America as *Sheep Raider*), PAL version, to modern 64-bit systems. The game's code is decompiled C++; the Windows-only layers it was written against (DirectDraw, Direct3D 7, DirectInput, DirectSound, the registry) are replaced by SDL3:

- **Rendering:** SDL3's GPU API - Vulkan, Direct3D 12 or Metal - behind a small renderer interface (`src/render/`) that reproduces what the game asked of Direct3D 7.
- **Sound:** a software mixer over SDL3 audio playing the game's DirectSound buffers (`src/platform/`).
- **Input and window:** SDL3's keyboard, mouse, controllers and window.
- **Settings and saves:** files in the user's data folder instead of the registry.

It builds for **Windows (MSVC, x64)**, **macOS (Apple Clang, arm64)** and **Linux (Clang, x86_64 or arm64)**.

For Linux users, it is also possible to cross-compile for Windows 64-bit using MinGW-w64 (Clang) via the provided `build_win64.sh` script. This requires the `mingw-w64-gcc` package (on Arch Linux). Note that the resulting `.exe` requires the MinGW runtime DLLs (e.g., `libstdc++-6.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll`) to be present in the game folder to run.

You need your own copy of the game: its data is read at run time and is not part of this repository.

**How it was made:** the decompilation was done with Claude and Codex coordinating via an MCP mailbox server - reading the machine code, naming, writing the source. In-game testing and technical knowledge about the game's inner workings came from the Sheep Raider community.
## What is here

| Path | What it holds |
|---|---|
| `src/app/`, `src/engine/`, `src/game/`, `src/objects/`, `src/fx/` | The game: application shell, engine, the player characters and the level objects |
| `src/render/` | The renderer interface and its SDL3 GPU backend |
| `src/launcher/` | `SheepLauncher`: picks the renderer, resolution, full screen and controller (Dear ImGui), then starts the game with them as command-line flags. On macOS it is built as the app bundle `Rewooled.app`, with the game inside it and the game's data next to it |
| `res/` | the app icon (`icon.png`, a square PNG) and the macOS bundle's `Info.plist` template |
| `src/platform/` | SDL3 window, input, audio, save storage, and the small Windows compatibility layer the game code still calls |
| `src/sdk/` | Declarations of the Windows, DirectX and C runtime names the game code uses |
| `src/include/` | Shared headers: types, enums and the classes. `scenaric_props.h` is generated from your copy of the game (`tools/scenaric_to_c.py`) |
| `src/jpeg/` | The Independent JPEG Group's library, release 6, unmodified, under IJG's own licence |
| `tools/` | Build helpers and level-file readers ([TOOLS.md](TOOLS.md)) |
| `docs/` | Notes on the engine: conventions, the game's files, the frame loop, the object system, the input system |



## Credits

- **uhwot**, for [sdw_re_stuff](https://github.com/uhwot/sdw_re_stuff) (MIT): the research into the game's asset formats (WAR, DAV, SND and others), with an exporter and ImHex patterns.
- **The whole Sheep Raider Discord community**, for helping crack this game open.

## Licence

TBD