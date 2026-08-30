/* main.c - platform layer.
 * Opens a window, gets a real OpenGL 3.3 core context out of WGL, gathers
 * input, and hands each frame to game.c. Nothing here links against anything
 * Windows does not already ship, so the distribution stays a single .exe. */

#define WIN32_LEAN_AND_MEAN
#include "gl33.h"
#include "game.h"
#include "audio.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define APP_TITLE   "SOUNDING"
#define APP_CLASS   "sounding_window"
#define BOOT_CLASS  "sounding_boot"
#ifndef START_DEPTH
#define START_DEPTH 0.0f   /* -DSTART_DEPTH=40 builds a stage-2 test exe */
#endif

#define WIN_W       1280
#define WIN_H       720

typedef BOOL  (WINAPI *PFNWGLCHOOSEPIXELFORMATARB)(HDC, const int*, const FLOAT*, UINT, int*, UINT*);
typedef HGLRC (WINAPI *PFNWGLCREATECONTEXTATTRIBSARB)(HDC, HGLRC, const int*);
typedef BOOL  (WINAPI *PFNWGLSWAPINTERVALEXT)(int);

static PFNWGLCHOOSEPIXELFORMATARB    p_wglChoosePixelFormatARB;
static PFNWGLCREATECONTEXTATTRIBSARB p_wglCreateContextAttribsARB;
static PFNWGLSWAPINTERVALEXT         p_wglSwapIntervalEXT;

static int g_running  = 1;
static int g_width    = WIN_W;
static int g_height   = WIN_H;
static int g_captured = 0;     /* mouse locked to the window centre */
static int g_ping     = 0;     /* a click arrived since the last frame */

static int g_headless;    /* -shot: no dialogs, or the script waits forever */

static void fail(const char *msg)
{
    if (g_headless) {
        FILE *f = fopen("faillog.txt", "w");
        if (f) { fputs(msg, f); fclose(f); }
        ExitProcess(1);
    }
    MessageBoxA(0, msg, APP_TITLE " - startup failed", MB_OK | MB_ICONERROR);
    ExitProcess(1);
}

static void set_capture(HWND hwnd, int on)
{
    if (on == g_captured) return;
    g_captured = on;
    ShowCursor(on ? FALSE : TRUE);
    if (on) SetCapture(hwnd); else ReleaseCapture();
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CLOSE:
    case WM_DESTROY:
        g_running = 0;
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            if (g_captured) set_capture(hwnd, 0);
            else g_running = 0;
        }
        return 0;
    case WM_LBUTTONDOWN:
        /* The click that grabs the mouse counts as a ping too. Otherwise the
         * title needs two clicks and the first one appears to do nothing. */
        if (!g_captured) set_capture(hwnd, 1);
        g_ping = 1;
        return 0;
    case WM_KILLFOCUS:
        set_capture(hwnd, 0);
        return 0;
    case WM_SIZE:
        g_width  = LOWORD(lp);
        g_height = HIWORD(lp);
        if (g_width  < 1) g_width  = 1;
        if (g_height < 1) g_height = 1;
        glViewport(0, 0, g_width, g_height);
        return 0;
    case WM_ERASEBKGND:
        /* the game repaints every frame, so let Windows skip the background */
        return 1;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

/* WGL only describes modern pixel formats through an extension, and looking up
 * an extension needs a live context. So: throwaway window, throwaway context,
 * grab the entry points, tear it down, then start over properly. */
static void load_wgl_extensions(HINSTANCE inst)
{
    HWND  dummy;
    HDC   dc;
    HGLRC rc;
    PIXELFORMATDESCRIPTOR pfd;
    int   fmt;

    dummy = CreateWindowExA(0, BOOT_CLASS, "", WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT, CW_USEDEFAULT, 64, 64,
                            0, 0, inst, 0);
    if (!dummy) fail("Could not create the bootstrap window.");

    dc = GetDC(dummy);

    ZeroMemory(&pfd, sizeof pfd);
    pfd.nSize        = sizeof pfd;
    pfd.nVersion     = 1;
    pfd.dwFlags      = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType   = PFD_TYPE_RGBA;
    pfd.cColorBits   = 32;
    pfd.cDepthBits   = 24;
    pfd.cStencilBits = 8;

    fmt = ChoosePixelFormat(dc, &pfd);
    if (!fmt || !SetPixelFormat(dc, fmt, &pfd))
        fail("No usable OpenGL pixel format on this display.");

    rc = wglCreateContext(dc);
    if (!rc || !wglMakeCurrent(dc, rc))
        fail("Could not create a bootstrap OpenGL context.");

    p_wglChoosePixelFormatARB =
        (PFNWGLCHOOSEPIXELFORMATARB)wglGetProcAddress("wglChoosePixelFormatARB");
    p_wglCreateContextAttribsARB =
        (PFNWGLCREATECONTEXTATTRIBSARB)wglGetProcAddress("wglCreateContextAttribsARB");
    p_wglSwapIntervalEXT =
        (PFNWGLSWAPINTERVALEXT)wglGetProcAddress("wglSwapIntervalEXT");

    wglMakeCurrent(0, 0);
    wglDeleteContext(rc);
    ReleaseDC(dummy, dc);
    DestroyWindow(dummy);

    if (!p_wglCreateContextAttribsARB)
        fail("This driver does not support OpenGL 3.3.\n"
             "Update your graphics driver and try again.");
}

