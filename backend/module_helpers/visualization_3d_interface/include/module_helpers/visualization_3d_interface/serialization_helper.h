#pragma once

#include "scene_desc_api.h"
#include "command_buffer.h"

#include <vector>

namespace aergo::module::helpers::visualization_3d_interface
{
    namespace serialization
    {
        /// @brief Push a uint32_t (4 bytes, little-endian) into buffer.
        inline void pushUint32(std::vector<char>& buf, uint32_t v)
        {
            const char* byte_data = reinterpret_cast<const char*>(&v);
            buf.insert(buf.end(), byte_data, byte_data + sizeof(uint32_t));
        }


        /// @brief Push a uint8_t (1 byte) into buffer.
        inline void pushUint8(std::vector<char>& buf, uint8_t v)
        {
            buf.push_back(static_cast<char>(v));
        }


        /// @brief Push a float (4 bytes, little-endian) into buffer.
        inline void pushF32(std::vector<char>& buf, float v)
        {
            static_assert(sizeof(float) == 4, "float must be 4 bytes");

            const char* byte_data = reinterpret_cast<const char*>(&v);
            buf.insert(buf.end(), byte_data, byte_data + sizeof(float));
        }


        /// @brief Push pose (t: x,y,z; q: x,y,z,w) as 7 floats (4 bytes each, little-endian) into buffer
        inline void pushPose(std::vector<char>& buf, const Pose& pose)
        {
            // [7*f32 t.x,t.y,t.z,q.x,q.y,q.z,q.w]
            pushF32(buf, pose.t.x);
            pushF32(buf, pose.t.y);
            pushF32(buf, pose.t.z);
            pushF32(buf, pose.q.x);
            pushF32(buf, pose.q.y);
            pushF32(buf, pose.q.z);
            pushF32(buf, pose.q.w);
        }


        /// @brief Push color (r,g,b,a) as 4 bytes (1 byte each) into buffer
        inline void pushColor(std::vector<char>& buf, const Color& color)
        {
            // [4*u8 r,g,b,a]
            pushUint8(buf, color.r);
            pushUint8(buf, color.g);
            pushUint8(buf, color.b);
            pushUint8(buf, color.a);
        }


        /// @brief Push vector3 (x,y,z) as 3 floats (4 bytes each, little-endian) into buffer
        inline void pushVec3(std::vector<char>& buf, const Vec3& v)
        {
            pushF32(buf, v.x);
            pushF32(buf, v.y);
            pushF32(buf, v.z);
        }


        /// @brief Push a boolean as a uint8_t (1 byte, 0=false, 1=true) into buffer.
        inline void pushBool(std::vector<char>& buf, bool v)
        {
            pushUint8(buf, v ? 1 : 0);
        }


        /// @brief Push pending resource registrations into buffer. The format is:
        ///    REGISTRATION_SECTION:
        ///        u32 registration_count
        ///        repeat registration_count times:
        ///        {
        ///            u32   resource_id
        ///            u32   part_count
        ///            repeat part_count times:
        ///            {
        ///                u8 type                 // 0=Box, 1=Sphere, 2=Cylinder
        ///                switch(type):
        ///                    case Box:      f32 sx, sy, sz
        ///                    case Sphere:   f32 r
        ///                    case Cylinder: f32 rTop, rBot, h
        ///                Pose7 origin            // local pose of this primitive
        ///                u8 r, g, b, a           // color
        ///            }
        ///        }
        inline bool pushPendingRegistration(std::vector<char>& buf, const std::map<ResourceId, ComplexShape>& registrations)
        {
            uint32_t registration_count = static_cast<uint32_t>(registrations.size());
            pushUint32(buf, registration_count); // [u32 registration_count]

            for (const auto& [res_id, shape] : registrations)
            {
                pushUint32(buf, res_id.id);      // [u32 resource_id]
                uint32_t part_count = static_cast<uint32_t>(shape.parts.size());
                pushUint32(buf, part_count);     // [u32 part_count]
                for (const auto& part : shape.parts)
                {
                    // Box: [u8 type=0][3*f32 sx,sy,sz][7*f32 origin][4*u8 color]
                    // Sphere: [u8 type=1][1*f32 r][7*f32 origin][4*u8 color]
                    // Cylinder: [u8 type=2][3*f32 rTop,rBot,h][7*f32 origin][4*u8 color]

                    // push type
                    pushUint8(buf, static_cast<uint8_t>(part.type));

                    // push description
                    if (part.type == PrimitiveShapeType::BOX)
                    {
                        if (!std::holds_alternative<BoxDesc>(part.desc))
                        {
                            return false; // invalid
                        }
                        const BoxDesc& d = std::get<BoxDesc>(part.desc);
                        pushF32(buf, d.sx);
                        pushF32(buf, d.sy);
                        pushF32(buf, d.sz);
                    }
                    else if (part.type == PrimitiveShapeType::SPHERE)
                    {
                        if (!std::holds_alternative<SphereDesc>(part.desc))
                        {
                            return false; // invalid
                        }
                        const SphereDesc& d = std::get<SphereDesc>(part.desc);
                        pushF32(buf, d.r);
                    }
                    else if (part.type == PrimitiveShapeType::CYLINDER)
                    {
                        if (!std::holds_alternative<CylinderDesc>(part.desc))
                        {
                            return false; // invalid
                        }
                        const CylinderDesc& d = std::get<CylinderDesc>(part.desc);
                        pushF32(buf, d.rTop);
                        pushF32(buf, d.rBot);
                        pushF32(buf, d.h);
                    }

                    // push local origin pose
                    pushPose(buf, part.origin);

                    // push color
                    pushColor(buf, part.color);
                }
            }

            return true;
        }


