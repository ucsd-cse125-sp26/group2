#include "Renderer.hpp"

#include <algorithm>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <vector>

bool Renderer::init(SDL_Renderer* renderer)
{
    this->renderer = renderer;
    return true;
}

void Renderer::drawFrame()
{
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    int w, h;
    SDL_GetWindowSize(SDL_GetRenderWindow(renderer), &w, &h);
    float aspect = static_cast<float>(w) / static_cast<float>(h);

    glm::mat4 view = glm::lookAt(eye, target, up);              // Camera space -> Screen space
    glm::mat4 proj = glm::perspective(fovy, aspect, near, far); // World space -> Camera space
    glm::mat4 model = glm::mat4(1.0f);                          // Model space -> World space

    static Uint64 last = SDL_GetTicks();
    Uint64 now = SDL_GetTicks();

    float dt = (now - last) / 1000.0f;
    last = now;

    angle += dt * 0.5f; // radians per second
    model = glm::rotate(model, angle, glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, angle * 0.7f, glm::vec3(1.0f, 0.0f, 0.0f));

    glm::mat4 mvp = proj * view * model;

    struct ProjectedVertex
    {
        SDL_FPoint screen;
        float depth;
        bool valid;
    };

    ProjectedVertex projected[8];

    // Project all 8 cube vertices to screen
    for (int i = 0; i < 8; i++) {
        glm::vec4 clip = mvp * glm::vec4(vertices[i], 1.0f);

        // Avoid dividing by zero or drawing points behind the camera
        if (clip.w <= 0.0f) {
            projected[i].valid = false;
            continue;
        }

        glm::vec3 ndc = glm::vec3(clip) / clip.w; // range roughly [-1, 1]

        projected[i].valid = true;
        projected[i].depth = ndc.z;

        // NDC -> screen
        projected[i].screen.x = (ndc.x * 0.5f + 0.5f) * static_cast<float>(w);
        projected[i].screen.y = (-ndc.y * 0.5f + 0.5f) * static_cast<float>(h);
    }

    struct TriangleToDraw
    {
        SDL_Vertex verts[3];
        float avgDepth;
    };

    std::vector<TriangleToDraw> drawList;
    drawList.reserve(12);

    for (int t = 0; t < 12; t++) {
        int i0 = triangles[t * 3 + 0];
        int i1 = triangles[t * 3 + 1];
        int i2 = triangles[t * 3 + 2];

        if (!projected[i0].valid || !projected[i1].valid || !projected[i2].valid)
            continue;

        TriangleToDraw tri{};

        tri.verts[0].position = projected[i0].screen;
        tri.verts[1].position = projected[i1].screen;
        tri.verts[2].position = projected[i2].screen;

        // Give each face pair a different color
        SDL_FColor color{};
        switch (t / 2) {
        case 0:
            color = SDL_FColor{1.0f, 0.2f, 0.2f, 1.0f};
            break; // front
        case 1:
            color = SDL_FColor{0.2f, 1.0f, 0.2f, 1.0f};
            break; // back
        case 2:
            color = SDL_FColor{0.2f, 0.2f, 1.0f, 1.0f};
            break; // left
        case 3:
            color = SDL_FColor{1.0f, 1.0f, 0.2f, 1.0f};
            break; // right
        case 4:
            color = SDL_FColor{1.0f, 0.2f, 1.0f, 1.0f};
            break; // top
        default:
            color = SDL_FColor{0.2f, 1.0f, 1.0f, 1.0f};
            break; // bottom
        }

        tri.verts[0].color = color;
        tri.verts[1].color = color;
        tri.verts[2].color = color;

        tri.verts[0].tex_coord = SDL_FPoint{0.0f, 0.0f};
        tri.verts[1].tex_coord = SDL_FPoint{0.0f, 0.0f};
        tri.verts[2].tex_coord = SDL_FPoint{0.0f, 0.0f};

        tri.avgDepth = (projected[i0].depth + projected[i1].depth + projected[i2].depth) / 3.0f;

        drawList.push_back(tri);
    }

    // Painter's algorithm: farther triangles first
    std::sort(drawList.begin(), drawList.end(), [](const TriangleToDraw& a, const TriangleToDraw& b) {
        return a.avgDepth > b.avgDepth;
    });

    for (const TriangleToDraw& tri : drawList) {
        SDL_RenderGeometry(renderer, nullptr, tri.verts, 3, nullptr, 0);
    }

    SDL_RenderPresent(renderer);
}

void Renderer::quit()
{
    SDL_DestroyRenderer(renderer);
    renderer = nullptr;
}
