#pragma once
#include "Object.hpp"

namespace Mulen {
    class Camera : public Object
    {
        double fovy;
        Mat4 projectionMatrix;
        Position acceleration{ 0.0 }, velocity{ 0.0 };
        double maxSpeed = std::numeric_limits<double>::max(); // infinity means "distance-based" (but maybe it should be made explicit)

        bool inertial = false, keepLevel = false;

    public:
        // - to do: move all update and radius computation logic fully into this class
        bool needsUpdate = false;
        double radius;

        bool upright = true;

        // Tell the camera to recompute e.g. radius on next frame (since e.g. position was modified directly).
        void FlagForUpdate()
        {
            needsUpdate = true;
        }

        void Update(double dt);

        void SetPerspectiveProjection(double fovy, double aspect, double near, double far)
        {
            this->fovy = fovy;
            projectionMatrix = glm::perspective(fovy, aspect, near, far);
        }

        void Accelerate(const Position&);

        void SetFovy(double f) { fovy = f; }
        double GetFovy() const { return fovy; }
        Mat4 GetViewMatrix() const;
        Mat4 GetProjectionMatrix() const { return projectionMatrix; }

        const Position& GetVelocity() const { return velocity;  }

        void SetMaxSpeed(double maxSpeed) { this->maxSpeed = maxSpeed; }
        double GetMaxSpeed() const { return maxSpeed; }

        void SetInertial(bool b) { inertial = b; }
        bool IsInertail() const { return inertial; }
        void SetKeepLevel(bool b) { keepLevel = b; }
        bool IsKeepingLevel() const { return keepLevel; }
    };
}