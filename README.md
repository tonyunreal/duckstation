# DuckStation - PlayStation 1, aka. PSX Emulator
[Latest News](#latest-news) | [Features](#features) | [Screenshots](#screenshots) | [Downloading and Running](#downloading-and-running) | [Building](#building) | [Disclaimers](#disclaimers)

**Discord Server:** https://discord.gg/Buktv3t

**Latest Windows, Linux (AppImage), Mac, Android** https://github.com/stenzek/duckstation/releases/tag/latest

**Available on Google Play:** https://play.google.com/store/apps/details?id=com.github.stenzek.duckstation&hl=en_AU&gl=US

**Game Compatibility List:** https://docs.google.com/spreadsheets/d/1H66MxViRjjE5f8hOl5RQmF5woS1murio2dsLn14kEqo/edit?usp=sharing

**Wiki:** https://www.duckstation.org/wiki/

DuckStation is an simulator/emulator of the Sony PlayStation(TM) console, focusing on playability, speed, and long-term maintainability. The goal is to be as accurate as possible while maintaining performance suitable for low-end devices. "Hack" options are discouraged, the default configuration should support all playable games with only some of the enhancements having compatibility issues.

A "BIOS" ROM image is required to to start the emulator and to play games. You can use an image from any hardware version or region, although mismatching game regions and BIOS regions may have compatibility issues. A ROM image is not provided with the emulator for legal reasons, you should dump this from your own console using Caetla or other means.

## Latest News
Older entries are available at https://github.com/stenzek/duckstation/blob/master/NEWS.md

- 2021/01/10: Option to sync to host refresh rate added (enabled by default). This will give the smoothest animation possible with zero duped frames, at the cost of running the game <1% faster. Users with variable refresh rate (GSync/FreeSync) displays will want to disable the option.
- 2021/01/10: Audio resampling added when fast forwarding to fixed speeds. Instead of crackling audio, you'll now get pitch altered audio.
- 2021/01/03: Per game settings and game properties added to Android version.
- 2020/12/30: Box and Adaptive downsampling modes added. Adaptive downsampling will smooth 2D backgrounds but attempt to preserve 3D geometry via pixel similarity (only supported in D3D11/Vulkan). Box is a simple average filter which will downsample to native resolution.
- 2020/12/30: Hotkey binding added to Android version. You can now bind hotkeys such as fast forward, save state, etc to controller buttons. The ability to bind multi-button combinations will be added in the future.
- 2020/12/29: Controller mapping/binding added for Android version. By default mappings will be clear and you will have to set them, you can do this from `Settings -> Controllers -> Controller Mapping`. Profiles can be saved and loaded as well.
- 2020/12/29: Dark theme added for Android. By default it will follow your system theme (Android 10+), but can be overridden in settings.
- 2020/12/29: DirectInput/DInput controller interface added for Windows. You can use this if you are having difficulties with SDL. Vibration is not supported yet.
- 2020/12/25: Partial texture replacement support added. For now, this is only applicable to a small number of games which upload backgrounds to video RAM every frame. Dumping and replacement options are available in `Advanced Settings`.
- 2020/12/22: PGXP Depth Buffer enhancement added. This enhancement can eliminate "polygon fighting" in games, by giving the PS1 the depth buffer it never had. Compatibility is rather low at the moment, but for the games it does work in, it works very well. The depth buffer will be made available to postprocessing shaders in the future, enabling  effects such as SSAO.
- 2020/12/21: DuckStation now has two releases - Development and Preview. New features will appear in Preview first, and make their way to the Development release a few days later. To switch to preview, update to the latest development build (older builds will update to development), change the channel from `latest` to `preview` in general settings, and click `Check for Updates`.
- 2020/12/16: Integrated CPU debugger added in Qt frontend.
- 2020/12/13: Button layout for the touchscreen controller in the Android version can now be customized.
- 2020/12/10: Translation support added for Android version. Currently Brazillian Portuguese, Italian, and Dutch are available.
- 2020/11/27: Cover support added for game list in Android version. Procedure is the same as the desktop version, except you should place cover images in `<storage>/duckstation/covers` (see [Adding Game Covers](https://github.com/stenzek/duckstation/wiki/Adding-Game-Covers)).
- 2020/11/27: Disc database is shipped with desktop and Android versions courtesy of redump.org. This will provide titles for games on Android, where it was not possible previously.
- 2020/11/27: SDL game controller database is included with desktop versions courtesy of https://github.com/gabomdq/SDL_GameControllerDB.
- 2020/11/21: OpenGL ES 2.0 host display support added. You cannot use the hardware renderer with GLES2, it still requires GLES3, but GLES2 GPUs can now use the software renderer.
- 2020/11/21: Threaded renderer for software renderer added. Can result in a significant speed boost depending on the game.
- 2020/11/21: AArch32/armv7 recompiler added. Android and Linux builds will follow after further testing, but for now you can build it yourself.
- 2020/11/18: Window size (resize window to Nx content resolution) added to Qt and SDL frontends.
- 2020/11/10: Widescreen hack now renders in the display aspect ratio instead of always 16:9.
- 2020/11/01: Exclusive fullscreen option added for Windows D3D11 users. Enjoy buttery smooth PAL games.

## Features

DuckStation features a fully-featured frontend built using Qt (pictured), as well as a simplified frontend based on SDL and Dear ImGui. An Android version has been started, but is not yet feature complete.

<p align="center">
  <img src="https://raw.githubusercontent.com/stenzek/duckstation/md-images/main-qt.png" alt="Main Window Screenshot" />
</p>

Other features include:

 - CPU Recompiler/JIT (x86-64, armv7/AArch32 and AArch64)
 - Hardware (D3D11, OpenGL, Vulkan) and software rendering
 - Upscaling, texture filtering, and true colour (24-bit) in hardware renderers
 - PGXP for geometry precision, texture correction, and depth buffer emulation
 - Adaptive downsampling filter
 - Post processing shader chains
 - "Fast boot" for skipping BIOS splash/intro
 - Save state support
 - Windows, Linux, **highly experimental** macOS support
 - Supports bin/cue images, raw bin/img files, and MAME CHD formats.
 - Direct booting of homebrew executables
 - Direct loading of Portable Sound Format (psf) files
 - Digital and analog controllers for input (rumble is forwarded to host)
 - Namco GunCon lightgun support (simulated with mouse)
 - NeGcon support
 - Qt and SDL frontends for desktop
 - Automatic updates for Windows builds
 - Automatic content scanning - game titles/regions are provided by redump.org
 - Optional automatic switching of memory cards for each game
 - Supports loading cheats from libretro or PCSXR format lists
 - Memory card editor and save importer
 - Emulated CPU overclocking
 - Integrated and remote debugging

## System Requirements
 - A CPU faster than a potato. But it needs to be x86_64, AArch32/armv7, or AArch64/ARMv8, otherwise you won't get a recompiler and it'll be slow.
 - For the hardware renderers, a GPU capable of OpenGL 3.1/OpenGL ES 3.0/Direct3D 11 Feature Level 10.0 (or Vulkan 1.0) and above. So, basically anything made in the last 10 years or so.
 - SDL, XInput or DInput compatible game controller (e.g. XB360/XBOne). DualShock 3 users on Windows will need to install the official DualShock 3 drivers included as part of PlayStation Now.

## Downloading and running
Binaries of DuckStation for Windows x64/ARM64, x86_64 Linux x86_64 (in AppImage format), and Android ARMv8/AArch64 are available via GitHub Releases and are automatically built with every commit/push. Binaries or packages distributed through other sources may be out of date and are not supported by the developer.

### Windows

**Windows 10 is the only version of Windows supported by the developer.** Windows 7/8 may work, but is not supported. I am aware some users are still using Windows 7, but it is no longer supported by Microsoft and too much effort to get running on modern hardware. Game bugs are unlikely to be affected by the operating system, however performance issues should be verified on Windows 10 before reporting.

To download:
 - Go to https://github.com/stenzek/duckstation/releases/tag/latest, and download the Windows x64 build. This is a zip archive containing the prebuilt binary.
 - Alternatively, direct download link: https://github.com/stenzek/duckstation/releases/download/latest/duckstation-windows-x64-release.zip
 - Extract the archive **to a subdirectory**. The archive has no root subdirectory, so extracting to the current directory will drop a bunch of files in your download directory if you do not extract to a subdirectory.

Once downloaded and extracted, you can launch the Qt frontend from `duckstation-qt-x64-ReleaseLTCG.exe`, or the SDL frontend from `duckstation-sdl-x64-ReleaseLTCG.exe`.
To set up:
1. Either configure the path to a BIOS image in the settings, or copy one or more PlayStation BIOS images to the bios/ subdirectory. On Windows, by default this will be located in `C:\Users\YOUR_USERNAME\Documents\DuckStation\bios`. If you don't want to use the Documents directory to save the BIOS/memory cards/etc, you can use portable mode. See [User directory](#user-directories).
2. If using the Qt frontend, add the directories containing your disc images by clicking `Settings->Add Game Directory`.
2. Select a game from the list, or open a disc image file and enjoy.

**If you get an error about `vcruntime140_1.dll` being missing, you will need to update your Visual C++ runtime.** You can do that from this page: https://support.microsoft.com/en-au/help/2977003/the-latest-supported-visual-c-downloads. Specifically, you want the x64 runtime, which can be downloaded from https://aka.ms/vs/16/release/vc_redist.x64.exe.

The Qt frontend includes an automatic update checker. Builds downloaded after 2020/08/07 will automatically check for updates each time the emulator starts, this can be disabled in Settings. Alternatively, you can force an update check by clicking `Help->Check for Updates`.

### Linux

Prebuilt binaries for 64-bit Linux distros are available for download in the AppImage format. However, these binaries may be incompatible with older Linux distros (e.g. Ubuntu distros earlier than 18.04.4 LTS) due to older distros not providing newer versions of the C/C++ standard libraries required by the AppImage binaries.

**Linux users are encouraged to build from source when possible and optionally create their own AppImages for features such as desktop integration if desired.**

To download:
 - Go to https://github.com/stenzek/duckstation/releases/tag/latest, and download either `duckstation-qt-x64.AppImage` or `duckstation-sdl-x64.AppImage` for your desired frontend. Keep in mind that keyboard/controller bindings are currently not customizable through the SDL frontend and should be customized through the Qt frontend instead.
 - Run `chmod a+x` on the downloaded AppImage -- following this step, the AppImage can be run like a typical executable.
 - Optionally use a program such as [appimaged](https://github.com/AppImage/appimaged) or [AppImageLauncher](https://github.com/TheAssassin/AppImageLauncher) for desktop integration. [AppImageUpdate](https://github.com/AppImage/AppImageUpdate) can be used alongside appimaged to easily update your DuckStation AppImage.

### macOS

To download:
 - Go to https://github.com/stenzek/duckstation/releases/tag/latest, and download the Mac build. This is a zip archive containing the prebuilt binary.
 - Alternatively, direct download link: https://github.com/stenzek/duckstation/releases/download/latest/duckstation-mac-release.zip
 - Extract the zip archive. If you're using Safari, apparently this happens automatically. This will give you DuckStation.app.
 - Right click DuckStation.app, and click Open. As the package is not signed (Mac certificates are expensive), you must do this the first time you open it. Subsequent runs can be done by double-clicking.

macOS support is considered experimental and not actively supported by the developer; the builds are provided here as a courtesy. Please feel free to submit issues, but it may be some time before
they are investigated.

**macOS builds do not support automatic updates yet.** If there is sufficient demand, this may be something I will consider.


### Android

A prebuilt APK is now available for Android. However, please keep in mind that the Android version does not contain all features present in the desktop version yet. You will need a device with armv7 (32-bit ARM) or AArch64 (64-bit ARM). 64-bit is preferred, the requirements are higher for 32-bit, you'll probably want at least a 1.5GHz CPU.

Download link: https://github.com/stenzek/duckstation/releases/download/latest/duckstation-android.apk

To use:
 - Install and run the app for the first time.
 - This will create `/sdcard/duckstation`. Drop your BIOS files in `/sdcard/duckstation/bios`.
 - Add game directories by hitting the `+` icon and selecting a directory.
 - Map your controller buttons and axes by going into `Controller Mapping` under `Controllers` in `Settings`.
 - Tap a game to start.


### Title Information

PlayStation game discs do not contain title information. For game titles, we use the redump.org database cross-referenced with the game's executable code. A version of the database is included with the DuckStation download, but you can replace this with a different database by saving it as `cache/redump.dat` in your user directory, or updated by going into the `Game List Settings` in the Qt Frontend, and clicking `Update Redump Database`.

### Region detection and BIOS images
By default, DuckStation will emulate the region check present in the CD-ROM controller of the console. This means that when the region of the console does not match the disc, it will refuse to boot, giving a "Please insert PlayStation CD-ROM" message. DuckStation supports automatic detection disc regions, and if you set the console region to auto-detect as well, this should never be a problem.

If you wish to use auto-detection, you do not need to change the BIOS path each time you switch regions. Simply place the BIOS images for the other regions in the **same directory** as the configured image. This will probably be in the `bios/` subdirectory. Then set the console region to "Auto-Detect", and everything should work fine. The console/log will tell you if you are missing the image for the disc's region.

Some users have been confused by the "BIOS Path" option, the reason it is a path and not a directory is so that an unknown BIOS revision can be used/tested.

Alternatively, the region checking can be disabled in the console options tab. This is the only way to play unlicensed games or homebrew which does not supply a correct region string on the disc, aside from using fastboot which skips the check entirely.

Mismatching the disc and console regions with the check disabled is supported, but may break games if they are patching the BIOS and expecting specific content.

### LibCrypt protection and SBI files

A number of PAL region games use LibCrypt protection, requiring additional CD subchannel information to run properly. libcrypt not functioning usually manifests as hanging or crashing, but can sometimes affect gameplay too, depending on how the game implemented it.

For these games, make sure that the CD image and its corresponding SBI (.sbi) file have the same name and are placed in the same directory. DuckStation will automatically load the SBI file when it is found next to the CD image.

For example, if your disc image was named `Spyro3.cue`, you would place the SBI file in the same directory, and name it `Spyro3.sbi`.

## Building

### Windows
Requirements:
 - Visual Studio 2019
 
1. Clone the respository with submodules (`git clone --recursive https://github.com/stenzek/duckstation.git -b dev`).
2. Open the Visual Studio solution `duckstation.sln` in the root, or "Open Folder" for cmake build.
3. Build solution.
4. Binaries are located in `bin/x64`.
5. Run `duckstation-sdl-x64-Release.exe`/`duckstation-qt-x64-Release.exe` or whichever config you used.

### Linux
Requirements (Debian/Ubuntu package names):
 - CMake (`cmake`)
 - SDL2 (`libsdl2-dev`)
 - GTK3.0 for file selector (`libgtk-3-dev`)
 - pkgconfig (`pkg-config`)
 - Qt 5 (`qtbase5-dev`, `qtbase5-private-dev`, `qtbase5-dev-tools`, `qttools5-dev`)
 - git (`git`) (Note: needed to clone the repository and at build time)
 - Optional for faster building: Ninja (`ninja-build`)

1. Clone the repository. Submodules aren't necessary, there is only one and it is only used for Windows (`git clone https://github.com/stenzek/duckstation.git -b dev`).
2. Create a build directory, either in-tree or elsewhere.
3. Run cmake to configure the build system. Assuming a build subdirectory of `build-release`, `cd build-release && cmake -DCMAKE_BUILD_TYPE=Release -GNinja ..`.
4. Compile the source code. For the example above, run `ninja`.
5. Run the binary, located in the build directory under `bin/duckstation-sdl`, or `bin/duckstation-qt`.

### macOS
**NOTE:** macOS is highly experimental and not tested by the developer. Use at your own risk, things may be horribly broken.

**The following guide assumes you are cloning the non-official fork at `https://github.com/tonyunreal/duckstation/tree/tonyunreal-m1`, and you are building on a Apple Silicon Mac. This fork is not supported in anyway, either by me (tonyunreal) or the developers of DuckStation. Building on intel is also possible, just ignore all the steps for installing arm64 libaries and follow the steps on building an intel binary.**

Preliminary Steps:

1. Get the latest version of Xcode (either from the Mac App Store or from Apple's developer website), run it once and install the Command Line Tools as prompted.

  If you happen to have the macOS version of the Command Line Tools installed before this, Cmake might complain about missing tools during complication. If this happens, do this:

`sudo xcode-select -s /Applications/Xcode.app/Contents/Developer`

2. Make sure you have both the arm64 and the x64 versions of Homebrew installed. If you don't know how to do this, see [this article](https://soffes.blog/homebrew-on-apple-silicon). The following steps assumes that you have the arm64 version installed in `/opt/homebrew` and the x64 version in `/usr/local`.

  Get both versions of qt5 and cmake installed:
    
`brew install cmake qt`

`ibrew install cmake qt`
    
  Get the HEAD version of SDL2 installed, for better controller support with Apple Silicon:
    
  `brew install hg`
    
  `brew install --HEAD sdl2`

  `ibrew install hg`
    
  `ibrew install --HEAD sdl2`

3. (optional) Get the latest version of MoltenVK, either with Homebrew or build it from the source code on Github.
  
Cloning the code:

1. Clone the repository:

  `git clone https://github.com/tonyunreal/duckstation -b tonyunreal-m1`
  
2. (optional) Clone the macOS dependency repository, which only has MoltenVK for the moment:

  `git clone https://github.com/stenzek/duckstation-ext-mac.git`
  
3. Copy the `libvulkan.dylib` you got (see part 3 of "Preliminary Steps" and part 2 of "Cloning the code") to `duckstation/dep/mac/MoltenVK/`.

Building the code:

For Apple Silicon:

  1. Create a build directory:

  `cd duckstation`
  
  `mkdir -p build/arm64`
  
  `cd build/arm64`
  
  2. Run Cmake to configure the build system:
  
  `PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig" Qt5_DIR="/opt/homebrew/opt/qt5" CMAKE_OSX_ARCHITECTURES=arm64 CMAKE_BUILD_TYPE=Release arch -arm64 /opt/homebrew/bin/cmake ../..`
  
  3. Compile the code (replace `8` with the number of cores or hyper-threads that your system has):
  
  `make -j8`
  
  4. Sign the binary locally so it runs on your computer (if you need to distribute the binary to others, you might need to sign it with a proper developer certificate):

  `codesign --deep --force -s "-" bin/DuckStation.app`
  
  5. Open Finder, locate to `duckstation/build/arm64/bin/` and double-click `DuckStation.app` to run it, if the app asks for permission for reading certain storage locations, click "OK".

For Rosetta 2 / Intel:

  1. Create a build directory:

  `cd duckstation`
  
  `mkdir -p build/x64`
  
  `cd build/x64`
  
  2. Run Cmake to configure the build system:
  
  `PKG_CONFIG_PATH="/usr/local/lib/pkgconfig" Qt5_DIR="/usr/local/opt/qt5" CMAKE_OSX_ARCHITECTURES=x86_64 CMAKE_BUILD_TYPE=Release arch -x86_64 /usr/local/bin/cmake ../..`
  
  3. Compile the code (replace `8` with the number of cores or hyper-threads that your system has):
  
  `make -j8`
  
  4. Sign the binary locally so it runs on your computer (if you need to distribute the binary to others, you might need to sign it with a proper developer certificate):

  `codesign --deep --force -s "-" bin/DuckStation.app`
  
  5. Open Finder, locate to `duckstation/build/x64/bin/` and double-click `DuckStation.app` to run it, if the app asks for permission for reading certain storage locations, click "OK".

### Android
Requirements:
 - Android Studio with the NDK and CMake installed

1. Clone the repository. Submodules aren't necessary, there is only one and it is only used for Windows.
2. Open the project in the `android` directory.
3. Select Build -> Build Bundle(s) / APKs(s) -> Build APK(s).
4. Install APK on device, or use Run menu for attached device.

## User Directories
The "User Directory" is where you should place your BIOS images, where settings are saved to, and memory cards/save states are saved by default.
An optional [SDL game controller database file](#sdl-game-controller-database) can be also placed here.

This is located in the following places depending on the platform you're using:

- Windows: My Documents\DuckStation
- Linux: `$XDG_DATA_HOME/duckstation`, or `~/.local/share/duckstation`.
- macOS: `~/Library/Application Support/DuckStation`.

So, if you were using Linux, you would place your BIOS images in `~/.local/share/duckstation/bios`. This directory will be created upon running DuckStation
for the first time.

If you wish to use a "portable" build, where the user directory is the same as where the executable is located, create an empty file named `portable.txt`
in the same directory as the DuckStation executable.

## Bindings for Qt frontend
Your keyboard and any SDL-compatible game controller can be used to simulate the PS Controller. To bind keys/controllers to buttons, go to
`Settings -> Port Settings`. Each of the buttons will be listed, along with the corresponding key it is bound to. To re-bind the button to a new key,
click the button next to button name, and press the key/button you want to use within 5 seconds.

**Currently, it is only possible to bind one input to each controller button/axis. Multiple bindings per button are planned for the future.**

## Bindings for SDL frontend
Keyboard bindings in the SDL frontend are currently not customizable in the frontend itself. You should use the Qt frontend to set up your key/controller bindings first.

## SDL Game Controller Database
DuckStation uses the SDL2 GameController API for input handling which requires controller devices to have known input mappings.
SDL2 provides an embedded database of recognised controllers in its own source code, however it is rather small and thus limited in practice.

There is an officially endorsed [community sourced database](https://github.com/gabomdq/SDL_GameControllerDB) that can be used to support a much broader range of game controllers in DuckStation.
If your controller is not recognized by DuckStation but can be found in the community database above, just download a recent copy of the `gamecontrollerdb.txt` database file and place it in your [User directory](#user-directories). Your controller should now be recognized by DuckStation.

Alternatively, you can also create your own custom controller mappings from scratch easily using readily available tools. See the referenced community database repository for more information.

Using a mappings database is specially useful when using non-XInput game controllers with DuckStation.

## Default bindings
Controller 1:
 - **D-Pad:** W/A/S/D
 - **Triangle/Square/Circle/Cross:** Numpad8/Numpad4/Numpad6/Numpad2
 - **L1/R1:** Q/E
 - **L2/R2:** 1/3
 - **Start:** Enter
 - **Select:** Backspace

Hotkeys:
 - **Escape:** Power off console
 - **ALT+ENTER:** Toggle fullscreen
 - **Tab:** Temporarily disable speed limiter
 - **Pause/Break:** Pause/resume emulation
 - **Page Up/Down:** Increase/decrease resolution scale in hardware renderers
 - **End:** Toggle software renderer
 
## Tests
 - Passes amidog's CPU and GTE tests in both interpreter and recompiler modes, partial passing of CPX tests

## Screenshots
<p align="center">
  <a href="https://raw.githubusercontent.com/stenzek/duckstation/md-images/monkey.jpg"><img src="https://raw.githubusercontent.com/stenzek/duckstation/md-images/monkey.jpg" alt="Monkey Hero" width="400" /></a>
  <a href="https://raw.githubusercontent.com/stenzek/duckstation/md-images/rrt4.jpg"><img src="https://raw.githubusercontent.com/stenzek/duckstation/md-images/rrt4.jpg" alt="Ridge Racer Type 4" width="400" /></a>
  <a href="https://raw.githubusercontent.com/stenzek/duckstation/md-images/tr2.jpg"><img src="https://raw.githubusercontent.com/stenzek/duckstation/md-images/tr2.jpg" alt="Tomb Raider 2" width="400" /></a>
  <a href="https://raw.githubusercontent.com/stenzek/duckstation/md-images/quake2.jpg"><img src="https://raw.githubusercontent.com/stenzek/duckstation/md-images/quake2.jpg" alt="Quake 2" width="400" /></a>
  <a href="https://raw.githubusercontent.com/stenzek/duckstation/md-images/croc.jpg"><img src="https://raw.githubusercontent.com/stenzek/duckstation/md-images/croc.jpg" alt="Croc" width="400" /></a>
  <a href="https://raw.githubusercontent.com/stenzek/duckstation/md-images/croc2.jpg"><img src="https://raw.githubusercontent.com/stenzek/duckstation/md-images/croc2.jpg" alt="Croc 2" width="400" /></a>
  <a href="https://raw.githubusercontent.com/stenzek/duckstation/md-images/ff7.jpg"><img src="https://raw.githubusercontent.com/stenzek/duckstation/md-images/ff7.jpg" alt="Final Fantasy 7" width="400" /></a>
  <a href="https://raw.githubusercontent.com/stenzek/duckstation/md-images/ff8.jpg"><img src="https://raw.githubusercontent.com/stenzek/duckstation/md-images/ff8.jpg" alt="Final Fantasy 8" width="400" /></a>
  <a href="https://raw.githubusercontent.com/stenzek/duckstation/md-images/main.png"><img src="https://raw.githubusercontent.com/stenzek/duckstation/md-images/main.png" alt="SDL Frontend" width="400" /></a>
  <a href="https://raw.githubusercontent.com/stenzek/duckstation/md-images/spyro.jpg"><img src="https://raw.githubusercontent.com/stenzek/duckstation/md-images/spyro.jpg" alt="Spyro 2" width="400" /></a>
  <a href="https://raw.githubusercontent.com/stenzek/duckstation/md-images/gamegrid.png"><img src="https://raw.githubusercontent.com/stenzek/duckstation/md-images/gamegrid.png" alt="Game Grid" width="400" /></a>
</p>

## Disclaimers

Icon by icons8: https://icons8.com/icon/74847/platforms.undefined.short-title

"PlayStation" and "PSX" are registered trademarks of Sony Interactive Entertainment Europe Limited. This project is not affiliated in any way with Sony Interactive Entertainment.


