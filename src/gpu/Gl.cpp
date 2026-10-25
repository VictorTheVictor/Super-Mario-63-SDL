#include "gpu/Gl.h"

#include <SDL.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace gl {

#define SM63_GL_DEFINE(UPPER, Name) PFNGL##UPPER##PROC Name = nullptr;
SM63_GL_FUNCTIONS(SM63_GL_DEFINE)
#undef SM63_GL_DEFINE

PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC RenderbufferStorageMultisample = nullptr;
PFNGLBLITFRAMEBUFFERNVPROC BlitFramebuffer = nullptr;

namespace {
bool es = false;

void* lookup(const char* name) {
    void* f = SDL_GL_GetProcAddress(name);
    if (!f) f = SDL_GL_GetProcAddress((std::string(name) + "EXT").c_str());
    return f;
}
}  // namespace

bool load(const char*& missing) {
#define SM63_GL_LOAD(UPPER, Name)                                                       \
    Name = reinterpret_cast<PFNGL##UPPER##PROC>(lookup("gl" #Name));                \
    if (!Name) {                                                                        \
        missing = "gl" #Name;                                                           \
        return false;                                                                   \
    }
    SM63_GL_FUNCTIONS(SM63_GL_LOAD)
#undef SM63_GL_LOAD
    RenderbufferStorageMultisample =
        reinterpret_cast<PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC>(lookup("glRenderbufferStorageMultisample"));
    BlitFramebuffer = reinterpret_cast<PFNGLBLITFRAMEBUFFERNVPROC>(lookup("glBlitFramebuffer"));
    const char* version = reinterpret_cast<const char*>(GetString(GL_VERSION));
    es = version && std::strstr(version, "OpenGL ES");
    return true;
}

bool isEs() { return es; }

namespace {
GLuint compile(GLenum type, const char* source) {
    // GLSL ES 1.00 sources; desktop GLSL 1.20 accepts them once precision qualifiers are
    // defined away.
    std::string full = es ? std::string("#version 100\nprecision highp float;\n")
                          : std::string("#version 120\n#define highp\n#define mediump\n#define lowp\n");
    full += source;
    GLuint s = CreateShader(type);
    const char* src = full.c_str();
    ShaderSource(s, 1, &src, nullptr);
    CompileShader(s);
    GLint ok = 0;
    GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        GetShaderInfoLog(s, sizeof log, nullptr, log);
        std::fprintf(stderr, "shader compile failed:\n%s\n", log);
        DeleteShader(s);
        return 0;
    }
    return s;
}
}  // namespace

GLuint program(const char* vertex, const char* fragment, const std::initializer_list<const char*>& attribs) {
    GLuint vs = compile(GL_VERTEX_SHADER, vertex), fs = compile(GL_FRAGMENT_SHADER, fragment);
    if (!vs || !fs) return 0;
    GLuint p = CreateProgram();
    AttachShader(p, vs);
    AttachShader(p, fs);
    GLuint i = 0;
    for (const char* a : attribs) BindAttribLocation(p, i++, a);
    LinkProgram(p);
    DeleteShader(vs);
    DeleteShader(fs);
    GLint ok = 0;
    GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        GetProgramInfoLog(p, sizeof log, nullptr, log);
        std::fprintf(stderr, "shader link failed:\n%s\n", log);
        DeleteProgram(p);
        return 0;
    }
    return p;
}

}  // namespace gl
