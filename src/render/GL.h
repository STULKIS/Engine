// Derecho Engine - OpenGL 4.5 Core function loader.
//
// The engine does not link against libGL and does not depend on a platform GL
// development package. The Khronos glcorearb.h header is vendored for types and
// enumerants only; every entry point below is resolved at runtime through
// SDL_GL_GetProcAddress. This keeps the dependency set to SDL2 alone.
#pragma once

#include <gl/glcorearb.h>

namespace derecho::render::gl {

// X(return-typedef, function-name)
#define DERECHO_GL_FUNCTIONS(X)                                     \
    X(PFNGLGETSTRINGPROC, glGetString)                               \
    X(PFNGLGETSTRINGIPROC, glGetStringi)                             \
    X(PFNGLGETINTEGERVPROC, glGetIntegerv)                           \
    X(PFNGLGETFLOATVPROC, glGetFloatv)                               \
    X(PFNGLGETERRORPROC, glGetError)                                 \
    X(PFNGLVIEWPORTPROC, glViewport)                                 \
    X(PFNGLSCISSORPROC, glScissor)                                   \
    X(PFNGLENABLEPROC, glEnable)                                     \
    X(PFNGLDISABLEPROC, glDisable)                                   \
    X(PFNGLBLENDFUNCPROC, glBlendFunc)                               \
    X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate)               \
    X(PFNGLBLENDEQUATIONPROC, glBlendEquation)                       \
    X(PFNGLBLENDEQUATIONSEPARATEPROC, glBlendEquationSeparate)       \
    X(PFNGLISENABLEDPROC, glIsEnabled)                               \
    X(PFNGLISPROGRAMPROC, glIsProgram)                               \
    X(PFNGLDRAWELEMENTSBASEVERTEXPROC, glDrawElementsBaseVertex)     \
    X(PFNGLDEPTHFUNCPROC, glDepthFunc)                               \
    X(PFNGLDEPTHMASKPROC, glDepthMask)                               \
    X(PFNGLCULLFACEPROC, glCullFace)                                 \
    X(PFNGLFRONTFACEPROC, glFrontFace)                               \
    X(PFNGLPOLYGONMODEPROC, glPolygonMode)                           \
    X(PFNGLPOLYGONOFFSETPROC, glPolygonOffset)                       \
    X(PFNGLLINEWIDTHPROC, glLineWidth)                               \
    X(PFNGLCLEARCOLORPROC, glClearColor)                             \
    X(PFNGLCLEARPROC, glClear)                                       \
    X(PFNGLCLEARBUFFERFVPROC, glClearBufferfv)                       \
    X(PFNGLPIXELSTOREIPROC, glPixelStorei)                           \
    X(PFNGLREADPIXELSPROC, glReadPixels)                             \
    X(PFNGLFINISHPROC, glFinish)                                     \
    X(PFNGLFLUSHPROC, glFlush)                                       \
    X(PFNGLGENTEXTURESPROC, glGenTextures)                           \
    X(PFNGLDELETETEXTURESPROC, glDeleteTextures)                     \
    X(PFNGLBINDTEXTUREPROC, glBindTexture)                           \
    X(PFNGLTEXIMAGE2DPROC, glTexImage2D)                             \
    X(PFNGLTEXPARAMETERIPROC, glTexParameteri)                       \
    X(PFNGLTEXPARAMETERFPROC, glTexParameterf)                       \
    X(PFNGLGENERATEMIPMAPPROC, glGenerateMipmap)                     \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture)                       \
    X(PFNGLGENSAMPLERSPROC, glGenSamplers)                           \
    X(PFNGLDELETESAMPLERSPROC, glDeleteSamplers)                     \
    X(PFNGLBINDSAMPLERPROC, glBindSampler)                           \
    X(PFNGLSAMPLERPARAMETERIPROC, glSamplerParameteri)               \
    X(PFNGLCREATESHADERPROC, glCreateShader)                      \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                    \
    X(PFNGLDELETESHADERPROC, glDeleteShader)                         \
    X(PFNGLSHADERSOURCEPROC, glShaderSource)                         \
    X(PFNGLCOMPILESHADERPROC, glCompileShader)                       \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv)                           \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                 \
    X(PFNGLGETSHADERPRECISIONFORMATPROC, glGetShaderPrecisionFormat) \
    X(PFNGLATTACHSHADERPROC, glAttachShader)                         \
    X(PFNGLDETACHSHADERPROC, glDetachShader)                         \
    X(PFNGLDELETEPROGRAMPROC, glDeleteProgram)                       \
    X(PFNGLUSEPROGRAMPROC, glUseProgram)                             \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram)                           \
    X(PFNGLVALIDATEPROGRAMPROC, glValidateProgram)                   \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                         \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)               \
    X(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation)             \
    X(PFNGLGETATTRIBLOCATIONPROC, glGetAttribLocation)               \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)             \
    X(PFNGLUNIFORM1IPROC, glUniform1i)                               \
    X(PFNGLUNIFORM1FPROC, glUniform1f)                               \
    X(PFNGLUNIFORM2FPROC, glUniform2f)                               \
    X(PFNGLUNIFORM3FPROC, glUniform3f)                               \
    X(PFNGLUNIFORM4FPROC, glUniform4f)                               \
    X(PFNGLUNIFORM1IVPROC, glUniform1iv)                             \
    X(PFNGLUNIFORM1FVPROC, glUniform1fv)                             \
    X(PFNGLUNIFORM3FVPROC, glUniform3fv)                             \
    X(PFNGLUNIFORMMATRIX3FVPROC, glUniformMatrix3fv)                 \
    X(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv)                 \
    X(PFNGLGENBUFFERSPROC, glGenBuffers)                             \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers)                       \
    X(PFNGLBINDBUFFERPROC, glBindBuffer)                             \
    X(PFNGLBUFFERDATAPROC, glBufferData)                             \
    X(PFNGLBUFFERSUBDATAPROC, glBufferSubData)                       \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                   \
    X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays)             \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                   \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)           \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)   \
    X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray) \
    X(PFNGLVERTEXATTRIBDIVISORPROC, glVertexAttribDivisor)           \
    X(PFNGLDRAWARRAYSPROC, glDrawArrays)                             \
    X(PFNGLDRAWELEMENTSPROC, glDrawElements)                         \
    X(PFNGLDRAWARRAYSINSTANCEDPROC, glDrawArraysInstanced)           \
    X(PFNGLDRAWELEMENTSINSTANCEDPROC, glDrawElementsInstanced)       \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers)                   \
    X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers)             \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)                   \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D)         \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus)     \
    X(PFNGLDRAWBUFFERSPROC, glDrawBuffers)                           \
    X(PFNGLDRAWBUFFERPROC, glDrawBuffer)                             \
    X(PFNGLREADBUFFERPROC, glReadBuffer)                             \
    X(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer)                   \
    X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers)                 \
    X(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers)           \
    X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer)                 \
    X(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage)           \
    X(PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC, glRenderbufferStorageMultisample) \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer)   \
    X(PFNGLBLENDCOLORPROC, glBlendColor)

// Declares one function pointer per entry in the list above.
#define DERECHO_GL_DECLARE(type, name) extern type name;
DERECHO_GL_FUNCTIONS(DERECHO_GL_DECLARE)
#undef DERECHO_GL_DECLARE

// Resolves every entry point. Returns false if any symbol is missing; the
// offending names are logged. Must be called with a current GL context.
bool LoadFunctions(void* (*getProcAddress)(const char*));

// Human readable GL error string ("0x0502" style, plus a short name).
const char* ErrorString(GLenum error);

// Drains the GL error queue, logging each entry with `context` as a prefix.
// Returns true when the queue was clean.
bool CheckErrors(const char* context);

} // namespace derecho::render::gl

namespace derecho::render {
// The renderer (and its helper classes) call GL entry points unqualified, the
// same way they would with a traditional GL loader header.
using namespace gl;
} // namespace derecho::render
