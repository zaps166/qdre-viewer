# QDRE Viewer

Simple image viewer with wide-gamut and 10-bit color support.

## Requirements

- Qt 6.11+ (qt6-base and qt6-declarative)
- CMake 3.20+ and C++23 compiler
- exiv2

## Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### CMake options

| Option | Default | Description |
|---|---|---|
| `USE_WIDGETS` | `ON` | Use QtWidgets for native file dialogs and KDE style |
| `USE_COLORD` | `ON` | colord support for X11 color management |
| `DISCARD_QML_CONTENTS` | `OFF` | Don't store QML source code in the binary |

## Install

```sh
cmake --install build
```

## AUR Install / Update

`yay -S qdre-viewer-git` or `paru -S qdre-viewer-git`

## Settings

Settings are stored at `~/.config/qdre-viewer.ini`.

### RHI backend

The `rhiBackend` key selects the graphics API used by the Qt Quick scene graph. Supported values:

| Value | Description |
|---|---|
| *(unset)* | Use the default backend (usually OpenGL) |
| `vulkan` | Force Vulkan (use this if large images appear black with OpenGL on some GPUs) |
| `opengl` | Force OpenGL |
| `software` | Force software rendering (10-bit output will not work) |

There is also a `--software` command line option that forces software rendering and takes precedence over this setting.
