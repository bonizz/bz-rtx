#include "pch.h"

#include "camera.h"

void cameraMove(Camera& camera, const glm::vec3& value)
{
    glm::vec3 right = glm::normalize(glm::cross(camera.direction, kCameraUp));

    camera.position += right * value.x;
    camera.position += kCameraUp * value.y;
    camera.position += camera.direction * value.z;
}

void cameraRotate(Camera& camera, const float angleX, const float angleY)
{
    glm::vec3 right = glm::cross(camera.direction, kCameraUp);

    glm::quat pitch = glm::angleAxis(glm::radians(angleY), right);
    glm::quat heading = glm::angleAxis(glm::radians(angleX), kCameraUp);

    glm::quat combined = glm::normalize(pitch * heading);

    camera.direction = glm::normalize(glm::rotate(combined, camera.direction));
}

void cameraUpdateProjection(Camera& camera)
{
    float left = camera.viewport[0];
    float top = camera.viewport[1];
    float right = camera.viewport[2];
    float bottom = camera.viewport[3];

    float aspect = float(right - left) / float(bottom - top);
    camera.projection = glm::perspectiveRH_ZO(
        glm::radians(camera.fovy), aspect, camera.nearz, camera.farz);
}

void cameraUpdateView(Camera& camera)
{
    camera.view = glm::lookAtRH(camera.position, camera.position + camera.direction, kCameraUp);
}
