#include "Camera.hpp"

namespace Mulen {
    void Camera::Update(double dt)
    {
        // - to do: proper integration (seeming FPS-invariance should be a given)
        const auto dampening = 0.8;
        position += velocity * dt;
        //velocity *= pow(dampening, 60.0 * dt);
        velocity *= exp(-dampening * 20.0 * dt);
        velocity += acceleration * dt;
        acceleration = Position{ 0.0 };
    }

    void Camera::Accelerate(const Position& a)
    {
        acceleration += a;
    }

    Camera::Mat4 Camera::GetViewMatrix() const 
    {
        return glm::translate(glm::toMat4(orientation), -position);
    }
}
