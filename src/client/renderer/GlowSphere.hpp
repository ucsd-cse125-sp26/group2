/// @file GlowSphere.hpp
/// @brief Procedural UV sphere with emissive material for bloom testing.

#pragma once

#include "ModelLoader.hpp"

#include <cmath>
#include <glm/glm.hpp>
#include <numbers>

/// @brief Generate a procedural UV sphere as a LoadedModel with a bright
///        emissive material suitable for testing bloom / HDR glow.
///
/// The sphere is centred at the origin with the given radius.  Its material
/// uses a high emissiveFactor (well above luminance 1.0) so the bloom
/// downsample pass extracts the glow automatically.  A 1×1 white texture
/// is included as the emissive map to prevent the PBR shader from
/// multiplying the factor by the black fallback texture.
///
/// @param stacks   Number of latitude subdivisions (default 32).
/// @param slices   Number of longitude subdivisions (default 32).
/// @param radius   Sphere radius in world units (default 1.0).
/// @param emissiveColor  HDR emissive colour (values > 1 trigger bloom).
/// @return A ready-to-upload LoadedModel.
inline LoadedModel
createGlowSphere(int stacks = 32, int slices = 32, float radius = 1.0f, glm::vec3 emissiveColor = {10.0f, 6.0f, 2.0f})
{
    LoadedModel model;
    MeshData mesh;

    // ── Vertices ──
    // UV sphere: stacks (latitude) × slices (longitude).
    mesh.vertices.reserve(static_cast<size_t>((stacks + 1) * (slices + 1)));

    for (int i = 0; i <= stacks; ++i) {
        const float phi = std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(stacks);
        const float sinPhi = std::sin(phi);
        const float cosPhi = std::cos(phi);

        for (int j = 0; j <= slices; ++j) {
            const float theta = 2.0f * std::numbers::pi_v<float> * static_cast<float>(j) / static_cast<float>(slices);
            const float sinTheta = std::sin(theta);
            const float cosTheta = std::cos(theta);

            const glm::vec3 normal{sinPhi * cosTheta, cosPhi, sinPhi * sinTheta};
            const glm::vec3 position = normal * radius;
            const glm::vec2 uv{static_cast<float>(j) / static_cast<float>(slices),
                               static_cast<float>(i) / static_cast<float>(stacks)};

            // Tangent = ∂P/∂θ normalised (longitude direction).
            // At the poles sinPhi ≈ 0, so the tangent degenerates — pick an
            // arbitrary perpendicular instead.
            glm::vec3 tangent;
            if (sinPhi > 1e-5f)
                tangent = glm::normalize(glm::vec3{-sinTheta, 0.0f, cosTheta});
            else
                tangent = glm::vec3{1.0f, 0.0f, 0.0f};

            mesh.vertices.push_back(ModelVertex{
                .position = position,
                .normal = normal,
                .texCoord = uv,
                .tangent = glm::vec4(tangent, 1.0f),
            });
        }
    }

    // ── Indices ──
    // Two triangles per quad, winding CCW from outside.
    mesh.indices.reserve(static_cast<size_t>(stacks * slices * 6));

    for (int i = 0; i < stacks; ++i) {
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

    // ── Material ──
    // Bright emissive (HDR values) triggers bloom.  Low roughness gives a
    // smooth, slightly specular surface so it picks up some environment
    // reflection on top of the glow.
    mesh.material.baseColorFactor = {1.0f, 1.0f, 1.0f, 1.0f};
    mesh.material.emissiveFactor = glm::vec4(emissiveColor, 0.0f);
    mesh.material.metallicFactor = 0.0f;
    mesh.material.roughnessFactor = 0.3f;
    mesh.material.aoStrength = 1.0f;
    mesh.material.normalScale = 1.0f;
    mesh.material.alphaMode = AlphaMode::Opaque;

    // 1×1 white emissive texture — the PBR shader multiplies emissiveFactor
    // by texEmissive, which falls back to the black fallback when no texture
    // is present.  Including a white pixel ensures the factor survives.
    TextureData whiteTex;
    whiteTex.pixels = {255, 255, 255, 255};
    whiteTex.width = 1;
    whiteTex.height = 1;
    whiteTex.isSRGB = true;

    model.textures.push_back(std::move(whiteTex));
    mesh.emissiveTexIndex = 0; // Points to the white texture above.

    model.meshes.push_back(std::move(mesh));
    return model;
}
