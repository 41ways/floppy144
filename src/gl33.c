#include "gl33.h"

/* one definition per entry point */
#define GL33_DEF(TYPE, RET, NAME, ARGS) TYPE##PROC NAME = 0;
GL_FUNCS(GL33_DEF)
#undef GL33_DEF

static HMODULE gl_dll;

/* wglGetProcAddress covers extensions; anything core-1.1 still lives in the DLL
 * itself, so we check both before giving up. */
static void *gl_get(const char *name)
{
    void *p = (void *)wglGetProcAddress(name);
    if (p == 0 || p == (void *)1 || p == (void *)2 ||
        p == (void *)3 || p == (void *)-1) {
        if (!gl_dll) gl_dll = LoadLibraryA("opengl32.dll");
        p = gl_dll ? (void *)GetProcAddress(gl_dll, name) : 0;
    }
    return p;
}

int gl33_load(void)
{
    int missing = 0;

#define GL33_LOAD(TYPE, RET, NAME, ARGS) \
    NAME = (TYPE##PROC)gl_get(#NAME); \
    if (!NAME) missing++;
    GL_FUNCS(GL33_LOAD)
#undef GL33_LOAD

    return missing == 0;
}
