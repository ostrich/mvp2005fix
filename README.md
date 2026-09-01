# mvp2005fix

True widescreen support for MVP Baseball 2005.

Existing widescreen hacks give pixel perfect output but still stretch the aspect ratio. This hack finally gives the true widescreen support you expect.

It also includes an optional save-game fix for systems where MVP Baseball 2005 has trouble saving profiles on very large drives.

## Quick Start

[Download the latest release](https://github.com/ostrich/mvp2005fix/releases/latest), or build these two files:

```text
mvp2005fix.exe
mvp2005fix.dll
```

Put them in the same folder. The easiest option is the MVP Baseball 2005 install folder, next to `mvp2005.exe`.

Run `mvp2005fix.exe`.

If the launcher finds `mvp2005.exe` in the same folder, it fills in the path automatically. If not, click Browse and select it.

Choose a resolution, leave the 3D aspect fix enabled, and click Launch. If the game starts, the launcher closes.

## What It Does

- adds modern resolution options at runtime
- fixes stretched 3D gameplay for widescreen displays
- can work alongside SafeDisc/no-CD setups and Direct3D wrappers
- can fake a smaller disk size so profile saves work on huge drives
- stores settings in one local file, `mvp2005fix.ini`

The fix is applied in memory after the launcher starts the game. Your game executable is not patched on disk. The resolution patch targets the game's original `800x600` resolution constants.

## Settings

The first successful setup creates `mvp2005fix.ini` next to the launcher and DLL. Later runs skip the window and launch the game immediately when the config has a valid game path and runtime settings.

To change settings, delete `mvp2005fix.ini` and run the launcher again.

Example:

```ini
[Launcher]
ExePath=C:\Path\To\MVP Baseball 2005\mvp2005.exe

[Resolution]
Width=3840
Height=2160

[Aspect]
Enabled=1
ScaleX=0.75

[SaveFix]
Enabled=1
```

`ScaleX` is calculated by the launcher from the selected resolution.

## Compatibility Notes

`mvp2005fix` expects a copy of MVP Baseball 2005 that already launches. It does not include a SafeDisc workaround, no-CD executable, or crack.

The tool avoids common wrapper DLL names, so it is designed to coexist with:

- SafeDisc/no-CD methods that use `version.dll`
- dgVoodoo or other `d3d8.dll` wrappers

If you already patched `mvp2005.exe` with another widescreen or resolution fix, start from a clean executable before using `mvp2005fix`. The resolution patch looks for the game's original `800x600` setting.

The aspect fix targets 3D gameplay. Menus, movies, and some 2D overlays may still be stretched.

## Debug Logging

Normal runs are quiet. To enable DLL diagnostics, add this to `mvp2005fix.ini`:

```ini
[Debug]
Logging=1
```

The DLL writes `mvp2005fix.log` next to the launcher and DLL.

## Building

Requirements:

- `i686-w64-mingw32-gcc`
- `make`

Build:

```sh
make release
```

Outputs:

```text
release/mvp2005fix.exe
release/mvp2005fix.dll
```

Clean generated files:

```sh
make clean
```

## License

MIT. See [LICENSE](LICENSE).
