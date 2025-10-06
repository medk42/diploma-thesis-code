#pragma once

#include <cstdint>
#include <vector>
#include <variant>
#include <cmath>

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

        // Hamilton product: this ∘ rhs (apply rhs, then this)
        friend Quat operator*(const Quat& a, const Quat& b) {
            return {
                a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
                a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
                a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
                a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z
            };
        }

        Quat normalized() const {
            float s = std::sqrt(x*x+y*y+z*z+w*w);
            return {x/s, y/s, z/s, w/s};
        }

        // Axis-angle (degrees)
        static Quat FromAxisDeg(float ax, float ay, float az, float deg) {
            constexpr float k = 3.14159265358979323846f / 180.f;
            float r = deg * k;
            float s = std::sin(r * 0.5f), c = std::cos(r * 0.5f);
            // assume axis is already normalized (or normalize if you prefer)
            return {ax*s, ay*s, az*s, c};
        }

        // Fluent rotations: LEFT-multiply so chaining reads in time order.
        Quat RotateDegX(float deg) const {
            Quat rx = FromAxisDeg(1.f, 0.f, 0.f, deg);
            return (rx * *this).normalized();
        }
        Quat RotateDegY(float deg) const {
            Quat ry = FromAxisDeg(0.f, 1.f, 0.f, deg);
            return (ry * *this).normalized();
        }
        Quat RotateDegZ(float deg) const {
            Quat rz = FromAxisDeg(0.f, 0.f, 1.f, deg);
            return (rz * *this).normalized();
        }

        static Quat QuatFromRvec(double rx, double ry, double rz) {
            double theta = std::sqrt(rx*rx + ry*ry + rz*rz);
            if (theta < 1e-12) {
                return Quat::Identity();
            }
            double half = 0.5 * theta;
            double s = std::sin(half) / theta; // safe because theta > 0
            double c = std::cos(half);
            return Quat{ float(rx * s), float(ry * s), float(rz * s), float(c) }.normalized();
        }
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