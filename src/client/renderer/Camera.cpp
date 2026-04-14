#include "Camera.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

Camera::Camera()
{
    computeViewMatrix();
    computeProjectionMatrix();
}

void Camera::setAspect(float width, float height)
{
    aspect_ = (height == 0.0f) ? 1.0f : width / height;
    computeViewMatrix();
}

void Camera::setFov(float fovyDegrees)
{
    fovy_ = glm::radians(fovyDegrees);
    computeViewMatrix();
}

void Camera::setZNear(float zNear)
{
    zNear_ = zNear;
    computeViewMatrix();
}

void Camera::setZFar(float zFar)
{
    zFar = zFar_;
    computeViewMatrix();
}

void Camera::setEye(glm::vec3 eye)
{
    eye_ = eye;
    computeProjectionMatrix();
}

void Camera::setTarget(float pitch, float yaw, float _roll)
{
    const float cosPitch = std::cos(pitch);

    // Forward vector from yaw (horizontal) and pitch (vertical).
    // Convention matches InputSnapshot: yaw=0 → +Z, pitch>0 → looking down.
    const glm::vec3 forward{std::sin(yaw) * cosPitch, -std::sin(pitch), std::cos(yaw) * cosPitch};

    target_ = eye_ + forward;
    computeProjectionMatrix();
}

void Camera::setUp(glm::vec3 up)
{
    up_ = up;
    computeProjectionMatrix();
}

void Camera::computeViewMatrix()
{
    view_ = glm::lookAt(eye_, target_, up_);
}

void Camera::computeProjectionMatrix()
{
    projection_ = glm::perspective(fovy_, aspect_, zNear_, zFar_);
    projection_[1][1] *= -1.0f;
}

const glm::vec3 Camera::getForward() const
{
    return glm::normalize(target_ - eye_);
}

const glm::vec3 Camera::getRight() const
{
    return glm::normalize(glm::cross(getForward(), up_));
}
