#pragma once

#include <glm/glm.hpp>

class Camera
{
public:
    Camera() = default;

    Camera(glm::vec3 eye,
           glm::vec3 target,
           glm::vec3 up,
           float fovyDegrees,
           float aspectRatio,
           float nearPlane,
           float farPlane);

    void reset();

    void setAspect(float aspectRatio);
    void setPerspective(float fovyDegrees, float aspectRatio, float nearPlane, float farPlane);
    void setLookAt(glm::vec3 eyePos, glm::vec3 targetPos, glm::vec3 upDir);

    void rotateRight(float degrees);
    void rotateUp(float degrees);

    void computeMatrices();

    [[nodiscard]] const glm::mat4& getView() const { return view; }
    [[nodiscard]] const glm::mat4& getProjection() const { return proj; }

    [[nodiscard]] const glm::vec3& getEye() const { return eye; }
    [[nodiscard]] const glm::vec3& getTarget() const { return target; }
    [[nodiscard]] const glm::vec3& getUp() const { return up; }

private:
    glm::vec3 eye{0.0f, 0.0f, 3.0f};
    glm::vec3 target{0.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};

    float fovy = 60.0f; // degrees
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