        /// @brief Push object commands into buffer. The format is:
        ///    OBJECT_SECTION:
        ///        u32 object_count
        ///        repeat object_count times:
        ///        {
        ///            u32 id
        ///            u8  action               // 0=Add, 1=Update, 2=Remove
        ///            if action == Add:
        ///                u32 resource_id
        ///            if action != Remove:
        ///                Pose7 pose
        ///        }
        inline void pushObjectCommands(std::vector<char>& buf, const std::map<ObjectId, CommandBuffer::ObjectParameters>& objects)
        {
            uint32_t object_count = static_cast<uint32_t>(objects.size());
            pushUint32(buf, object_count); // [u32 object_count]

            for (const auto& [obj_id, params] : objects)
            {
                // Add: [u32 new_id][u8 action=0][u32 resource_id][7*f32 pose]
                // Update: [u32 id][u8 action=1][7*f32 pose]
                // Remove: [u32 id][u8 action=2]

                pushUint32(buf, obj_id.id);
                pushUint8(buf, static_cast<uint8_t>(params.action));
                if (params.action == CommandBuffer::Action::ADD)
                {
                    pushUint32(buf, params.resource_id.id);
                    pushPose(buf, params.pose);
                }
                if (params.action == CommandBuffer::Action::UPDATE)
                {
                    pushPose(buf, params.pose);
                }
            }
        }


        /// @brief Push trajectory commands into buffer. The format is:
        ///    TRAJECTORY_SECTION:
        ///        u32 trajectory_count
        ///        repeat trajectory_count times:
        ///        {
        ///            u32 id
        ///            u8  action               // 0=Add, 1=Update, 2=Remove
        ///        
        ///            if action == Add:
        ///                u8 r, g, b, a        // color
        ///                u8  dashed           // 0/1
        ///                u32 point_count
        ///                repeat point_count times: f32 x, y, z
        ///        
        ///            if action == Update:
        ///                u32 point_count
        ///                repeat point_count times: f32 x, y, z
        ///                u32 remove_from_head
        ///        
        ///            // if Remove: no payload
        ///        }
        inline void pushTrajectoryCommands(std::vector<char>& buf, const std::map<ObjectId, CommandBuffer::TrajectoryParameters>& trajectories)
        {
            uint32_t trajectory_count = static_cast<uint32_t>(trajectories.size());
            pushUint32(buf, trajectory_count); // [u32 trajectory_count]

            for (const auto& [traj_id, params] : trajectories)
            {
                // Add: [u32 new_id][u8 action=0][4*u8 r,g,b,a][u8 dashed][u32 point_count][point_count*3*f32 points]
                // Update: [u32 id][u8 action=1][u32 point_count][point_count*3*f32 points][u32 remove_from_head]
                // Remove: [u32 id][u8 action=2]

                pushUint32(buf, traj_id.id);                           // [u32 id]
                pushUint8(buf, static_cast<uint8_t>(params.action));   // [u8 action]
                if (params.action == CommandBuffer::Action::ADD)
                {
                    // push color
                    pushColor(buf, params.color);                      // [4*u8 r,g,b,a]
    
                    pushBool(buf, params.dashed);                      // [u8 dashed]
                    uint32_t point_count = static_cast<uint32_t>(params.points.size());
                    pushUint32(buf, point_count);                      // [u32 point_count]
                    for (const auto& p : params.points)
                    {
                        pushVec3(buf, p);                              // [3*f32 x,y,z]
                    }
                }
                else if (params.action == CommandBuffer::Action::UPDATE)
                {
                    uint32_t point_count = static_cast<uint32_t>(params.points.size());
                    pushUint32(buf, point_count);                      // [u32 point_count]
                    for (const auto& p : params.points)
                    {
                        pushVec3(buf, p);                              // [3*f32 x,y,z]
                    }
                    pushUint32(buf, params.remove_from_head);          // [u32 remove_from_head]
                }
            }
        }


