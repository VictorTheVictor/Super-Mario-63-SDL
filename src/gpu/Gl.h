#pragma once
// OpenGL ES 2.0 functions, loaded at run time through SDL (gl::Clear(...) etc.). Only the
// ES 2.0 subset is used, so the same code runs on an ES 2.0 context (Android) and on a
// desktop OpenGL 2.1 context. SDL's bundled headers give the types and constants, so no
// system GL headers are needed.

#include <initializer_list>

#define SDL_USE_BUILTIN_OPENGL_DEFINITIONS 1
#define GL_GLES_PROTOTYPES 0
#include <SDL_opengles2.h>

#define SM63_GL_FUNCTIONS(F)                                                                  \
    F(ACTIVETEXTURE, ActiveTexture)                                                           \
    F(ATTACHSHADER, AttachShader)                                                             \
    F(BINDATTRIBLOCATION, BindAttribLocation)                                                 \
    F(BINDBUFFER, BindBuffer)                                                                 \
    F(BINDFRAMEBUFFER, BindFramebuffer)                                                       \
    F(BINDRENDERBUFFER, BindRenderbuffer)                                                     \
    F(CHECKFRAMEBUFFERSTATUS, CheckFramebufferStatus)                                         \
    F(DELETEFRAMEBUFFERS, DeleteFramebuffers)                                                 \
    F(DELETERENDERBUFFERS, DeleteRenderbuffers)                                               \
    F(FRAMEBUFFERRENDERBUFFER, FramebufferRenderbuffer)                                       \
    F(FRAMEBUFFERTEXTURE2D, FramebufferTexture2D)                                             \
    F(GENFRAMEBUFFERS, GenFramebuffers)                                                       \
    F(GENRENDERBUFFERS, GenRenderbuffers)                                                     \
    F(RENDERBUFFERSTORAGE, RenderbufferStorage)                                               \
    F(BINDTEXTURE, BindTexture)                                                               \
    F(BLENDFUNC, BlendFunc)                                                                   \
    F(BLENDFUNCSEPARATE, BlendFuncSeparate)                                                   \
    F(COPYTEXSUBIMAGE2D, CopyTexSubImage2D)                                                   \
    F(BUFFERDATA, BufferData)                                                                 \
    F(CLEAR, Clear)                                                                           \
    F(CLEARCOLOR, ClearColor)                                                                 \
    F(CLEARSTENCIL, ClearStencil)                                                             \
    F(COLORMASK, ColorMask)                                                                   \
    F(COMPILESHADER, CompileShader)                                                           \
    F(CREATEPROGRAM, CreateProgram)                                                           \
    F(CREATESHADER, CreateShader)                                                             \
    F(DELETEBUFFERS, DeleteBuffers)                                                           \
    F(DELETEPROGRAM, DeleteProgram)                                                           \
    F(DELETESHADER, DeleteShader)                                                             \
    F(DELETETEXTURES, DeleteTextures)                                                         \
    F(DISABLE, Disable)                                                                       \
    F(DRAWARRAYS, DrawArrays)                                                                 \
    F(ENABLE, Enable)                                                                         \
    F(ENABLEVERTEXATTRIBARRAY, EnableVertexAttribArray)                                       \
    F(FINISH, Finish)                                                                         \
    F(GENBUFFERS, GenBuffers)                                                                 \
    F(GENTEXTURES, GenTextures)                                                               \
    F(GETERROR, GetError)                                                                     \
    F(GETINTEGERV, GetIntegerv)                                                               \
    F(GETPROGRAMINFOLOG, GetProgramInfoLog)                                                   \
    F(GETPROGRAMIV, GetProgramiv)                                                             \
    F(GETSHADERINFOLOG, GetShaderInfoLog)                                                     \
    F(GETSHADERIV, GetShaderiv)                                                               \
    F(GETSTRING, GetString)                                                                   \
    F(GETUNIFORMLOCATION, GetUniformLocation)                                                 \
    F(LINKPROGRAM, LinkProgram)                                                               \
    F(PIXELSTOREI, PixelStorei)                                                               \
    F(READPIXELS, ReadPixels)                                                                 \
    F(SCISSOR, Scissor)                                                                       \
    F(SHADERSOURCE, ShaderSource)                                                             \
    F(STENCILFUNC, StencilFunc)                                                               \
    F(STENCILMASK, StencilMask)                                                               \
    F(STENCILOP, StencilOp)                                                                   \
    F(STENCILOPSEPARATE, StencilOpSeparate)                                                   \
    F(TEXIMAGE2D, TexImage2D)                                                                 \
    F(TEXPARAMETERI, TexParameteri)                                                           \
    F(TEXSUBIMAGE2D, TexSubImage2D)                                                           \
    F(UNIFORM1F, Uniform1f)                                                                   \
    F(UNIFORM1I, Uniform1i)                                                                   \
    F(UNIFORM2F, Uniform2f)                                                                   \
    F(UNIFORM3F, Uniform3f)                                                                   \
    F(UNIFORM4F, Uniform4f)                                                                   \
    F(UNIFORMMATRIX4FV, UniformMatrix4fv)                                                     \
    F(USEPROGRAM, UseProgram)                                                                 \
    F(VERTEXATTRIBPOINTER, VertexAttribPointer)                                               \
    F(VIEWPORT, Viewport)

namespace gl {

#define SM63_GL_DECLARE(UPPER, Name) extern PFNGL##UPPER##PROC Name;
SM63_GL_FUNCTIONS(SM63_GL_DECLARE)
#undef SM63_GL_DECLARE

// Optional (desktop OpenGL 3.0 or extensions; null when missing): multisampled offscreen
// framebuffers and copying between framebuffers.
extern PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC RenderbufferStorageMultisample;
extern PFNGLBLITFRAMEBUFFERNVPROC BlitFramebuffer;
constexpr GLenum kReadFramebuffer = 0x8CA8, kDrawFramebuffer = 0x8CA9, kRgba8 = 0x8058,
                 kDepth24Stencil8 = 0x88F0, kRgba16f = 0x881A, kHalfFloatOes = 0x8D61;

// Loads every function from the current context (an "EXT" name where the plain one is
// missing, as framebuffer objects are on some OpenGL 2.1 drivers); false (and `missing`
// names one) if any is unavailable.
bool load(const char*& missing);

// Whether the context is OpenGL ES (shaders then need a precision statement).
bool isEs();

// Compiles and links a program from GLSL ES 1.00 sources (a desktop context gets them
// as GLSL 1.20); attribute i is bound to `attribs[i]`. 0 on failure (logged).
GLuint program(const char* vertex, const char* fragment, const std::initializer_list<const char*>& attribs);

}  // namespace gl
