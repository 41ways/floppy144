/* gl33.h — minimal OpenGL 3.3 core loader.
 * No GLAD, no GLEW: we declare only the entry points this game actually calls
 * and pull them from the driver with wglGetProcAddress at startup.
 * Every byte of a loader library is a byte not spent on the game. */
#ifndef GL33_H
#define GL33_H

#include <windows.h>
#include <GL/gl.h>
#include <stddef.h>

/* --- types the 1.1 header predates --- */
typedef char      GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

/* --- enums we use --- */
#define GL_FRAGMENT_SHADER   0x8B30
#define GL_VERTEX_SHADER     0x8B31
#define GL_COMPILE_STATUS    0x8B81
#define GL_LINK_STATUS       0x8B82
#define GL_INFO_LOG_LENGTH   0x8B84
#define GL_ARRAY_BUFFER      0x8892
#define GL_STATIC_DRAW       0x88E4
#define GL_DYNAMIC_DRAW      0x88E8
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_PROGRAM_POINT_SIZE 0x8642
#define GL_TEXTURE0          0x84C0
#define GL_CLAMP_TO_EDGE     0x812F
#define GL_MULTISAMPLE       0x809D

/* --- WGL extension enums --- */
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_FLAGS_ARB         0x2094
#define WGL_CONTEXT_PROFILE_MASK_ARB  0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x0001
#define WGL_DRAW_TO_WINDOW_ARB   0x2001
#define WGL_ACCELERATION_ARB     0x2003
#define WGL_SUPPORT_OPENGL_ARB   0x2010
#define WGL_DOUBLE_BUFFER_ARB    0x2011
#define WGL_PIXEL_TYPE_ARB       0x2013
#define WGL_COLOR_BITS_ARB       0x2014
#define WGL_DEPTH_BITS_ARB       0x2022
#define WGL_STENCIL_BITS_ARB     0x2023
#define WGL_FULL_ACCELERATION_ARB 0x2027
#define WGL_TYPE_RGBA_ARB        0x202B

/* --- the entry points we need, as an X-macro list ---
 * Add a line here and the declaration, definition and load are all handled. */
#define GL_FUNCS(X) \
  X(PFNGLCREATESHADER,      GLuint,  glCreateShader,      (GLenum)) \
  X(PFNGLSHADERSOURCE,      void,    glShaderSource,      (GLuint, GLsizei, const GLchar* const*, const GLint*)) \
  X(PFNGLCOMPILESHADER,     void,    glCompileShader,     (GLuint)) \
  X(PFNGLGETSHADERIV,       void,    glGetShaderiv,       (GLuint, GLenum, GLint*)) \
  X(PFNGLGETSHADERINFOLOG,  void,    glGetShaderInfoLog,  (GLuint, GLsizei, GLsizei*, GLchar*)) \
  X(PFNGLDELETESHADER,      void,    glDeleteShader,      (GLuint)) \
  X(PFNGLCREATEPROGRAM,     GLuint,  glCreateProgram,     (void)) \
  X(PFNGLATTACHSHADER,      void,    glAttachShader,      (GLuint, GLuint)) \
  X(PFNGLLINKPROGRAM,       void,    glLinkProgram,       (GLuint)) \
  X(PFNGLGETPROGRAMIV,      void,    glGetProgramiv,      (GLuint, GLenum, GLint*)) \
  X(PFNGLGETPROGRAMINFOLOG, void,    glGetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*)) \
  X(PFNGLUSEPROGRAM,        void,    glUseProgram,        (GLuint)) \
  X(PFNGLGENVERTEXARRAYS,   void,    glGenVertexArrays,   (GLsizei, GLuint*)) \
  X(PFNGLBINDVERTEXARRAY,   void,    glBindVertexArray,   (GLuint)) \
  X(PFNGLGENBUFFERS,        void,    glGenBuffers,        (GLsizei, GLuint*)) \
  X(PFNGLBINDBUFFER,        void,    glBindBuffer,        (GLenum, GLuint)) \
  X(PFNGLBUFFERDATA,        void,    glBufferData,        (GLenum, GLsizeiptr, const void*, GLenum)) \
  X(PFNGLBUFFERSUBDATA,     void,    glBufferSubData,     (GLenum, GLintptr, GLsizeiptr, const void*)) \
  X(PFNGLVERTEXATTRIBPOINTER, void,  glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)) \
  X(PFNGLENABLEVERTEXATTRIBARRAY, void, glEnableVertexAttribArray, (GLuint)) \
  X(PFNGLGETUNIFORMLOCATION, GLint,  glGetUniformLocation, (GLuint, const GLchar*)) \
  X(PFNGLUNIFORM1F,         void,    glUniform1f,         (GLint, GLfloat)) \
  X(PFNGLUNIFORM1I,         void,    glUniform1i,         (GLint, GLint)) \
  X(PFNGLUNIFORM2F,         void,    glUniform2f,         (GLint, GLfloat, GLfloat)) \
  X(PFNGLUNIFORM3F,         void,    glUniform3f,         (GLint, GLfloat, GLfloat, GLfloat)) \
  X(PFNGLUNIFORM3FV,        void,    glUniform3fv,        (GLint, GLsizei, const GLfloat*)) \
  X(PFNGLUNIFORM4F,         void,    glUniform4f,         (GLint, GLfloat, GLfloat, GLfloat, GLfloat)) \
  X(PFNGLUNIFORMMATRIX4FV,  void,    glUniformMatrix4fv,  (GLint, GLsizei, GLboolean, const GLfloat*))

/* typedef + extern pointer for each */
#define GL33_DECL(TYPE, RET, NAME, ARGS) \
  typedef RET (APIENTRY *TYPE##PROC) ARGS; \
  extern TYPE##PROC NAME;
GL_FUNCS(GL33_DECL)
#undef GL33_DECL

/* Returns 1 on success, 0 if the driver is missing an entry point. */
int gl33_load(void);

#endif /* GL33_H */
