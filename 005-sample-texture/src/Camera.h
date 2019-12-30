#pragma once

struct Camera
{
    std::array<float, 4> viewport; // left, top, right, bottom
    float fovy = 60.f;
    float nearz = 1.f;
    float farz = 1000.f;

    glm::vec3 position;
    glm::vec3 up;
    glm::vec3 forward;
    glm::mat4 projection;
    glm::mat4 view;
};

void cameraMove(Camera& camera, const glm::vec3& value);
void cameraRotate(Camera& camera, const float angleX, const float angleY);
void cameraRotate(Camera& camera, const glm::quat& rotation);
void cameraUpdateProjection(Camera& camera);
void cameraUpdateView(Camera& camera);