static GLuint compile(GLenum type, const char *src, const char *label)
{
    GLuint sh = glCreateShader(type);
    GLint  ok = 0;

    glShaderSource(sh, 1, &src, 0);
    glCompileShader(sh);
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        char msg[1280];
        glGetShaderInfoLog(sh, sizeof log, 0, log);
        wsprintfA(msg, "%s shader failed to compile:\n\n%s", label, log);
        fail(msg);
    }
    return sh;
}

/* game.c calls this to turn its shader strings into a program. */
GLuint gfx_build_program(const char *vs_src, const char *fs_src)
{
    GLuint vs   = compile(GL_VERTEX_SHADER,   vs_src, "Vertex");
    GLuint fs   = compile(GL_FRAGMENT_SHADER, fs_src, "Fragment");
    GLuint prog = glCreateProgram();
    GLint  ok   = 0;

    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        char msg[1280];
        glGetProgramInfoLog(prog, sizeof log, 0, log);
        wsprintfA(msg, "Shader program failed to link:\n\n%s", log);
        fail(msg);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

/* Rasterise a string with GDI and hand back its lit pixels, normalised so the
 * word spans roughly two units across and is centred on the origin.
 *
 * No font ships with the game: Windows already has several, and letters that
 * arrive as pixels can be scattered into space and swept by the same wavefront
 * as the cave walls. The title is therefore not type drawn over the game - it
 * is geometry inside it. The same path will take Korean later for free. */
int plat_text_points(const char *str, int px, float *out_xy, int max_pts)
{
    HDC        dc  = CreateCompatibleDC(0);
    BITMAPINFO bi;
    void      *bits = 0;
    HBITMAP    bmp;
    HFONT      font, oldf;
    HGDIOBJ    oldb;
    unsigned  *p;
    RECT       r;
    const int  W = 1024, H = 256;
    int        n = 0, x, y;

    if (!dc) return 0;

    ZeroMemory(&bi, sizeof bi);
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = W;
    bi.bmiHeader.biHeight      = -H;              /* top-down rows */
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, 0, 0);
    if (!bmp || !bits) { DeleteDC(dc); return 0; }
    oldb = SelectObject(dc, bmp);

    font = CreateFontA(px, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       ANTIALIASED_QUALITY, FF_DONTCARE, "Consolas");
    oldf = (HFONT)SelectObject(dc, font);

    r.left = 0; r.top = 0; r.right = W; r.bottom = H;
    FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    SetTextAlign(dc, TA_CENTER | TA_TOP);
    TextOutA(dc, W / 2, (H - px) / 2, str, (int)strlen(str));
    GdiFlush();

    p = (unsigned *)bits;
    for (y = 0; y < H && n < max_pts; y += 2)
        for (x = 0; x < W && n < max_pts; x += 2)
            if ((p[y * W + x] & 0xFFu) > 110u) {
                out_xy[n * 2 + 0] = ((float)x - W * 0.5f) / (W * 0.25f);
                out_xy[n * 2 + 1] = ((float)y - H * 0.5f) / (W * 0.25f);
                n++;
            }

    SelectObject(dc, oldf);
    DeleteObject(font);
    SelectObject(dc, oldb);
    DeleteObject(bmp);
    DeleteDC(dc);
    return n;
}

/* Keep the pointer pinned to the middle of the window and read how far it
 * tried to escape. Cheaper than raw input and good enough for a prototype. */
static void read_mouse(HWND hwnd, float *dx, float *dy)
{
    POINT p, centre;
    RECT  rc;

    *dx = 0.0f;
    *dy = 0.0f;
    if (!g_captured) return;

    GetClientRect(hwnd, &rc);
    centre.x = (rc.right - rc.left) / 2;
    centre.y = (rc.bottom - rc.top) / 2;
    ClientToScreen(hwnd, &centre);

    if (!GetCursorPos(&p)) return;
    *dx = (float)(p.x - centre.x);
    *dy = (float)(p.y - centre.y);
    SetCursorPos(centre.x, centre.y);
}

