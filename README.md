# SECTOR ZERO

An entry for the **1.44MB 게임개발 공모전** (2P GAME ARCADE, 2026).

The whole game has to fit in **1,474,560 bytes** — the real capacity of a 3.5"
HD floppy — measured after decompression, and it has to run without a browser,
a server, or an internet connection.

## The rules that shape every decision

| | |
|---|---|
| Size limit | 1,474,560 bytes, uncompressed, all files combined |
| Distribution | one standalone `.exe`, no redistributable DLLs |
| Deadline | 2026-09-04, 23:39 KST |
| Judging | 1. Is it finished? 2. Is it within budget? 3. Is it fun? |

## Stack

C99 + Win32 + OpenGL 3.3 core. No engine, no framework, no GL loader library.

`opengl32.dll`, `gdi32.dll` and `winmm.dll` all ship with Windows, so linking
against them costs zero distribution bytes. Everything else follows from that:

- **Textures and geometry**: generated in shaders at runtime. 0 bytes on disk.
- **Audio**: PCM synthesised at runtime and pushed through `waveOut`. 0 bytes.
- **Fonts**: system fonts rasterised into a texture atlas via GDI at startup. 0 bytes.
- **GL entry points**: pulled with `wglGetProcAddress` from a hand-written list
  in `src/gl33.h`, so we pay for the functions we call and nothing else.

Current budget: roughly 145 KB projected, leaving about 90% of the disk free.

## Playing the latest build

CI overwrites a rolling `latest` release on every push to main, so there is one
fixed URL that needs no GitHub account:

    https://github.com/41ways/floppy144/releases/download/latest/SOUNDING.exe

`./update.sh` fetches it into `~/play`. Windows SmartScreen will warn about an
unsigned binary; the exe is 80 KB of the source in this repo.

Click the window once to capture the mouse. WASD moves in silence, click sends
a ping, Esc releases the mouse and a second Esc quits.

## Build

Pushing is enough: GitHub Actions cross-compiles with mingw-w64, gates the size
and publishes the result. The size report lands in the run summary, so it reads
fine from a phone.

For tuning you want the local loop instead, because it is two seconds rather
than a minute. With a native mingw-w64 (WinLibs, MSYS2) on PATH:

```bash
mingw32-make CC=gcc          # release build into dist/
mingw32-make CC=gcc size     # build, then report against the byte budget
```

On Linux or macOS with the cross toolchain installed, plain `make` picks up
`x86_64-w64-mingw32-gcc` on its own. CI stays the source of truth for the size
number, since that is the compiler the submitted binary comes from.

## Layout

```
src/main.c      window, WGL context creation, frame loop
src/gl33.h/.c   minimal OpenGL 3.3 loader (X-macro list of entry points)
src/shaders.h   GLSL kept as source strings
tools/sizecheck.py   the 1,474,560 byte gate, also runs in CI
```

## Controls

`Esc` quits. That is the whole input surface so far.
