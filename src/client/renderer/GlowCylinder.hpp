/// @file GlowCylinder.hpp
/// @brief Procedural cylinder with emissive material for bloom beam testing.

#pragma once

#include "ModelLoader.hpp"

#include <cmath>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <numbers>

/// @brief Generate a procedural unit cylinder as a LoadedModel.
///
/// The cylinder is centred at the origin, extends from y = -0.5 to y = +0.5,
/// with radius 1.  At render time a world transform scales, rotates, and
/// translates it to connect any two world-space points.
///
/// @param slices         Longitudinal subdivisions around the circumference.
/// @param segments       Height subdivisions along the barrel.
/// @param emissiveColor  HDR emissive colour (values > 1 trigger bloom).
/// @return A ready-to-upload LoadedModel.
inline LoadedModel createGlowCylinder(int slices = 24, int segments = 1, glm::vec3 emissiveColor = {8.0f, 2.0f, 10.0f})
{
    LoadedModel model;
    MeshData mesh;

    const float pi2 = 2.0f * std::numbers::pi_v<float>;

    // ── Side vertices ──
    // (segments+1) rings of (slices+1) vertices each.
    for (int i = 0; i <= segments; ++i) {
        const float y = static_cast<float>(i) / static_cast<float>(segments) - 0.5f;
        const float v = static_cast<float>(i) / static_cast<float>(segments);

        for (int j = 0; j <= slices; ++j) {
            const float theta = pi2 * static_cast<float>(j) / static_cast<float>(slices);
            const float ct = std::cos(theta);
            const float st = std::sin(theta);

            const glm::vec3 pos{ct, y, st};
            const glm::vec3 normal{ct, 0.0f, st};
            const glm::vec2 uv{static_cast<float>(j) / static_cast<float>(slices), v};
            // Tangent runs along the height (Y axis).
            const glm::vec4 tangent{0.0f, 1.0f, 0.0f, 1.0f};

            mesh.vertices.push_back(ModelVertex{
                .position = pos,
                .normal = normal,
                .texCoord = uv,
                .tangent = tangent,
            });
        }
    }

    // ── Side indices ──
    for (int i = 0; i < segments; ++i) {
        for (int j = 0; j < slices; ++j) {
            const uint32_t a = static_cast<uint32_t>(i * (slices + 1) + j);
            const uint32_t b = a + static_cast<uint32_t>(slices + 1);

            mesh.indices.push_back(a);
            mesh.indices.push_back(b);
            mesh.indices.push_back(a + 1);

            mesh.indices.push_back(a + 1);
            mesh.indices.push_back(b);
            mesh.indices.push_back(b + 1);
        }
    }

    // ── Cap vertices & indices ──
    // Top cap (y = +0.5) and bottom cap (y = -0.5).
    for (int cap = 0; cap < 2; ++cap) {
        const float y = (cap == 0) ? 0.5f : -0.5f;
        const glm::vec3 capNormal{0.0f, (cap == 0) ? 1.0f : -1.0f, 0.0f};
        const auto centerIdx = static_cast<uint32_t>(mesh.vertices.size());

        // Centre vertex.
        mesh.vertices.push_back(ModelVertex{
            .position = {0.0f, y, 0.0f},
            .normal = capNormal,
            .texCoord = {0.5f, 0.5f},
            .tangent = {1.0f, 0.0f, 0.0f, 1.0f},
        });

        // Ring vertices.
        const auto ringStart = static_cast<uint32_t>(mesh.vertices.size());
        for (int j = 0; j <= slices; ++j) {
            const float theta = pi2 * static_cast<float>(j) / static_cast<float>(slices);
            const float ct = std::cos(theta);
            const float st = std::sin(theta);

            mesh.vertices.push_back(ModelVertex{
                .position = {ct, y, st},
                .normal = capNormal,
                .texCoord = {ct * 0.5f + 0.5f, st * 0.5f + 0.5f},
                .tangent = {1.0f, 0.0f, 0.0f, 1.0f},
            });
        }

        // Fan triangles.
        for (int j = 0; j < slices; ++j) {
            const uint32_t r0 = ringStart + static_cast<uint32_t>(j);
            const uint32_t r1 = r0 + 1;
            if (cap == 0) {
                // Top cap: wind CCW from outside (looking down +Y).
                mesh.indices.push_back(centerIdx);
                mesh.indices.push_back(r0);
                mesh.indices.push_back(r1);
            } else {
                // Bottom cap: wind CCW from outside (looking up -Y).
                mesh.indices.push_back(centerIdx);
                mesh.indices.push_back(r1);
                mesh.indices.push_back(r0);
            }
        }
    }

    // ── Material ──
    mesh.material.baseColorFactor = {1.0f, 1.0f, 1.0f, 1.0f};
    mesh.material.emissiveFactor = glm::vec4(emissiveColor, 0.0f);
    mesh.material.metallicFactor = 0.0f;
    mesh.material.roughnessFactor = 0.2f;
    mesh.material.aoStrength = 1.0f;
    mesh.material.normalScale = 1.0f;
    mesh.material.alphaMode = AlphaMode::Opaque;

    // 1x1 white emissive texture (same trick as GlowSphere).
    TextureData whiteTex;
    whiteTex.pixels = {255, 255, 255, 255};
    whiteTex.width = 1;
    whiteTex.height = 1;
    whiteTex.isSRGB = true;

    model.textures.push_back(std::move(whiteTex));
    mesh.emissiveTexIndex = 0;

    model.meshes.push_back(std::move(mesh));
    return model;
}

/// @brief Build a world transform that places a unit cylinder (Y-axis, height 1,
///        radius 1) so it connects `start` to `end` with the given radius.
///
/// The resulting matrix: translate to midpoint → rotate Y onto beam direction → scale.
inline glm::mat4 cylinderTransform(glm::vec3 start, glm::vec3 end, float radius)
{
    const glm::vec3 mid = (start + end) * 0.5f;
    const glm::vec3 delta = end - start;
    const float len = glm::length(delta);

    if (len < 1e-6f)
        return glm::translate(glm::mat4(1.0f), mid);

    const glm::vec3 dir = delta / len;

    // Build a rotation that maps +Y to `dir`.
    // Find a perpendicular axis via cross product with Y.
    const glm::vec3 up{0.0f, 1.0f, 0.0f};
    glm::vec3 axis = glm::cross(up, dir);
    const float sinAngle = glm::length(axis);
    const float cosAngle = glm::dot(up, dir);

    glm::mat4 rot(1.0f);
    if (sinAngle > 1e-6f) {
        axis /= sinAngle; // normalise
        const float angle = std::atan2(sinAngle, cosAngle);
        rot = glm::rotate(glm::mat4(1.0f), angle, axis);
    } else if (cosAngle < 0.0f) {
        // dir ≈ -Y → 180° around any perpendicular axis.
        rot = glm::rotate(glm::mat4(1.0f), std::numbers::pi_v<float>, glm::vec3{1.0f, 0.0f, 0.0f});
    }

    // Translate → rotate → scale(radius, length, radius).
    glm::mat4 m = glm::translate(glm::mat4(1.0f), mid);
    m *= rot;
    m = glm::scale(m, glm::vec3(radius, len, radius));
    return m;
}
