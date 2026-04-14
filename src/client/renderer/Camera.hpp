#pragma once

#include <glm/glm.hpp>

class Camera
{
public:
    Camera();

    // Camera perspective properties
    void setAspect(float width, float height);
    void setFov(float fovyDegrees);
    void setZNear(float zNear);
    void setZFar(float zFar);

    // Sets the position of the camera in the world
    void setEye(glm::vec3 eye);
    // Sets the view direction from the eye based on the pitch and yaw (both radians)
    void setTarget(float pitch, float yaw, float _roll);
    // Sets the up direction of the camera (for gravity stuff?)
    void setUp(glm::vec3 up);

    void computeViewMatrix();
    void computeProjectionMatrix();

    [[nodiscard]] const glm::mat4& getViewMatrix() const { return view_; }
    [[nodiscard]] const glm::mat4& getProjectionMatrix() const { return projection_; }
    [[nodiscard]] const glm::vec3& getEye() const { return eye_; }
    [[nodiscard]] const glm::vec3 getRight() const;
    [[nodiscard]] const glm::vec3 getForward() const;
    [[nodiscard]] const glm::vec3& getUp() const { return up_; }
    /// @brief Return the combined view-projection matrix.
    [[nodiscard]] glm::mat4 getViewProjection() const { return projection_ * view_; }
    /// @brief Return the vertical field of view in degrees.
    [[nodiscard]] float getFovy() const { return fovy_; }
    /// @brief Return the aspect ratio (width / height).
    [[nodiscard]] float getAspect() const { return aspect_; }
    /// @brief Return the near clip plane distance.
    [[nodiscard]] float getNear() const { return zNear_; }

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