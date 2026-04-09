#include "Camera.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

Camera::Camera(glm::vec3 eye,
               glm::vec3 target,
               glm::vec3 up,
               float fovyDegrees,
               float aspectRatio,
               float nearPlane,
               float farPlane)
    : eye(eye)
    , target(target)
    , up(glm::normalize(up))
    , fovy(fovyDegrees)
    , aspect(aspectRatio)
    , nearPlane(nearPlane)
    , farPlane(farPlane)
    , eyeDefault(eye)
    , targetDefault(target)
    , upDefault(glm::normalize(up))
    , fovyDefault(fovyDegrees)
    , aspectDefault(aspectRatio)
    , nearDefault(nearPlane)
    , farDefault(farPlane)
{
    computeMatrices();
}

void Camera::reset()
{
    eye = eyeDefault;
    target = targetDefault;
    up = upDefault;
    fovy = fovyDefault;
    aspect = aspectDefault;
    nearPlane = nearDefault;
    farPlane = farDefault;

    computeMatrices();
}

void Camera::setAspect(float aspectRatio)
{
    aspect = aspectRatio;
    computeMatrices();
}

void Camera::setPerspective(float fovyDegrees, float aspectRatio, float nearPlaneValue, float farPlaneValue)
{
    fovy = fovyDegrees;
    aspect = aspectRatio;
    nearPlane = nearPlaneValue;
    farPlane = farPlaneValue;
    computeMatrices();
}

void Camera::setLookAt(glm::vec3 eyePos, glm::vec3 targetPos, glm::vec3 upDir)
{
    eye = eyePos;
    target = targetPos;
    up = glm::normalize(upDir);
    computeMatrices();
}

void Camera::rotateRight(float degrees)
{
    const glm::vec3 axis = glm::normalize(up);
    const glm::mat4 rot = glm::rotate(glm::mat4(1.0f), glm::radians(degrees), axis);

    const glm::vec3 offset = eye - target;
    eye = target + glm::vec3(rot * glm::vec4(offset, 0.0f));
    up = glm::normalize(glm::vec3(rot * glm::vec4(up, 0.0f)));

    computeMatrices();
}

void Camera::rotateUp(float degrees)
{
    const glm::vec3 forward = glm::normalize(target - eye);
    const glm::vec3 right = glm::normalize(glm::cross(forward, up));
    const glm::mat4 rot = glm::rotate(glm::mat4(1.0f), glm::radians(degrees), right);

    const glm::vec3 offset = eye - target;
    eye = target + glm::vec3(rot * glm::vec4(offset, 0.0f));
    up = glm::normalize(glm::vec3(rot * glm::vec4(up, 0.0f)));

    computeMatrices();
}

void Camera::computeMatrices()
{
    view = glm::lookAt(eye, target, up);
    proj = glm::perspective(glm::radians(fovy), aspect, nearPlane, farPlane);
}

glm::vec3 Camera::getForward() const
{
    return glm::normalize(target - eye);
}

glm::vec3 Camera::getRight() const
{
    return glm::normalize(glm::cross(getForward(), up));
}