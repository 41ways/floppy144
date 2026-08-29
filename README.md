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

## Build

There is no C compiler on the primary dev machine — CI does the building. Push,
and GitHub Actions cross-compiles with mingw-w64, checks the size, and uploads
`dist/` as an artifact. The size report also appears in the run summary, so the
result is legible from a phone.

To build locally you need mingw-w64:

```bash
make        # release build into dist/
make size   # build, then report against the 1,474,560 byte budget
make clean
```

Under MSYS2 the cross prefix is unnecessary:

```bash
make CC=gcc
```

## Layout

```
src/main.c      window, WGL context creation, frame loop
src/gl33.h/.c   minimal OpenGL 3.3 loader (X-macro list of entry points)
src/shaders.h   GLSL kept as source strings
tools/sizecheck.py   the 1,474,560 byte gate, also runs in CI
```

## Controls

`Esc` quits. That is the whole input surface so far.
