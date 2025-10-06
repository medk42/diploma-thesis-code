#pragma once

#include <cstdint>
#include <vector>
#include <variant>

namespace aergo::module::helpers::visualization_3d_interface
{
    struct Vec3 
    { 
        static Vec3 Zero() { return {0,0,0}; }
        static Vec3 One()  { return {1,1,1}; }

        float x,y,z; 
    };

    struct Quat
    { 
        static Quat Identity() { return {0,0,0,1}; }

        float x,y,z,w; 
    };

    struct Pose
    {
        Vec3 t = Vec3::Zero();
        Quat q = Quat::Identity();
    };

    struct Color
    {
        // default: light blue
        uint8_t r = 0x66;
        uint8_t g = 0x99;
        uint8_t b = 0xFF;
        uint8_t a = 0xFF;
    };

    enum class PrimitiveShapeType : uint8_t { BOX=0, SPHERE=1, CYLINDER=2 };

    struct BoxDesc { float sx, sy, sz; };
    struct SphereDesc { float r; };
    struct CylinderDesc { float rBot, rTop, h; };

    struct PrimitiveShape
    {
        PrimitiveShapeType type;
        std::variant<BoxDesc, SphereDesc, CylinderDesc> desc;
        Pose     origin;          // local pose
        Color    color;
    };

    struct ComplexShape
    {
        std::vector<PrimitiveShape> parts;
    };

    enum class ObjectType : uint8_t { Complex=0 };

    struct ResourceId
    {
        uint32_t id; 
        auto operator<=>(const ResourceId&) const = default;
    };
    
    struct ObjectId
    { 
        uint32_t id; 
        auto operator<=>(const ObjectId&) const = default;
    };

    struct ObjectData
    { 
        ResourceId resource_id; 
        Pose pose; 
    };

    struct TrajectoryData 
    { 
        std::vector<Vec3> points; 
        Color color; 
        bool dashed; 
    };
}