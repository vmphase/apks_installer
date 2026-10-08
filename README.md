# apks_installer

A small multiplatform command-line tool that installs `.apks` split-APK bundles (as produced by bundletool) onto an Android device over ADB. Each APK is streamed straight from the archive, with nothing extracted to disk.

Trivial flow:

1. Opens the `.apks` file (a ZIP) with [miniz](https://github.com/richgel999/miniz) and collects every embedded `.apk` entry and its uncompressed size.
2. Connects to the local ADB server on `127.0.0.1:5037`. If nothing is listening, it runs `adb start-server` (bundled copy first, then `PATH`) and retries once.
3. Selects the device with `host:transport-any`, or `host:transport:<serial>` if a serial is given.
4. Runs the package manager session flow over `exec:` services:

```
cmd package install-create -r -S <total_bytes>
cmd package install-write  -S <apk_bytes> <session> <index>_<name>.apk -
cmd package install-commit <session>
```

## Requirements

- A C99 compiler with GNU extensions (`gcc` or `clang`); MinGW-w64 on Windows
- [adb](https://developer.android.com/tools/releases/platform-tools) from platform-tools, on `PATH` or in `platform-tools/` next to the executable
- An Android device with USB debugging enabled

## Build

With make:

```
make
```


Without make:

```
# Windows
gcc -std=gnu99 -O2 -w -c miniz/miniz.c -o miniz/miniz.o
gcc -std=gnu99 -O2 -Wall -Wextra -isystem miniz src/main.c src/apks.c src/install.c src/adb_win.c src/adb_posix.c miniz/miniz.o -o apks_installer.exe -lws2_32

# Linux / macOS
gcc -std=gnu99 -O2 -w -c miniz/miniz.c -o miniz/miniz.o
gcc -std=gnu99 -O2 -Wall -Wextra -isystem miniz src/main.c src/apks.c src/install.c src/adb_win.c src/adb_posix.c miniz/miniz.o -o apks_installer
```

## Usage

```
apks_installer <file.apks> [device_serial]
```

With one connected device the serial can be omitted. With several, pass the serial as listed by `adb devices`.

```
apks_installer app.apks
apks_installer app.apks R58M123ABC
```

## License

Apache 2.0. See the [LICENSE](LICENSE) file.