/* Dump the framebuffer as raw RGB with a tiny header. Called from -shot mode,
 * which drives a scripted session and exits, so the look of a build can be
 * checked without anyone sitting in front of it. */
static void save_rgb(const char *path, int w, int h)
{
    unsigned char *px = (unsigned char *)malloc((size_t)w * h * 3);
    FILE *f;
    if (!px) return;
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px);
    f = fopen(path, "wb");
    if (f) {
        fwrite(&w, 4, 1, f);
        fwrite(&h, 4, 1, f);
        fwrite(px, 1, (size_t)w * h * 3, f);
        fclose(f);
    }
    free(px);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASSA wc;
    HWND   hwnd;
    HDC    dc;
    HGLRC  rc;
    RECT   rect;
    LARGE_INTEGER freq, start, prev_t, now_t;
    MSG    msg;
    PIXELFORMATDESCRIPTOR pfd;
    int    fmt = 0;
    UINT   fmt_count = 0;
    float  title_timer = 0.0f;
    /* -shot <file> <frame> [pingframe]: run a scripted session, save, quit */
    char   shot_path[260]; int shot_at = 0, shot_ping = 3, frame = 0, shot_spider = -1;
    float  start_depth = START_DEPTH;
    shot_path[0] = 0;

    const int pf_attribs[] = {
        WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
        WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
        WGL_DOUBLE_BUFFER_ARB,  GL_TRUE,
        WGL_ACCELERATION_ARB,   WGL_FULL_ACCELERATION_ARB,
        WGL_PIXEL_TYPE_ARB,     WGL_TYPE_RGBA_ARB,
        WGL_COLOR_BITS_ARB,     32,
        WGL_DEPTH_BITS_ARB,     24,
        WGL_STENCIL_BITS_ARB,   8,
        0
    };
    const int ctx_attribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
        WGL_CONTEXT_MINOR_VERSION_ARB, 3,
        WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0
    };

    (void)prev; (void)show;

    if (cmd && strstr(cmd, "-depth")) sscanf(strstr(cmd, "-depth") + 6, "%f", &start_depth);

    if (cmd && strstr(cmd, "-shot")) {
        g_headless = 1;
        char *p = strstr(cmd, "-shot") + 5;
        sscanf(p, "%259s %d %d %d", shot_path, &shot_at, &shot_ping, &shot_spider);
        if (shot_at <= 0) shot_at = 70;
    }

    ZeroMemory(&wc, sizeof wc);
    wc.style         = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursorA(0, IDC_ARROW);
    wc.lpszClassName = APP_CLASS;
    if (!RegisterClassA(&wc)) fail("Could not register the window class.");

    /* The throwaway window used to fish the WGL extensions out of the driver
     * gets its own class and the default handler. Sharing the game's wndproc
     * means DestroyWindow() on it fires WM_DESTROY into the game's quit path
     * and the program exits the moment it starts. */
    ZeroMemory(&wc, sizeof wc);
    wc.style         = CS_OWNDC;
    wc.lpfnWndProc   = DefWindowProcA;
    wc.hInstance     = inst;
    wc.lpszClassName = BOOT_CLASS;
    if (!RegisterClassA(&wc)) fail("Could not register the bootstrap class.");

    load_wgl_extensions(inst);

    rect.left = 0; rect.top = 0; rect.right = WIN_W; rect.bottom = WIN_H;
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    hwnd = CreateWindowExA(0, APP_CLASS, APP_TITLE, WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT,
                           rect.right - rect.left, rect.bottom - rect.top,
                           0, 0, inst, 0);
    if (!hwnd) fail("Could not create the game window.");

    dc = GetDC(hwnd);

    if (p_wglChoosePixelFormatARB &&
        p_wglChoosePixelFormatARB(dc, pf_attribs, 0, 1, &fmt, &fmt_count) &&
        fmt_count > 0) {
        DescribePixelFormat(dc, fmt, sizeof pfd, &pfd);
    } else {
        /* older driver: the classic path still gets us a 3.3 context */
        ZeroMemory(&pfd, sizeof pfd);
        pfd.nSize        = sizeof pfd;
        pfd.nVersion     = 1;
        pfd.dwFlags      = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType   = PFD_TYPE_RGBA;
        pfd.cColorBits   = 32;
        pfd.cDepthBits   = 24;
        pfd.cStencilBits = 8;
        fmt = ChoosePixelFormat(dc, &pfd);
    }
    if (!fmt || !SetPixelFormat(dc, fmt, &pfd))
        fail("No usable OpenGL pixel format on this display.");

    rc = p_wglCreateContextAttribsARB(dc, 0, ctx_attribs);
    if (!rc || !wglMakeCurrent(dc, rc))
        fail("Could not create an OpenGL 3.3 core context.\n"
             "Update your graphics driver and try again.");

    if (!gl33_load())
        fail("This driver is missing OpenGL 3.3 entry points the game needs.");

    if (p_wglSwapIntervalEXT) p_wglSwapIntervalEXT(1);   /* vsync */

    audio_init();
    QueryPerformanceCounter(&start);
    /* a capture is only worth comparing against another capture if both
       ran in the same cave, so -shot pins the seed */
    game_init(shot_path[0] ? 20260904u
                           : (unsigned)(start.QuadPart ^ (start.QuadPart >> 32)),
              start_depth);

    ShowWindow(hwnd, SW_SHOW);
    glViewport(0, 0, g_width, g_height);

    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    prev_t = start;

    while (g_running) {
        GameInput in;
        float dt, now;

        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) g_running = 0;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        QueryPerformanceCounter(&now_t);
        dt  = (float)((double)(now_t.QuadPart - prev_t.QuadPart) / (double)freq.QuadPart);
        now = (float)((double)(now_t.QuadPart - start.QuadPart)  / (double)freq.QuadPart);
        prev_t = now_t;
        if (dt > 0.1f) dt = 0.1f;          /* never let a stall teleport you */

        /* A capture is only comparable with another capture if both advanced
         * the simulation by the same amount, so -shot runs on a fixed step
         * rather than on whatever the machine managed that frame. */
        if (shot_path[0]) { dt = 1.0f / 60.0f; now = (float)frame / 60.0f; }

        in.fwd   = g_captured && (GetAsyncKeyState('W') & 0x8000) ? 1 : 0;
        in.back  = g_captured && (GetAsyncKeyState('S') & 0x8000) ? 1 : 0;
        in.left  = g_captured && (GetAsyncKeyState('A') & 0x8000) ? 1 : 0;
        in.right = g_captured && (GetAsyncKeyState('D') & 0x8000) ? 1 : 0;
        in.ping  = g_ping;
        g_ping   = 0;
        read_mouse(hwnd, &in.mdx, &in.mdy);

        if (shot_path[0] && shot_spider >= 0 && frame == 30)
            game_debug_spider(now, shot_spider);

        if (shot_path[0]) {          /* scripted: sound repeatedly, then look */
            in.fwd   = (frame > 40);      /* walk, the way it is played */
            in.back  = in.left = in.right = 0;
            in.mdx = in.mdy = 0.0f;
            /* stop sounding well before the capture so the last wave has
               died and only the map it left is on screen */
            in.ping = (shot_ping >= 0)
                   && ((frame == 2) || (shot_ping > 0 && frame > 2
                                       && frame < shot_at - 300
                                       && (frame % shot_ping) == 0));
        }

        game_frame(&in, dt, now, g_width, g_height);
        audio_update();

        if (shot_path[0]) {
            /* a capture that cannot report the state it captured is half a
               tool, so the numbers go beside the pixels */
            if ((frame % 20) == 0) {
                FILE *lg = fopen("shotlog.txt", "a");
                if (lg) {
                    fprintf(lg, "frame %4d  state %d  pos %7.2f %6.2f %7.2f  travelled %6.2f\n",
                            frame, game_state(), game_px(), game_py(),
                            game_pz(), game_travelled());
                    fclose(lg);
                }
            }
            if (++frame >= shot_at) {
                save_rgb(shot_path, g_width, g_height);
                g_running = 0;
            }
        }
        SwapBuffers(dc);

        /* no HUD yet, so the numbers that matter live in the title bar */
        title_timer += dt;
        if (title_timer > 0.25f) {
            char t[160];
            title_timer = 0.0f;
            sprintf(t, "%s  -  lives %d  -  depth %.1f m  -  %d pts  -  %d hunting  -  %s",
                    APP_TITLE, game_lives(), game_depth(),
                    game_point_count(), game_monsters(),
                    g_captured ? "click to ping, Esc to release"
                               : "click the window to look around");
            SetWindowTextA(hwnd, t);
        }
    }

    set_capture(hwnd, 0);
    audio_shutdown();
    wglMakeCurrent(0, 0);
    wglDeleteContext(rc);
    ReleaseDC(hwnd, dc);
    return 0;
}
