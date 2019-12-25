#pragma once

struct Camera
{
    std::array<float, 4> viewport; // left, top, right, bottom
    float fovy = 60.f;
    float nearz = 1.f;
    float farz = 1000.f;

    glm::vec3 position;
    glm::vec3 direction;
    glm::quat rotation;
    glm::mat4 projection;
    glm::mat4 view;
};

static const glm::vec3 kCameraUp = glm::vec3(0.f, 1.f, 0.f);

void cameraMove(Camera& camera, const glm::vec3& value);
void cameraRotate(Camera& camera, const float angleX, const float angleY);
void cameraUpdateProjection(Camera& camera);
void cameraUpdateView(Camera& camera);

