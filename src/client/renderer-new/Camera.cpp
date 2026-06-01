/// @file Camera.cpp
/// @brief Implementation of the new renderer Camera class.

#include "Camera.hpp"

#include <algorithm>
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
    // Final safety net: never let a corrupted angle produce a NaN basis (which
    // would feed glm::lookAt a degenerate up vector and garble the whole frame).
    // Sanitize non-finite inputs and keep pitch off the poles so `forward` can
    // never be parallel to the world up axis.
    if (!std::isfinite(pitch))
        pitch = 0.0f;
    if (!std::isfinite(yaw))
        yaw = 0.0f;
    if (!std::isfinite(roll))
        roll = 0.0f;
    constexpr float kPitchLimit = 1.5620697f; // 89.5° in radians.
    pitch = std::clamp(pitch, -kPitchLimit, kPitchLimit);

    const float cosPitch = std::cos(pitch);

    glm::vec3 y_axis{0.0f, 1.0f, 0.0f};
    // Forward vector from yaw (horizontal) and pitch (vertical).
    // Convention matches InputSnapshot: yaw=0 → +Z, pitch>0 → looking down.
    const glm::vec3 forward{std::sin(yaw) * cosPitch, -std::sin(pitch), std::cos(yaw) * cosPitch};

    // `cross(forward, y)` has magnitude cos(pitch); the pitch clamp keeps it
    // comfortably non-zero, but guard the degenerate case anyway so normalize
    // can never divide by ~0.
    const glm::vec3 rightRaw = glm::cross(forward, y_axis);
    const float rightLen = glm::length(rightRaw);
    const glm::vec3 right = (rightLen > 1e-4f) ? (rightRaw / rightLen) : glm::vec3{1.0f, 0.0f, 0.0f};
    const glm::vec3 preRollUp = glm::normalize(glm::cross(right, forward));

    up_ = preRollUp * glm::cos(roll) + right * glm::sin(roll);

    target_ = eye_ + forward;
}

void NewCamera::setUp(glm::vec3 up)
{
    up_ = up;
}

void NewCamera::computeViewProjectionMatrix()
{
    view_ = glm::lookAt(eye_, target_, up_);
    projection_ = glm::perspective(fovy_, aspect_, zNear_, zFar_);
    view_projection_ = projection_ * view_;

    viewProjectionFrustumPlanes_ = gribbHartmannFrustumPlanes(view_projection_);

}


FrustumPlanes NewCamera::gribbHartmannFrustumPlanes(const glm::mat4 &viewProjectionMat)
{
    FrustumPlanes frustumPlanes;

    glm::mat4 viewProjectionMatTranspose = glm::transpose(viewProjectionMat);

    frustumPlanes.left = viewProjectionMatTranspose[3] - viewProjectionMatTranspose[0];
    frustumPlanes.right = viewProjectionMatTranspose[3] + viewProjectionMatTranspose[0];

    frustumPlanes.bottom = viewProjectionMatTranspose[3] - viewProjectionMatTranspose[1];
    frustumPlanes.top = viewProjectionMatTranspose[3] + viewProjectionMatTranspose[1];

    frustumPlanes.near = viewProjectionMatTranspose[3] - viewProjectionMatTranspose[2];
    frustumPlanes.far = viewProjectionMatTranspose[3] + viewProjectionMatTranspose[2];

    return frustumPlanes;

}

glm::vec3 NewCamera::getForward() const
{
    return glm::normalize(target_ - eye_);
}

glm::vec3 NewCamera::getRight() const
{
    return glm::normalize(glm::cross(getForward(), up_));
}
