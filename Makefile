# Build for 64-bit Windows. Cross-compiles from Linux in CI, and works
# unchanged under MSYS2/mingw locally if you set CC=gcc.
#
#   make          release build into dist/
#   make debug    same, with warnings loud and symbols kept
#   make size     build, then report against the 1,474,560 byte budget
#   make clean

CC      ?= x86_64-w64-mingw32-gcc
OUT     := dist
TARGET  := $(OUT)/SECTORZERO.exe
SRC     := src/main.c src/gl33.c

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

$(TARGET): $(SRC) src/gl33.h src/shaders.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) $(SRC) -o $@ $(LDFLAGS) $(LIBS)
	@echo "built $@ -- $$(stat -c%s $@) bytes"

debug: CFLAGS := -std=c99 -O0 -g -Wall -Wextra
debug: LDFLAGS := -mwindows
debug: $(TARGET)

size: $(TARGET)
	@python3 tools/sizecheck.py $(OUT)

clean:
	rm -rf $(OUT)
