# Build for 64-bit Windows. Cross-compiles from Linux in CI, and works
# unchanged under MSYS2/mingw locally if you set CC=gcc.
#
#   make          release build into dist/
#   make debug    same, with warnings loud and symbols kept
#   make size     build, then report against the 1,474,560 byte budget
#   make clean

# CC ?= does not work here: make ships a built-in default of `cc`, which counts
# as already defined, so the cross compiler never gets picked up. Checking
# origin overrides only that built-in and still lets `make CC=gcc` win.
ifeq ($(origin CC),default)
CC := x86_64-w64-mingw32-gcc
endif

OUT     := dist
TARGET  := $(OUT)/SOUNDING.exe
SRC     := src/main.c src/gl33.c src/game.c src/audio.c

# -Os over -O2: on this codebase the size win is large and the speed cost is
# nil, because the frame time lives in the shader, not in C.
CFLAGS  := -std=c99 -Os \
           -ffunction-sections -fdata-sections \
           -fno-ident -fno-asynchronous-unwind-tables -fno-unwind-tables \
           -fmerge-all-constants -fno-stack-protector \
           -Wall -Wextra

# -mwindows: no console window. --gc-sections + -s: drop unreferenced code
# and every symbol, which is most of the size difference.
LDFLAGS := -mwindows -Wl,--gc-sections -Wl,--build-id=none -s
LIBS    := -lopengl32 -lgdi32 -lwinmm -luser32

.PHONY: all debug size clean

all: $(TARGET)

$(TARGET): $(SRC) src/gl33.h src/shaders.h src/game.h src/audio.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(SRC) -o $@ $(LDFLAGS) $(LIBS)
	@echo "built $@ -- $$(wc -c < $@ | tr -d ' ') bytes"   # stat -c is GNU-only; wc works on the Mac too

debug: CFLAGS := -std=c99 -O0 -g -Wall -Wextra
debug: LDFLAGS := -mwindows
debug: $(TARGET)

size: $(TARGET)
	@python3 tools/sizecheck.py $(OUT)

clean:
	rm -rf $(OUT)
