/// @file Inertia.hpp
/// @brief Analytical inertia tensors for the primitive collision shapes.
///
/// Returns local-space *inverse* inertia tensors directly because every
/// runtime use multiplies by the inverse (`I^-1 * tau`).  The matrices are
/// diagonal for the primitives we ship (box, sphere, capsule, cylinder)
/// since their principal axes are aligned with local coordinates.

#pragma once

#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>

namespace physics::inertia
{

/// @brief Local-space inverse inertia tensor of a solid box of mass `m` and
/// full extents (= 2 * halfExtents).
[[nodiscard]] inline glm::mat3 boxInvInertia(float mass, glm::vec3 halfExtents) noexcept
{
    if (mass <= 0.0f)
        return glm::mat3(0.0f); // static body
    // I_xx = (1/12) m (h² + d²)  where h = 2*hY, d = 2*hZ
    // I_yy = (1/12) m (w² + d²)
    // I_zz = (1/12) m (w² + h²)
    const float w2 = (halfExtents.x * 2.0f) * (halfExtents.x * 2.0f);
    const float h2 = (halfExtents.y * 2.0f) * (halfExtents.y * 2.0f);
    const float d2 = (halfExtents.z * 2.0f) * (halfExtents.z * 2.0f);
    const float k = 12.0f / mass;
    return glm::mat3{
        glm::vec3{k / (h2 + d2), 0.0f, 0.0f},
        glm::vec3{0.0f, k / (w2 + d2), 0.0f},
        glm::vec3{0.0f, 0.0f, k / (w2 + h2)},
    };
}

/// @brief Local-space inverse inertia tensor of a solid sphere of mass `m`
/// and radius `r`.
[[nodiscard]] inline glm::mat3 sphereInvInertia(float mass, float radius) noexcept
{
    if (mass <= 0.0f)
        return glm::mat3(0.0f);
    const float i = (2.0f / 5.0f) * mass * radius * radius;
    return glm::mat3{
        glm::vec3{1.0f / i, 0.0f, 0.0f},
        glm::vec3{0.0f, 1.0f / i, 0.0f},
        glm::vec3{0.0f, 0.0f, 1.0f / i},
    };
}

/// @brief Local-space inverse inertia tensor of a capsule (cylinder + 2
/// hemispherical caps) with vertical axis (+Y), cylinder half-height `h`
/// and radius `r`, total mass `m`.
[[nodiscard]] inline glm::mat3 capsuleInvInertia(float mass, float radius, float halfHeight) noexcept
{
    if (mass <= 0.0f)
        return glm::mat3(0.0f);

    // Split mass proportionally by volume.  Cylinder: π r² (2h).
    // Hemispheres total: (4/3) π r³.
    const float r = radius;
    const float h = halfHeight;
    const float vCyl = 2.0f * h * (r * r);
    const float vSph = (4.0f / 3.0f) * (r * r * r);
    const float vTot = vCyl + vSph;
    if (vTot <= 0.0f)
        return glm::mat3(0.0f);

    const float mCyl = mass * (vCyl / vTot);
    const float mSph = mass * (vSph / vTot);

    // Cylinder inertia about its symmetry axis (Y):
    //   I_yy_cyl = (1/2) m r²
    //   I_xx_cyl = I_zz_cyl = (1/12) m (3 r² + (2h)²)
    const float iYcyl = 0.5f * mCyl * r * r;
    const float iXcyl = (1.0f / 12.0f) * mCyl * (3.0f * r * r + (2.0f * h) * (2.0f * h));

    // Two hemispheres = full sphere (parallel-axis to caps offset by ±h):
    //   I_yy_sph = (2/5) m r²
    //   I_xx_sph = (2/5) m r² + m * h²    (parallel-axis applied at cap centre)
    const float iYsph = (2.0f / 5.0f) * mSph * r * r;
    const float iXsph = iYsph + mSph * h * h;

    const float iY = iYcyl + iYsph;
    const float iX = iXcyl + iXsph;

    return glm::mat3{
        glm::vec3{1.0f / iX, 0.0f, 0.0f},
        glm::vec3{0.0f, 1.0f / iY, 0.0f},
        glm::vec3{0.0f, 0.0f, 1.0f / iX},
    };
}

} // namespace physics::inertia
