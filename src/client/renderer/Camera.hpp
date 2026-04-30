/// @file Camera.hpp
/// @brief First-person camera with perspective projection and Vulkan-convention matrices.

#pragma once

#include <glm/glm.hpp>

/// @brief First-person camera managing view and projection matrices.
class Camera
{
public:
    /// @brief Construct with default eye position, 60 degree FOV, and Quake-unit near/far.
    Camera();

    /// @brief Update aspect ratio from viewport dimensions and recompute projection.
    /// @param width  Viewport width in pixels.
    /// @param height Viewport height in pixels.
    void setAspect(float width, float height);
    /// @brief Set vertical field of view in degrees and recompute projection.
    /// @param fovyDegrees Vertical FOV in degrees.
    void setFov(float fovyDegrees);
    /// @brief Set the near clip plane distance (Quake units) and recompute projection.
    /// @param zNear Near clip plane distance.
    void setZNear(float zNear);
    /// @brief Set the far clip plane distance (Quake units) and recompute projection.
    /// @param zFar Far clip plane distance.
    void setZFar(float zFar);

    /// @brief Set camera world-space position and recompute view matrix.
    /// @param eye New camera position in world space.
    void setEye(glm::vec3 eye);
    /// @brief Set view direction from pitch, yaw, and roll (all radians).
    /// Roll rotates the up vector around the forward axis (wallrun tilt).
    /// @param pitch Vertical angle in radians (positive looks down).
    /// @param yaw   Horizontal angle in radians (0 faces +Z).
    /// @param roll  Roll angle in radians for camera tilt.
    void setTarget(float pitch, float yaw, float roll);
    /// @brief Override the up direction explicitly.
    /// Useful for non-gravity-aligned orientations.
    /// @param up New up direction vector.
    void setUp(glm::vec3 up);

    /// @brief Recompute the view matrix from current eye, target, and up.
    void computeViewMatrix();
    /// @brief Recompute the projection matrix from current FOV, aspect, near, far.
    /// @note Flips Y for Vulkan NDC convention.
    void computeProjectionMatrix();

    [[nodiscard]] const glm::mat4& getViewMatrix() const { return view_; }
    [[nodiscard]] const glm::mat4& getProjectionMatrix() const { return projection_; }
    [[nodiscard]] const glm::vec3& getEye() const { return eye_; }
    /// @brief Return the camera right vector (normalized cross of forward and up).
    [[nodiscard]] const glm::vec3 getRight() const;
    /// @brief Return the camera forward vector (normalized target - eye).
    [[nodiscard]] const glm::vec3 getForward() const;
    [[nodiscard]] const glm::vec3& getUp() const { return up_; }
    /// @brief Return the combined view-projection matrix.
    [[nodiscard]] glm::mat4 getViewProjection() const { return projection_ * view_; }
    /// @brief Return the vertical field of view in degrees.
    ///
    /// Internally fovy_ is stored in radians (matches glm::perspective and
    /// setFov's conversion).  Convert back here so the public API contract
    /// stays consistent with setFov(fovyDegrees).
    [[nodiscard]] float getFovy() const { return glm::degrees(fovy_); }
    /// @brief Return the aspect ratio (width / height).
    [[nodiscard]] float getAspect() const { return aspect_; }
    /// @brief Return the near clip plane distance.
    [[nodiscard]] float getNear() const { return zNear_; }
    /// @brief Return the far clip plane distance.
    [[nodiscard]] float getFar() const { return zFar_; }

    /// @brief Apply a sub-pixel jitter offset to the projection matrix.
    ///
    /// Used by SMAA T2x temporal supersampling.  Call **after**
    /// setAspect()/computeProjectionMatrix() so the base projection is
    /// established first.  The offset is in NDC units (already scaled by
    /// 2/resolution).
    /// @param jitterX Horizontal jitter in NDC.
    /// @param jitterY Vertical jitter in NDC.
    void applySubpixelJitter(float jitterX, float jitterY);

private:
    glm::vec3 eye_{0.0f, 0.0f, 3.0f};
    glm::vec3 target_{0.0f, 0.0f, 0.0f};
    glm::vec3 up_{0.0f, 1.0f, 0.0f};

    // Near/far are sized for Quake units.
    float fovy_ = glm::radians(60.0f);
    float aspect_ = 1.0f;
    float zNear_ = 5.0f;    ///< Near clip (Quake units); 5 ≈ half a foot.
    float zFar_ = 15000.0f; ///< Far clip; covers the 4 000-unit play area with margin.

    glm::mat4 view_{1.0f};
    glm::mat4 projection_{1.0f};
};