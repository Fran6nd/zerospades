# ZeroSpades ![Build status](https://github.com/siecvi/zerospades/actions/workflows/ci.yml/badge.svg) [![All releases downloads](https://img.shields.io/github/downloads/siecvi/zerospades/total.svg)](https://github.com/siecvi/zerospades/releases) [![Latest release](https://img.shields.io/github/release/siecvi/zerospades.svg)](https://github.com/siecvi/zerospades/releases)


![unknown](https://user-images.githubusercontent.com/25997662/166125363-3cdf237d-2154-4371-a44b-baea8a7abe5f.png)

[Download](https://github.com/siecvi/zerospades/releases/latest) — Community: [BuildAndShoot](https://buildandshoot.com) - [aloha.pk](https://aloha.pk)

## What is OpenSpades?
[OpenSpades](https://github.com/yvt/openspades) is a compatible client of [Ace Of Spades](https://en.wikipedia.org/wiki/Ace_of_Spades_(video_game)) 0.75.

* Can connect to a vanilla/[pyspades](https://code.google.com/archive/p/pyspades)/[PySnip](https://github.com/NateShoffner/PySnip)/[piqueserver](https://github.com/piqueserver/piqueserver)/[SpadeX](https://github.com/SpadesX/SpadesX) server.
* Uses OpenGL/AL for better experience.
* Open source, and cross platform.

## Ok but what the heck is ZeroSpades?
ZeroSpades is a fork of OpenSpades with extra features and many improvements/fixes.

Some of the most important changes are:

* In-Game HitTest Debugger (ty BR <3).
* Extended block color palette (ty Liza&Vier <3).
* Improved firstperson weapon animations (ty PTrooper <3).
* More thirdperson animations.
* Firstperson playermodels (torso&legs).
* Classic firstperson viewmodel
* Different playermodels depending on weapon class.
* Classic randomized dirt color.
* Dead player corpse & falling blocks physics.
* Client-side hit analyzer (showing hit player name, distance, and which body part it hit).
* Player names while on spectator mode.
* Teammate names on minimap.
* Damage dealt to players are now shown as floating text.
* Player statistics such as kill/death ratio, kill streak, number of melee/grenade kills, and blocks placed.
* Customizable HUD position and color.
* Customizable crosshair & scope.
* Same fog tint/color as the classic client (openspades applies a [bluish tint](https://github.com/yvt/openspades/blob/v0.1.3/Resources/Shaders/Fog.vs#L27) to the fog).

-- And a lot of other things that I forgot to mention here.

## How to Build

### Prerequisites (all platforms)
```
git clone --recurse-submodules https://github.com/Fran6nd/zerospades
cd zerospades
vcpkg/bootstrap-vcpkg.sh    # macOS/Linux
vcpkg\bootstrap-vcpkg.bat   # Windows
```
vcpkg dependencies are installed automatically during cmake configure.

---

### macOS — Apple Silicon
```bash
brew install molten-vk vulkan-headers glslang
mkdir build && cd build
cmake .. -G Ninja \
  -D CMAKE_BUILD_TYPE=Release \
  -D CMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake \
  -D VCPKG_TARGET_TRIPLET=arm64-osx \
  -D VCPKG_OVERLAY_TRIPLETS=../cmake/triplets \
  -D CMAKE_OSX_ARCHITECTURES=arm64 \
  -D CMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
  -D CMAKE_CXX_STANDARD=17
cmake --build . --parallel
```

### macOS — Intel
Same as above but use:
```
  -D VCPKG_TARGET_TRIPLET=x64-osx \
  -D CMAKE_OSX_ARCHITECTURES=x86_64 \
```

---

### Windows (x64)
Install the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home), then from a Visual Studio command prompt:
```bat
mkdir build && cd build
cmake .. -A x64 ^
  -D CMAKE_BUILD_TYPE=Release ^
  -D CMAKE_TOOLCHAIN_FILE=..\vcpkg\scripts\buildsystems\vcpkg.cmake ^
  -D VCPKG_TARGET_TRIPLET=x64-windows-static ^
  -D CMAKE_CXX_STANDARD=17
cmake --build . --config Release --parallel
```
For x86, replace `x64` with `Win32` and `x64-windows-static` with `x86-windows-static`.

---

### Linux — Ubuntu / Debian
```bash
sudo apt-get install -y build-essential cmake ninja-build \
  libsdl2-dev libsdl2-image-dev libglew-dev libfreetype6-dev \
  libcurl4-openssl-dev libogg-dev libopus-dev libopusfile-dev zlib1g-dev
# Install Vulkan SDK from https://vulkan.lunarg.com/sdk/home
mkdir build && cd build
cmake .. -G Ninja \
  -D CMAKE_BUILD_TYPE=Release \
  -D CMAKE_CXX_STANDARD=17
cmake --build . --parallel
```

### Linux — Nix
```bash
nix build
```

## Troubleshooting
For troubleshooting and common problems see [TROUBLESHOOTING](TROUBLESHOOTING.md).

## Licensing
Please see the file named LICENSE.

Note that other assets including sounds and models are not open source.
