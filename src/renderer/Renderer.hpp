#pragma once

#include <SDL3/SDL.h>

#include <glm/glm.hpp>

class Renderer
{
public:
    bool init(SDL_Renderer* renderer);
    void drawFrame();
    void quit();

private:
    SDL_Renderer* renderer = nullptr;
    float angle = 0.0f;

    // x, y, z   --- (- left to + right), (- bottom to + top), (- back to + front)
    glm::vec3 vertices[8] = {
        glm::vec3(-0.5f, -0.5f, 0.5f),  // 0 front bottom left
        glm::vec3(-0.5f, 0.5f, 0.5f),   // 1 front top left
        glm::vec3(0.5f, 0.5f, 0.5f),    // 2 front top right
        glm::vec3(0.5f, -0.5f, 0.5f),   // 3 front bottom right
        glm::vec3(-0.5f, -0.5f, -0.5f), // 4 back bottom left
        glm::vec3(-0.5f, 0.5f, -0.5f),  // 5 back top left
        glm::vec3(0.5f, 0.5f, -0.5f),   // 6 back top right
        glm::vec3(0.5f, -0.5f, -0.5f)   // 7 back bottom right
    };

    // Each face is 2 triangles -> 6 vertices
    int triangles[36] = {
        0, 1, 2, 0, 2, 3, // front
        4, 6, 5, 4, 7, 6, // back
        4, 5, 1, 4, 1, 0, // left
        3, 2, 6, 3, 6, 7, // right
        1, 5, 6, 1, 6, 2, // top
        4, 0, 3, 4, 3, 7  // bottom
    };

    // normals, same x, y, z as vertices
    glm::vec3 normals[12] = {
        glm::vec3(0.0f, 0.0f, 1.0f),  // front
        glm::vec3(0.0f, 0.0f, 1.0f),  // front
        glm::vec3(0.0f, 0.0f, -1.0f), // back
        glm::vec3(0.0f, 0.0f, -1.0f), // back
        glm::vec3(-1.0f, 0.0f, 0.0f), // left
        glm::vec3(-1.0f, 0.0f, 0.0f), // left
        glm::vec3(1.0f, 0.0f, 0.0f),  // right
        glm::vec3(1.0f, 0.0f, 0.0f),  // right
        glm::vec3(0.0f, 1.0f, 0.0f),  // top
        glm::vec3(0.0f, 1.0f, 0.0f),  // top
        glm::vec3(0.0f, -1.0f, 0.0f), // bottom
        glm::vec3(0.0f, -1.0f, 0.0f), // bottom
    };

    glm::vec3 eye = glm::vec3(0.0f, 0.0f, 3.0f);
    glm::vec3 target = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    float fovy = glm::radians(60.0f);
    // camera.aspect_default?
    float near = 0.1f;
    float far = 100.0f;
};