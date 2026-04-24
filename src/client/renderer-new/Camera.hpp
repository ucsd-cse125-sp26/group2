#pragma once

#include <glm/glm.hpp>

class NewCamera
{
public:
    NewCamera();

    // Camera perspective properties (modifies projection matrix)
    void setAspect(float width, float height);
    void setFov(float fovyDegrees);
    void setZNear(float zNear);
    void setZFar(float zFar);

    // Sets the position of the camera in the world (modifies view matrix)
    void setEye(glm::vec3 eye);
    // Sets the view direction from the eye based on the pitch, yaw, and roll
    // (all in radians). Roll rotates the up vector around the forward axis,
    // used for camera tilt during wallruns.
    void setTarget(float pitch, float yaw, float roll);
    // Sets the up direction of the camera explicitly (overrides whatever
    // setTarget produced). Useful if you ever need a non-gravity-aligned up.
    void setUp(glm::vec3 up);

    // Matrix computation, always run after using any of the set methods above.
    void computeViewProjectionMatrix();

    /// @brief Return the combined view-projection matrix.
    [[nodiscard]] glm::mat4 getViewProjectionMatrix() const { return view_projection_; }

private:
    glm::vec3 eye_{0.0f, 0.0f, 3.0f};
    glm::vec3 target_{0.0f, 0.0f, 0.0f};
    glm::vec3 up_{0.0f, 1.0f, 0.0f};

    // Near/far are sized for Quake units.
    float fovy_ = glm::radians(60.0f);
    float aspect_ = 1.0f;
    float zNear_ = 5.0f;    ///< Near clip (Quake units); 5 ≈ half a foot.
    float zFar_ = 15000.0f; ///< Far clip; covers the 4 000-unit play area with margin.

    glm::mat4 view_projection_{1.0f};
};