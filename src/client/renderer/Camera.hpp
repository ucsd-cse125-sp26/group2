/// @file Camera.hpp
/// @brief Perspective camera with view/projection matrix management.

#pragma once

#include <glm/glm.hpp>

/// @brief Perspective camera that computes view and projection matrices.
///
/// Stores eye/target/up and projection parameters. Matrices are recomputed
/// on every mutating call (setLookAt, setPerspective, rotateRight, etc.).
class Camera
{
public:
    Camera() = default;

    /// @brief Construct a camera with the given pose and projection.
    /// @param eye Camera world position.
    /// @param target World position the camera looks at.
    /// @param up World up direction.
    /// @param fovyDegrees Vertical field of view in degrees.
    /// @param aspectRatio Width / height.
    /// @param nearPlane Near clip plane distance.
    /// @param farPlane Far clip plane distance.
    Camera(glm::vec3 eye,
           glm::vec3 target,
           glm::vec3 up,
           float fovyDegrees,
           float aspectRatio,
           float nearPlane,
           float farPlane);

    /// @brief Reset all parameters to their construction-time defaults.
    void reset();

    /// @brief Update the aspect ratio and recompute matrices.
    /// @param aspectRatio New width / height.
    void setAspect(float aspectRatio);

    /// @brief Set all projection parameters and recompute matrices.
    /// @param fovyDegrees Vertical FOV in degrees.
    /// @param aspectRatio Width / height.
    /// @param nearPlane Near clip distance.
    /// @param farPlane Far clip distance.
    void setPerspective(float fovyDegrees, float aspectRatio, float nearPlane, float farPlane);

    /// @brief Set camera position and orientation, then recompute matrices.
    /// @param eyePos Camera world position.
    /// @param targetPos Look-at target.
    /// @param upDir World up direction.
    void setLookAt(glm::vec3 eyePos, glm::vec3 targetPos, glm::vec3 upDir);

    /// @brief Orbit the camera rightward around the target.
    /// @param degrees Rotation angle in degrees.
    void rotateRight(float degrees);

    /// @brief Orbit the camera upward around the target.
    /// @param degrees Rotation angle in degrees.
    void rotateUp(float degrees);

    /// @brief Recompute view and projection matrices from current parameters.
    void computeMatrices();

    /// @brief Return the view matrix.
    [[nodiscard]] const glm::mat4& getView() const { return view; }

    /// @brief Return the projection matrix.
    [[nodiscard]] const glm::mat4& getProjection() const { return proj; }

    /// @brief Return the combined view-projection matrix.
    [[nodiscard]] glm::mat4 getViewProjection() const { return proj * view; }

    /// @brief Return the camera eye position.
    [[nodiscard]] const glm::vec3& getEye() const { return eye; }

    /// @brief Return the look-at target position.
    [[nodiscard]] const glm::vec3& getTarget() const { return target; }

    /// @brief Return the camera up direction.
    [[nodiscard]] const glm::vec3& getUp() const { return up; }

    /// @brief World-space unit vector pointing from eye toward target.
    [[nodiscard]] glm::vec3 getForward() const;

    /// @brief World-space unit vector pointing to the camera's right.
    [[nodiscard]] glm::vec3 getRight() const;

    /// @brief Return the vertical field of view in degrees.
    [[nodiscard]] float getFovy() const { return fovy; }

    /// @brief Return the aspect ratio (width / height).
    [[nodiscard]] float getAspect() const { return aspect; }

    /// @brief Return the near clip plane distance.
    [[nodiscard]] float getNear() const { return nearPlane; }

    /// @brief Return the far clip plane distance.
    [[nodiscard]] float getFar() const { return farPlane; }

private:
    glm::vec3 eye{0.0f, 0.0f, 3.0f};
    glm::vec3 target{0.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};

    float fovy = 60.0f; ///< Vertical FOV in degrees.
    float aspect = 1.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;

    glm::vec3 eyeDefault{0.0f, 0.0f, 3.0f};
    glm::vec3 targetDefault{0.0f, 0.0f, 0.0f};
    glm::vec3 upDefault{0.0f, 1.0f, 0.0f};

    float fovyDefault = 60.0f;
    float aspectDefault = 1.0f;
    float nearDefault = 0.1f;
    float farDefault = 100.0f;

    glm::mat4 view{1.0f};
    glm::mat4 proj{1.0f};
};