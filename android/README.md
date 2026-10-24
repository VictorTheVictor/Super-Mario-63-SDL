# Android build

A Gradle project that packages the port as an Android app. SDL2's Java activity runs
the same `src/main.cpp` as the desktop build (as `libmain.so`); SDL2 itself is built
from its source release.

| Setting | Value |
|---|---|
| minSdk | 19 (Android 4.4, the oldest SDL2 2.32 supports) |
| targetSdk | 29 (Android 10) |
| compileSdk | 34 (needed by SDL's Java code; does not affect which devices run it) |
| NDK | r25c (25.2.9519653), the last NDK that builds for API 19 |
| ABIs | arm64-v8a, armeabi-v7a, x86, x86_64 |
| Gradle / plugin | Gradle 9.x, Android Gradle Plugin 9.4.1 |

## Setup

1. An Android SDK with `build-tools;34.0.0`, `platforms;android-34`,
   `ndk;25.2.9519653` and `cmake;3.22.1`, and `local.properties` pointing at it:

       sdk.dir=/home/you/Android/Sdk

2. SDL2 2.32.10's source in `deps/` (not in git):

       mkdir -p deps && cd deps
       curl -LO https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz
       tar xzf SDL2-2.32.10.tar.gz

3. Your own `Super_Mario_63_2010.swf` in `assets/` (not in git). The app loads it from
   the APK.

## Build and install

    gradle assembleDebug
    adb install -r app/build/outputs/apk/debug/app-debug.apk

The native code is compiled with optimizations in every build type (the game is too slow
at `-O0`), and for four ABIs, so a full build takes a long time.

## On the device

- Landscape, immersive full screen (system bars hidden). Touch acts as the mouse (menus); a USB or Bluetooth controller
  plays (D-pad/stick, A jump, B FLUDD, X spin, Y ground pound/talk, Start pause menu).
  Android offers to open the app when a USB controller is plugged in.
- Saves go to the app's private storage. The back button quits. The Runouw/Newgrounds
  links do nothing (a tap would leave the game).
- The generated game code is a translation of runouw's code (see the main README's
  licensing note): keep APKs private.
