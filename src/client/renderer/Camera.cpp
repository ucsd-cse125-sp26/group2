#include "Camera.hpp"

#include <cmath>
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
    computeProjectionMatrix();
}

void Camera::setFov(float fovyDegrees)
{
    fovy_ = glm::radians(fovyDegrees);
    computeProjectionMatrix();
}

void Camera::setZNear(float zNear)
{
    zNear_ = zNear;
    computeProjectionMatrix();
}

void Camera::setZFar(float zFar)
{
    zFar_ = zFar;
    computeProjectionMatrix();
}

void Camera::setEye(glm::vec3 eye)
{
    eye_ = eye;
    computeViewMatrix();
}

void Camera::setTarget(float pitch, float yaw, float roll)
{
    const float cosPitch = std::cos(pitch);

    // Forward vector from yaw (horizontal) and pitch (vertical).
    // Convention matches InputSnapshot: yaw=0 → +Z, pitch>0 → looking down.
    const glm::vec3 forward{std::sin(yaw) * cosPitch, -std::sin(pitch), std::cos(yaw) * cosPitch};

    target_ = eye_ + forward;

    // Apply roll by rotating the world-up vector around the forward axis.
    // Skip the trig when roll is effectively zero to avoid losing precision
    // on the up vector (and to keep wallrun tilt cleanly off when not needed).
    glm::vec3 camUp{0.0f, 1.0f, 0.0f};
    if (std::abs(roll) > 0.001f) {
        const glm::vec3 right = glm::normalize(glm::cross(forward, camUp));
        const glm::vec3 trueUp = glm::normalize(glm::cross(right, forward));
        const float cosR = std::cos(roll);
        const float sinR = std::sin(roll);
        camUp = trueUp * cosR + right * sinR;
    }
    up_ = camUp;

    computeViewMatrix();
}

void Camera::setUp(glm::vec3 up)
{
    up_ = up;
    computeViewMatrix();
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

void Camera::applySubpixelJitter(const float jitterX, const float jitterY)
{
    projection_[2][0] += jitterX;
    projection_[2][1] += jitterY;
}

const glm::vec3 Camera::getForward() const
{
    return glm::normalize(target_ - eye_);
}

const glm::vec3 Camera::getRight() const
{
    return glm::normalize(glm::cross(getForward(), up_));
}
