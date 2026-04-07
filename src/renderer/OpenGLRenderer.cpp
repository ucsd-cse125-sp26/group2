#include "OpenGLRenderer.hpp"

#include <SDL3/SDL.h>

#include <glad/glad.h>

// ---------------------------------------------------------------------------
// Inline GLSL shaders — OpenGL 4.1 core profile.
// Positions and colours are embedded in the shader via gl_VertexID so
// no vertex buffer is required.
// ---------------------------------------------------------------------------

static const char* k_vertSrc = R"glsl(
#version 410 core
out vec4 vColor;
void main() {
    const vec2 pos[3] = vec2[3](
        vec2( 0.0,  0.5),
        vec2( 0.5, -0.5),
        vec2(-0.5, -0.5)
    );
    const vec4 col[3] = vec4[3](
        vec4(1.0, 0.0, 0.0, 1.0),
        vec4(0.0, 1.0, 0.0, 1.0),
        vec4(0.0, 0.0, 1.0, 1.0)
    );
    gl_Position = vec4(pos[gl_VertexID], 0.0, 1.0);
    vColor      = col[gl_VertexID];
}
)glsl";

static const char* k_fragSrc = R"glsl(
#version 410 core
in  vec4 vColor;
out vec4 fragColor;
void main() {
    fragColor = vColor;
}
)glsl";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{
// Compile one shader stage and return its handle (0 on failure).
GLuint compileShader(GLenum type, const char* src)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        SDL_Log("OpenGLRenderer: shader compile error: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}
} // namespace

// ---------------------------------------------------------------------------
// IRenderer implementation
// ---------------------------------------------------------------------------

bool OpenGLRenderer::init(SDL_Window* win)
{
    window = win;

    context = SDL_GL_CreateContext(window);
    if (!context) {
        SDL_Log("OpenGLRenderer: SDL_GL_CreateContext failed: %s", SDL_GetError());
        return false;
    }
    SDL_GL_MakeCurrent(window, context);
    SDL_GL_SetSwapInterval(1); // vsync

    // Load all OpenGL function pointers via glad.
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        SDL_Log("OpenGLRenderer: gladLoadGLLoader failed");
        return false;
    }
    SDL_Log("OpenGLRenderer: OpenGL %s  GPU: %s", glGetString(GL_VERSION), glGetString(GL_RENDERER));

    // Compile and link the shader program.
    GLuint vert = compileShader(GL_VERTEX_SHADER, k_vertSrc);
    GLuint frag = compileShader(GL_FRAGMENT_SHADER, k_fragSrc);
    if (!vert || !frag) {
        glDeleteShader(vert);
        glDeleteShader(frag);
        return false;
    }

    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vert);
    glAttachShader(shaderProgram, frag);
    glLinkProgram(shaderProgram);
    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint linked = 0;
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[512];
        glGetProgramInfoLog(shaderProgram, sizeof(log), nullptr, log);
        SDL_Log("OpenGLRenderer: program link error: %s", log);
        glDeleteProgram(shaderProgram);
        shaderProgram = 0;
        return false;
    }

    // An empty VAO is required by OpenGL 4.1 core profile even with no vertex
    // attributes (positions come from gl_VertexID inside the shader).
    glGenVertexArrays(1, &vao);

    return true;
}

void OpenGLRenderer::draw(SDL_GPURenderPass* /*unused*/)
{
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    glViewport(0, 0, w, h);

    glClearColor(0.10f, 0.10f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(shaderProgram);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void OpenGLRenderer::shutdown()
{
    if (vao)
        glDeleteVertexArrays(1, &vao);
    if (shaderProgram)
        glDeleteProgram(shaderProgram);
    if (context)
        SDL_GL_DestroyContext(context);

    vao = 0;
    shaderProgram = 0;
    context = nullptr;
    window = nullptr;
}
