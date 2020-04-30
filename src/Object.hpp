#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

struct Object
{
    typedef glm::dvec3 Position;
    typedef glm::dquat Orientation;
    typedef glm::dmat4 Mat4;

    const Position& GetPosition() const 
    {
        return position;
    }

    void SetPosition(const Position& p) 
    {
        position = p;
    }

    const Orientation& GetOrientation() const
    { 
        return orientation;
    }

    void SetOrientation(const Orientation& o)
    {
        orientation = o;
    }

    void ApplyRotation(const Orientation& q)
    {
        orientation = glm::normalize(q * orientation);
    }

    Mat4 GetOrientationMatrix() const
    {
        return glm::toMat4(orientation);
    }

protected:
    Position position{ 0.0f };
    Orientation orientation = glm::identity<Orientation>();
};