        /// @brief Push current scene objects into buffer. The format is:
        ///    SCENE_OBJECTS_SECTION:
        ///        u32 object_count
        ///        repeat object_count times:
        ///        {
        ///            u32 id
        ///            u32 resource_id
        ///            Pose7 pose
        ///        }
        inline void pushSceneObjects(std::vector<char>& buf, const std::map<ObjectId, ObjectData>& objects)
        {
            uint32_t object_count = static_cast<uint32_t>(objects.size());
            pushUint32(buf, object_count); // [u32 object_count]

            for (const auto& [obj_id, obj] : objects)
            {
                pushUint32(buf, obj_id.id);          // [u32 id]
                pushUint32(buf, obj.resource_id.id); // [u32 resource_id]
                pushPose(buf, obj.pose);             // [7*f32 pose]
            }
        }


        /// @brief Push current scene trajectories into buffer. The format is:
        ///    SCENE_TRAJECTORIES_SECTION:
        ///        u32 trajectory_count
        ///        repeat trajectory_count times:
        ///        {
        ///            u32 id
        ///            u8 r, g, b, a           // color
        ///            u8  dashed              // 0/1
        ///            u32 point_count
        ///            repeat point_count times: f32 x, y, z
        ///        }
        inline void pushSceneTrajectories(std::vector<char>& buf, const std::map<ObjectId, TrajectoryData>& trajectories)
        {
            uint32_t trajectory_count = static_cast<uint32_t>(trajectories.size());
            pushUint32(buf, trajectory_count);           // [u32 trajectory_count]

            for (const auto& [traj_id, traj] : trajectories)
            {
                pushUint32(buf, traj_id.id);             // [u32 id]

                // push color
                pushColor(buf, traj.color);              // [4*u8 r,g,b,a]
                pushBool(buf, traj.dashed);              // [u8 dashed]

                uint32_t point_count = static_cast<uint32_t>(traj.points.size());
                pushUint32(buf, point_count);            // [u32 point_count]
                for (const auto& p : traj.points)
                {
                    pushVec3(buf, p);                    // [3*f32 x,y,z]
                }
            }
        }
    }

    namespace deserialization
    {
        class BufferReader
        {
        public:
            BufferReader(const char* data, size_t size) : data_(data), size_(size) { }

            /// @brief Read a uint32_t (4 bytes, little-endian) from buffer.
            bool readUint32(uint32_t& v)
            {
                if (pos_ + sizeof(uint32_t) > size_)
                {
                    return false; // out of bounds
                }
                v = *reinterpret_cast<const uint32_t*>(data_ + pos_);
                pos_ += sizeof(uint32_t);
                return true;
            }


            /// @brief Read a uint8_t (1 byte) from buffer.
            bool readUint8(uint8_t& v)
            {
                if (pos_ + sizeof(uint8_t) > size_)
                {
                    return false; // out of bounds
                }
                v = *reinterpret_cast<const uint8_t*>(data_ + pos_);
                pos_ += sizeof(uint8_t);
                return true;
            }


