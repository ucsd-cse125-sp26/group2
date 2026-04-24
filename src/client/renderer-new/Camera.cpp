#include "Camera.hpp"

#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

NewCamera::NewCamera()
{
    computeViewProjectionMatrix();
}

void NewCamera::setAspect(float width, float height)
{
    aspect_ = (height == 0.0f) ? 1.0f : width / height;
}

void NewCamera::setFov(float fovyDegrees)
{
    fovy_ = glm::radians(fovyDegrees);
}

void NewCamera::setZNear(float zNear)
{
    zNear_ = zNear;
}

void NewCamera::setZFar(float zFar)
{
    zFar_ = zFar;
}

void NewCamera::setEye(glm::vec3 eye)
{
    eye_ = eye;
}

void NewCamera::setTarget(float pitch, float yaw, float roll)
{
    const float cosPitch = std::cos(pitch);

    // Forward vector from yaw (horizontal) and pitch (vertical).
    // Convention matches InputSnapshot: yaw=0 → +Z, pitch>0 → looking down.
    const glm::vec3 forward{std::sin(yaw) * cosPitch, -std::sin(pitch), std::cos(yaw) * cosPitch};

    target_ = eye_ + forward;
}

void NewCamera::setUp(glm::vec3 up)
{
    up_ = up;
}

void NewCamera::computeViewProjectionMatrix()
{
    glm::mat4 view = glm::lookAt(eye_, target_, up_);
    glm::mat4 projection = glm::perspective(fovy_, aspect_, zNear_, zFar_);
    view_projection_ = projection * view;
}
