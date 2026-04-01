#pragma once
#include <glm/glm.hpp>

// Must match layout(set=2, binding=0) in scene.vert
// std140 rules: mat4 is 16-byte aligned; total = 128 bytes
struct alignas(16) SceneUniforms
{
    glm::mat4 viewProj;
    glm::mat4 model;
};