            /// @brief Read a float (4 bytes, little-endian) from buffer.
            bool readF32(float& v)
            {
                if (pos_ + sizeof(float) > size_)
                {
                    return false; // out of bounds
                }
                v = *reinterpret_cast<const float*>(data_ + pos_);
                pos_ += sizeof(float);
                return true;
            }

        private:
            const char* data_{ nullptr };
            size_t size_{ 0 };
            size_t pos_{ 0 };
        };

        /// @brief Read pose (t: x,y,z; q: x,y,z,w) as 7 floats (4 bytes each, little-endian) from buffer
        inline bool readPose(deserialization::BufferReader& reader, Pose& pose)
        {
            // [7*f32 t.x,t.y,t.z,q.x,q.y,q.z,q.w]
            if (!reader.readF32(pose.t.x)) return false;
            if (!reader.readF32(pose.t.y)) return false;
            if (!reader.readF32(pose.t.z)) return false;
            if (!reader.readF32(pose.q.x)) return false;
            if (!reader.readF32(pose.q.y)) return false;
            if (!reader.readF32(pose.q.z)) return false;
            if (!reader.readF32(pose.q.w)) return false;
            return true;
        }


        /// @brief Read color (r,g,b,a) as 4 bytes (1 byte each) from buffer
        inline bool readColor(deserialization::BufferReader& reader, Color& color)
        {
            // [4*u8 r,g,b,a]
            if (!reader.readUint8(color.r)) return false;
            if (!reader.readUint8(color.g)) return false;
            if (!reader.readUint8(color.b)) return false;
            if (!reader.readUint8(color.a)) return false;
            return true;
        }


        /// @brief Read vector3 (x,y,z) as 3 floats (4 bytes each, little-endian) from buffer
        inline bool readVec3(deserialization::BufferReader& reader, Vec3& v)
        {
            if (!reader.readF32(v.x)) return false;
            if (!reader.readF32(v.y)) return false;
            if (!reader.readF32(v.z)) return false;
            return true;
        }


        /// @brief Read a boolean as a uint8_t (1 byte, 0=false, 1=true; values larger than 1 also considered true) from buffer.
        inline bool readBool(deserialization::BufferReader& reader, bool& v)
        {
            uint8_t b;
            if (!reader.readUint8(b)) return false;
            v = (b != 0);
            return true;
        }


        /// @brief Read pending resource registrations from buffer. The format is:
        ///    REGISTRATION_SECTION:
        ///        u32 registration_count
        ///        repeat registration_count times:
        ///        {
        ///            u32   resource_id
        ///            u32   part_count
        ///            repeat part_count times:
        ///            {
        ///                u8 type                 // 0=Box, 1=Sphere, 2=Cylinder
        ///                switch(type):
        ///                    case Box:      f32 sx, sy, sz
        ///                    case Sphere:   f32 r
        ///                    case Cylinder: f32 rTop, rBot, h
        ///                Pose7 origin            // local pose of this primitive
        ///                u8 r, g, b, a           // color
        ///            }
        ///        }
        inline bool readPendingRegistration(deserialization::BufferReader& reader, std::map<ResourceId, ComplexShape>& registrations)
        {
            registrations.clear();

            uint32_t registration_count;
            if (!reader.readUint32(registration_count)) return false; // [u32 registration_count]

            for (uint32_t i = 0; i < registration_count; i++)
            {
                ResourceId res_id;
                if (!reader.readUint32(res_id.id)) return false; // [u32 resource_id]

                uint32_t part_count;
                if (!reader.readUint32(part_count)) return false; // [u32 part_count]

                ComplexShape shape;
                for (uint32_t j = 0; j < part_count; j++)
                {
                    PrimitiveShape part;

                    // read type
                    uint8_t type_u8;
                    if (!reader.readUint8(type_u8)) return false; // [u8 type]
                    if (type_u8 != static_cast<uint8_t>(PrimitiveShapeType::CYLINDER) 
                    && type_u8 != static_cast<uint8_t>(PrimitiveShapeType::SPHERE)
                    && type_u8 != static_cast<uint8_t>(PrimitiveShapeType::BOX))
                    {
                        return false; // invalid type
                    }
                    part.type = static_cast<PrimitiveShapeType>(type_u8);

                    // read description
                    if (part.type == PrimitiveShapeType::BOX)
                    {
                        BoxDesc d;
                        if (!reader.readF32(d.sx)) return false;
                        if (!reader.readF32(d.sy)) return false;
                        if (!reader.readF32(d.sz)) return false;
                        part.desc = d;
                    }
                    else if (part.type == PrimitiveShapeType::SPHERE)
                    {
                        SphereDesc d;
                        if (!reader.readF32(d.r)) return false;
                        part.desc = d;
                    }
                    else if (part.type == PrimitiveShapeType::CYLINDER)
                    {
                        CylinderDesc d;
                        if (!reader.readF32(d.rTop)) return false;
                        if (!reader.readF32(d.rBot)) return false;
                        if (!reader.readF32(d.h)) return false;
                        part.desc = d;
                    }

                    // read local origin pose
                    if (!readPose(reader, part.origin)) return false; // [7*f32 origin]

                    // read color
                    if (!readColor(reader, part.color)) return false; // [4*u8 r,g,b,a]

                    shape.parts.push_back(part);
                }

                registrations[res_id] = shape;
            }

            return true;
        }


