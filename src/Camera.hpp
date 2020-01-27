#pragma once
#include "Object.hpp"

namespace Mulen {
    class Camera : public Object
    {
        Mat4 projectionMatrix;
        Position acceleration{ 0.0 }, velocity{ 0.0 };

    public:
        double radius;

        void Update(double dt);

        void SetPerspectiveProjection(double fovy, double aspect, double near, double far)
        {
            projectionMatrix = glm::perspective(fovy, aspect, near, far);
        }

        void Accelerate(const Position&);

        Mat4 GetViewMatrix() const;
        Mat4 GetProjectionMatrix() const { return projectionMatrix; }

        const Position& GetVelocity() const { return velocity;  }
    };
}