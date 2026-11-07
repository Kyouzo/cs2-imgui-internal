# cs2internal

An internal DLL cheat for CS2, built with ImGui over a DirectX 11 kiero hook.

## Features

### ESP
| Option | Description |
|---|---|
| Cornered box | Draws a corner-style bounding box around players |
| Skeleton | Bone skeleton overlay |
| Snapline | Line from screen bottom to player feet |
| Head marker | Dot above the player's head |
| Position arrow | Off-screen directional arrow |
| Health bar | Side bar showing current HP |
| Health text | Numeric HP label |
| Name | Player name tag |
| Distance | Distance in units |
| 360 alert | Threat indicator for players outside the viewport |
| Spectator list | Lists players currently spectating you |
| Enemies only | Filter ESP to enemies only |
| Team colors | Color-code teammates vs enemies |

### Aimbot
| Option | Description |
|---|---|
| Enable aim assist | Master toggle |
| Visible check | Only aims at spotted (visible) enemies |
| Show FOV circle | Renders the aim FOV as a screen circle |
| Hold bind to aim | Aim assist is active only while the bind is held |
| Enemies only | Ignore teammates |
| Target bone | Head / Neck / Chest / Torso |
| Aim key | Bindable — press the button to capture any key or mouse button |
| FOV | Aim radius in degrees (0.5 – 30) |
| Smoothing | Mouse interpolation factor (1 – 20) |

### Crosshair
Custom software crosshair with length, gap, thickness, dot, and outline options.

## Keybinds

| Key | Action |
|---|---|
| `INSERT` | Toggle menu |
| `END` | Unload DLL |

## Building

Requires Visual Studio with the MSVC toolchain.

1. Open `zxynlxs.sln`
2. Select **Release x64**
3. Build — output is `Build/CS2.dll`

## Injecting

Inject `CS2.dll` into `cs2.exe` with any standard DLL injector.