        /// @brief Read object commands from buffer. The format is:
        ///    OBJECT_SECTION:
        ///        u32 object_count
        ///        repeat object_count times:
        ///        {
        ///            u32 id
        ///            u8  action               // 0=Add, 1=Update,
        ///            if action == Add:
        ///                u32 resource_id
        ///            if action != Remove:
        ///                Pose7 pose
        ///        }
        inline bool readObjectCommands(deserialization::BufferReader& reader, std::map<ObjectId, CommandBuffer::ObjectParameters>& objects)
        {
            objects.clear();

            uint32_t object_count;
            if (!reader.readUint32(object_count)) return false; // [u32 object_count]

            for (uint32_t i = 0; i < object_count; i++)
            {
                ObjectId obj_id;
                if (!reader.readUint32(obj_id.id)) return false; // [u32 id]

                uint8_t action_u8;
                if (!reader.readUint8(action_u8)) return false; // [u8 action]
                if (action_u8 != static_cast<uint8_t>(CommandBuffer::Action::ADD)
                && action_u8 != static_cast<uint8_t>(CommandBuffer::Action::UPDATE)
                && action_u8 != static_cast<uint8_t>(CommandBuffer::Action::REMOVE))
                {
                    return false; // invalid action
                }
                CommandBuffer::Action action = static_cast<CommandBuffer::Action>(action_u8);

                CommandBuffer::ObjectParameters params;
                params.action = action;

                if (action == CommandBuffer::Action::ADD)
                {
                    if (!reader.readUint32(params.resource_id.id)) return false; // [u32 resource_id]
                    if (!readPose(reader, params.pose)) return false;           // [7*f32 pose]
                }
                else if (action == CommandBuffer::Action::UPDATE)
                {
                    if (!readPose(reader, params.pose)) return false;           // [7*f32 pose]
                }
                // if action == REMOVE: no payload

                objects[obj_id] = params;
            }

            return true;
        }


