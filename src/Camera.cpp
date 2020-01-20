#include "Camera.hpp"

namespace Mulen {
    void Camera::Update(float dt)
    {
        // - to do: proper integration (seeming FPS-invariance should be a given)
        const float dampening = 0.8f;
        position += velocity * dt;
        velocity *= pow(dampening, 60.0 * dt);
        velocity += acceleration * dt;
        acceleration = Position{ 0.0f };
    }

    void Camera::Accelerate(const Position& a)
    {
        acceleration += a;
    }

    Camera::Matrix Camera::GetViewMatrix() const 
    {
        auto rotate = glm::toMat4(orientation);
        auto translate = glm::translate(Matrix{ 1.f }, -position);
        return rotate * translate;
    }
}
