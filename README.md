# CS2 Internal

An internal (injected DLL) overlay for Counter-Strike 2, built with Dear ImGui over a DirectX 11 [kiero](https://github.com/Rebzzel/kiero) / MinHook hook. Game offsets are resolved at runtime from signatures, so it survives most game updates without a rebuild.

> **Disclaimer:** For educational and research purposes only. Using this against VAC-secured servers will get your account banned. You are responsible for how you use it.

## Features

### ESP
| Option | Description |
|---|---|
| Cornered box | Corner-style bounding box |
| Skeleton | Bone skeleton overlay |
| Snapline | Line from screen bottom to player feet |
| Head marker | Dot on the player's head |
| Position arrow | Off-screen directional arrow |
| Health bar / text | Side bar and numeric HP |
| Name / Distance | Name tag and distance in units |
| 360 alert | Indicator for players outside the viewport |
| Spectator list | Players currently spectating you |
| Enemies only / Team colors | Filtering and teammate vs. enemy coloring |

### Aim assist
| Option | Description |
|---|---|
| Visible / smoke / flash checks | Skip targets you can't see, or when you're flashed |
| Show FOV circle | Draws the aim FOV on screen |
| Hold bind to aim | Active only while the bind is held; any key or mouse button can be bound |
| Target bone | Head / Neck / Chest / Torso |
| FOV, tight FOV when scoped | Aim radius in degrees (0.5 - 30) |
| Smoothing | Linear or exponential, factor 1 - 20 |
| RCS | Recoil control with adjustable scale |
| Humanize | Adds natural-looking aim variance |
| Enemies only | Ignore teammates |

### Crosshair
Software crosshair with length, gap, thickness, center dot and outline options.

### Skin changer
- Per-weapon skin, wear, StatTrak and Souvenir settings, with separate T / CT loadouts (or apply to both)
- Knife model, glove and agent selection
- Skins are written to the in-game inventory
- Save / load / reset configs

## Keybinds

| Key | Action |
|---|---|
| `INSERT` | Toggle menu |
| `END` | Unload DLL |

## Building

Requires Visual Studio with the MSVC toolchain (C++20).

1. Open `zxynlxs.sln`
2. Select **Release | x64**
3. Build. The output is `Build/CS2.dll`

## Usage

Inject `CS2.dll` into `cs2.exe` with any standard DLL injector. Launch the game with `-insecure` if you only want to test offline.

## Third-party libraries

- [Dear ImGui](https://github.com/ocornut/imgui)
- [kiero](https://github.com/Rebzzel/kiero)
- [MinHook](https://github.com/TsudaKageyu/minhook)
- [nlohmann/json](https://github.com/nlohmann/json)