        /// @brief Read trajectory commands from buffer. The format is:
        ///    TRAJECTORY_SECTION:
        ///        u32 trajectory_count
        ///        repeat trajectory_count times:
        ///        {
        ///            u32 id
        ///            u8  action               // 0=Add, 1=Update, 2=Remove
        ///
        ///            if action == Add:
        ///                u8 r, g, b, a        // color
        ///                u8  dashed           // 0/1
        ///                u32 point_count
        ///                repeat point_count times: f32 x, y, z
        ///
        ///            if action == Update:
        ///                u32 point_count
        ///                repeat point_count times: f32 x, y, z
        ///                u32 remove_from_head
        ///
        ///            // if Remove: no payload
        ///        }
        inline bool readTrajectoryCommands(deserialization::BufferReader& reader, std::map<ObjectId, CommandBuffer::TrajectoryParameters>& trajectories)
        {
            trajectories.clear();

            uint32_t trajectory_count;
            if (!reader.readUint32(trajectory_count)) return false; // [u32 trajectory_count]

            for (uint32_t i = 0; i < trajectory_count; i++)
            {
                ObjectId traj_id;
                if (!reader.readUint32(traj_id.id)) return false; // [u32 id]

                uint8_t action_u8;
                if (!reader.readUint8(action_u8)) return false; // [u8 action]
                if (action_u8 != static_cast<uint8_t>(CommandBuffer::Action::ADD)
                && action_u8 != static_cast<uint8_t>(CommandBuffer::Action::UPDATE)
                && action_u8 != static_cast<uint8_t>(CommandBuffer::Action::REMOVE))
                {
                    return false; // invalid action
                }
                CommandBuffer::Action action = static_cast<CommandBuffer::Action>(action_u8);

                CommandBuffer::TrajectoryParameters params;
                params.action = action;

                if (action == CommandBuffer::Action::ADD)
                {
                    if (!readColor(reader, params.color)) return false; // [4*u8 r,g,b,a]
                    if (!readBool(reader, params.dashed)) return false; // [u8 dashed]

                    uint32_t point_count;
                    if (!reader.readUint32(point_count)) return false; // [u32 point_count]
                    params.points.resize(point_count);
                    for (uint32_t j = 0; j < point_count; j++)
                    {
                        if (!readVec3(reader, params.points[j])) return false; // [3*f32 x,y,z]
                    }
                }
                else if (action == CommandBuffer::Action::UPDATE)
                {
                    uint32_t point_count;
                    if (!reader.readUint32(point_count)) return false; // [u32 point_count]
                    params.points.resize(point_count);
                    for (uint32_t j = 0; j < point_count; j++)
                    {
                        if (!readVec3(reader, params.points[j])) return false; // [3*f32 x,y,z]
                    }
                    if (!reader.readUint32(params.remove_from_head)) return false; // [u32 remove_from_head]
                }
                // if action == REMOVE: no payload

                trajectories[traj_id] = params;
            }

            return true;
        }


        /// @brief Read current scene objects from buffer. The format is:
        ///    SCENE_OBJECTS_SECTION:
        ///        u32 object_count
        ///        repeat object_count times:
        ///        {
        ///            u32 id
        ///            u32 resource_id
        ///            Pose7 pose
        ///        }
        inline bool readSceneObjects(deserialization::BufferReader& reader, std::map<ObjectId, ObjectData>& objects)
        {
            objects.clear();

            uint32_t object_count;
            if (!reader.readUint32(object_count)) return false; // [u32 object_count]

            for (uint32_t i = 0; i < object_count; i++)
            {
                ObjectId obj_id;
                if (!reader.readUint32(obj_id.id)) return false; // [u32 id]

                ObjectData obj;

                if (!reader.readUint32(obj.resource_id.id)) return false; // [u32 resource_id]
                if (!readPose(reader, obj.pose)) return false;           // [7*f32 pose]

                objects[obj_id] = obj;
            }

            return true;
        }


        /// @brief Read current scene trajectories from buffer. The format is:
        ///    SCENE_TRAJECTORIES_SECTION:
        ///        u32 trajectory_count
        ///        repeat trajectory_count times:
        ///        {
        ///            u32 id
        ///            u8 r, g, b, a           // color
        ///            u8  dashed              // 0/1
        ///            u32 point_count
        ///            repeat point_count times: f32 x, y, z
        ///        }
        inline bool readSceneTrajectories(deserialization::BufferReader& reader, std::map<ObjectId, TrajectoryData>& trajectories)
        {
            trajectories.clear();

            uint32_t trajectory_count;
            if (!reader.readUint32(trajectory_count)) return false; // [u32 trajectory_count]

            for (uint32_t i = 0; i < trajectory_count; i++)
            {
                ObjectId traj_id;
                if (!reader.readUint32(traj_id.id)) return false; // [u32 id]

                TrajectoryData traj;

                // read color
                if (!readColor(reader, traj.color)) return false; // [4*u8 r,g,b,a]
                if (!readBool(reader, traj.dashed)) return false; // [u8 dashed]

                uint32_t point_count;
                if (!reader.readUint32(point_count)) return false; // [u32 point_count]
                traj.points.resize(point_count);
                for (uint32_t j = 0; j < point_count; j++)
                {
                    if (!readVec3(reader, traj.points[j])) return false; // [3*f32 x,y,z]
                }

                trajectories[traj_id] = traj;
            }

            return true;
        }
    
    }
}