#pragma once
#include "Object.hpp"

namespace Mulen {
    class Camera : public Object
    {
        Matrix projectionMatrix;
        Position acceleration{ 0.0f }, velocity{ 0.0f };

    public:
        void Update(float dt);

        void SetPerspectiveProjection(float fovy, float aspect, float near, float far)
        {
            projectionMatrix = glm::perspective(fovy, aspect, near, far);
        }

        void Accelerate(const Position&);

        Matrix GetViewMatrix() const;
        Matrix GetProjectionMatrix() const { return projectionMatrix; }
    };
}