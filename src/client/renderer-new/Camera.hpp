/// @file Camera.hpp
/// @brief Camera class for the new renderer with combined view-projection matrix.

#pragma once

#include <glm/glm.hpp>

#define NUM_FRUSTUM_PLANES 6

struct FrustumPlanes
{
    glm::vec4 left;
    glm::vec4 right;
    glm::vec4 bottom;
    glm::vec4 top;
    glm::vec4 near;
    glm::vec4 far;
};

/// @brief Camera for the new renderer, combining view and projection into one matrix.
class NewCamera
{
public:
    /// @brief Construct a NewCamera with default perspective and position.
    NewCamera();

    /// @brief Set the aspect ratio from pixel dimensions (modifies projection matrix).
    /// @param width  Viewport width in pixels.
    /// @param height Viewport height in pixels.
    void setAspect(float width, float height);

    /// @brief Set the vertical field of view in degrees.
    /// @param fovyDegrees Vertical FOV in degrees.
    void setFov(float fovyDegrees);

    /// @brief Set the near clip plane distance.
    /// @param zNear Near clip distance.
    void setZNear(float zNear);

    /// @brief Set the far clip plane distance.
    /// @param zFar Far clip distance.
    void setZFar(float zFar);

    /// @brief Set the camera eye position in world space (modifies view matrix).
    /// @param eye World-space position.
    void setEye(glm::vec3 eye);

    /// @brief Set the view direction from pitch, yaw, and roll (all in radians).
    ///
    /// Roll rotates the up vector around the forward axis, used for camera
    /// tilt during wallruns.
    /// @param pitch Pitch angle in radians.
    /// @param yaw   Yaw angle in radians.
    /// @param roll  Roll angle in radians.
    void setTarget(float pitch, float yaw, float roll);

    /// @brief Explicitly set the camera up direction, overriding setTarget's up vector.
    /// @param up The desired up direction.
    void setUp(glm::vec3 up);

    /// @brief Recompute the combined view-projection matrix from current parameters.
    void computeViewProjectionMatrix();

    static FrustumPlanes gribbHartmannFrustumPlanes(const glm::mat4 &viewProjectionMat);

    /// @brief Return the combined view-projection matrix.
    [[nodiscard]] glm::mat4 getViewProjectionMatrix() const { return view_projection_; }
    [[nodiscard]] FrustumPlanes getViewProjectionFrustumPlane() const { return viewProjectionFrustumPlanes_; }
    [[nodiscard]] glm::mat4 getViewMatrix() const { return view_; }
    [[nodiscard]] glm::mat4 getProjectionMatrix() const { return projection_; }

    /// @brief Alias for `getViewProjectionMatrix()` — kept short for callsite ergonomics.
    [[nodiscard]] glm::mat4 getViewProjection() const { return view_projection_; }

    [[nodiscard]] const glm::vec3& getEye() const { return eye_; }
    [[nodiscard]] glm::vec3 getForward() const;
    [[nodiscard]] glm::vec3 getRight() const;
    [[nodiscard]] const glm::vec3& getUp() const { return up_; }

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
    glm::mat4 view_projection_{1.0f};

    FrustumPlanes viewProjectionFrustumPlanes_;
};
