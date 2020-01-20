#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

namespace Mulen {
    class Camera
    {
        typedef glm::vec3 Position;
        typedef glm::quat Orientation;
        typedef glm::mat4 Matrix;

        Matrix projectionMatrix;
        Position acceleration{ 0.0f }, velocity{ 0.0f }, position{ 0.0f };
        Orientation orientation = glm::identity<Orientation>();

    public:
        void Update(float dt);

        void SetPerspectiveProjection(float fovy, float aspect, float near, float far)
        {
            projectionMatrix = glm::perspective(fovy, aspect, near, far);
        }

        const Position& GetPosition() const { 
            return position;
        }
        void SetPosition(const Position& p) {
            position = p;
        }
        void Accelerate(const Position&);

        Matrix GetViewMatrix() const;
        Matrix GetProjectionMatrix() const { return projectionMatrix; }

        const Orientation& GetOrientation() const { return orientation; }
        void ApplyRotation(const Orientation& q)
        {
            orientation = glm::normalize(q * orientation);
        }
    };
}