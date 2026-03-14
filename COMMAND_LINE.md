# Command-Line Arguments

```
zerospades [server_address] [protocol_version] [-h|--help] [-v|--version]
```

```sh
# Connect to a server
zerospades aos://example.com:32887

# Connect with a specific protocol version
zerospades aos://example.com:32887 0.75
zerospades aos://example.com:32887 v=0.76

# Print version and exit
zerospades --version

# Print usage and exit
zerospades --help
```

---

# Settings

Settings are read from `SPConfig.cfg` at startup. The file is located at:

| Platform | Path |
|----------|------|
| Linux | `~/.local/share/openspades/Resources/SPConfig.cfg` |
| macOS | `~/Library/Application Support/OpenSpades/Resources/SPConfig.cfg` |
| Windows | `%APPDATA%\OpenSpades\Resources\SPConfig.cfg` |

Each line follows the format `key: value`. Unknown keys are ignored.

---

## Renderer

```ini
r_renderer: gl         # gl (OpenGL, default) or sw (software)
r_videoWidth: 1920
r_videoHeight: 1080
r_fullscreen: 0
r_vsync: 1
r_fps: 0               # 0 = unlimited
r_scale: 1             # internal resolution scale
r_fov: 68
r_multisamples: 0
r_maxAnisotropy: 8

# Effects (0 = off, 1 = on)
r_bloom: 1
r_ssao: 1
r_hdr: 1
r_fxaa: 1
r_depthOfField: 0
r_modelShadows: 1
r_softParticles: 1
r_water: 2             # 0, 1, or 2

# Shadow maps
r_shadowMapSize: 2048
```

---

## Audio

```ini
s_audioDriver: openal  # openal (default) or null (silent)
s_volume: 100
s_openalDevice:        # empty = system default
```

---

## Gameplay & HUD

```ini
cg_playerName: Deuce
cg_fov: 68
cg_mouseSensitivity: 1
cg_zoomedMouseSensScale: 1
cg_invertMouseY: 0
cg_thirdperson: 0
cg_blood: 2            # 0, 1, or 2
cg_particles: 2        # 0, 1, or 2
cg_ragdoll: 1
```

---

## Key Bindings

```ini
cg_keyAttack: Mouse0
cg_keyAltAttack: Mouse1
cg_keyMoveForward: W
cg_keyMoveBackward: S
cg_keyMoveLeft: A
cg_keyMoveRight: D
cg_keyJump: Space
cg_keyCrouch: Control
cg_keySprint: Shift
cg_keyReload: R
cg_keyChat: T
cg_keyTeamChat: Y
cg_keyMap: M
cg_keyChangeWeapon: Q
cg_keySpade: 1
cg_keyBlock: 2
cg_keyWeapon: 3
cg_keyGrenade: 4
cg_keySneak: V
```

---

## Software Renderer

```ini
r_swNumThreads: 4
r_swUndersampling: 0
```

---

## Misc

```ini
core_locale:           # empty = system locale
core_jpegQuality: 95
cl_showStartupWindow: 1
```
