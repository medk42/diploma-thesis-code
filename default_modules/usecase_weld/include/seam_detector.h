// SeamDetector.hpp
#pragma once

#include <opencv2/core.hpp>
#include <opencv2/calib3d.hpp>

#include <array>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <cmath>
#include <limits>

#include "module_helpers/pose_utils/pose_utils.h"
#include "module_helpers/scene_detection_helper/message_types.h"


namespace aergo::default_modules::usecase_weld
{

namespace pu = aergo::module::helpers::pose_utils;
namespace sdh = aergo::module::helpers::scene_detection_helper;

// --------------------------- small math helpers ---------------------------

inline cv::Vec3d cross3(const cv::Vec3d& a, const cv::Vec3d& b)
{
    return cv::Vec3d(
        a[1]*b[2] - a[2]*b[1],
        a[2]*b[0] - a[0]*b[2],
        a[0]*b[1] - a[1]*b[0]
    );
}

inline double dot3(const cv::Vec3d& a, const cv::Vec3d& b)
{
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}

inline double norm3(const cv::Vec3d& v)
{
    return std::sqrt(dot3(v,v));
}

inline bool normalize3(cv::Vec3d& v, double eps = 1e-12)
{
    const double n = norm3(v);
    if (n < eps) return false;
    v *= (1.0 / n);
    return true;
}

// --------------------------- outputs ---------------------------

struct Seam
{
    cv::Vec3d p0_world{0,0,0};
    cv::Vec3d p1_world{0,0,0};

    cv::Vec3d tangent_world{0,0,1};   // unit
    cv::Vec3d torch_dir_world{0,0,1}; // unit; direction FROM which torch comes

    enum class Type { EdgeEdge, EdgeFace } type{Type::EdgeEdge};

    int boxA{-1}, boxB{-1};      // indices in input detected_boxes
    int edgeA{-1}, edgeB{-1};    // for EdgeEdge (or edgeA for EdgeFace)
    int faceA{-1}, faceB{-1};    // chosen face indices for torch_dir (faceA on boxA, faceB on boxB)

    double score{0.0};           // optional (not used yet)
};

// --------------------------- internal geometry ---------------------------

struct BoxGeom
{
    struct Face
    {
        int axis{0};      // 0=x,1=y,2=z
        int sign{+1};     // +1 or -1
        cv::Vec3d n{0,0,1}; // world unit normal
        cv::Vec3d c{0,0,0}; // world point on face (center)
        cv::Vec3d u{1,0,0}; // in-plane axis unit
        cv::Vec3d v{0,1,0}; // in-plane axis unit
        double hu{0.0};     // half extent along u
        double hv{0.0};     // half extent along v
    };

    struct Edge
    {
        int v0{0}, v1{0};
        int f0{0}, f1{0};    // incident faces (indices into faces[0..5])
        cv::Vec3d p0{0,0,0};
        cv::Vec3d p1{0,0,0};
        cv::Vec3d d{0,0,1};  // unit direction p0->p1
        double len{0.0};
    };

    uint64_t id{0};
    pu::SE3 T_world_box{pu::SE3::unit()};
    pu::SE3 T_box_world{pu::SE3::unit()};
    cv::Vec3d size{0,0,0};     // (sx,sy,sz) meters

    std::array<cv::Vec3d, 8> verts{};
    std::array<Face, 6> faces{};
    std::array<Edge, 12> edges{};
};

// face indexing: +X=0, -X=1, +Y=2, -Y=3, +Z=4, -Z=5
inline int faceIndex(int axis, int sign)
{
    if (axis == 0) return (sign > 0) ? 0 : 1;
    if (axis == 1) return (sign > 0) ? 2 : 3;
    return (sign > 0) ? 4 : 5;
}

inline cv::Vec3d closestPointOnSegment3D(const cv::Vec3d& a, const cv::Vec3d& b, const cv::Vec3d& p)
{
    const cv::Vec3d ab = b - a;
    const double L2 = dot3(ab, ab);
    if (L2 < 1e-15) return a;
    double t = dot3(p - a, ab) / L2;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    return a + ab * t;
}

inline bool buildOrthonormalBasisFromT(const cv::Vec3d& t_unit, cv::Vec3d& u_unit, cv::Vec3d& v_unit)
{
    cv::Vec3d tmp = (std::abs(t_unit[2]) < 0.9) ? cv::Vec3d(0,0,1) : cv::Vec3d(0,1,0);
    u_unit = cross3(tmp, t_unit);
    if (!normalize3(u_unit)) return false;
    v_unit = cross3(t_unit, u_unit);
    return normalize3(v_unit);
}

inline cv::Vec2d proj2D(const cv::Vec3d& p, const cv::Vec3d& u_unit, const cv::Vec3d& v_unit)
{
    return cv::Vec2d(dot3(p, u_unit), dot3(p, v_unit));
}

inline double dot2(const cv::Vec2d& a, const cv::Vec2d& b) { return a[0]*b[0] + a[1]*b[1]; }

inline double distPointSegment2DSq(const cv::Vec2d& p, const cv::Vec2d& a, const cv::Vec2d& b)
{
    const cv::Vec2d ab = b - a;
    const double L2 = dot2(ab, ab);
    if (L2 < 1e-15) return dot2(p - a, p - a);

    double t = dot2(p - a, ab) / L2;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    const cv::Vec2d proj = a + ab * t;
    const cv::Vec2d d = p - proj;
    return dot2(d, d);
}

// segment intersection (non-strict; treat touching as intersect) to make distance robust
inline bool segmentsIntersect2D( 
    const cv::Vec2d& a, const cv::Vec2d& b,
    const cv::Vec2d& c, const cv::Vec2d& d,
    double eps = 1e-9)
{
    auto orient = [&](const cv::Vec2d& p, const cv::Vec2d& q, const cv::Vec2d& r)->double
    {
        return (q[0]-p[0])*(r[1]-p[1]) - (q[1]-p[1])*(r[0]-p[0]);
    };

    auto onSeg = [&](const cv::Vec2d& p, const cv::Vec2d& q, const cv::Vec2d& r)->bool
    {
        // r on segment pq (assuming collinear)
        return (std::min(p[0],q[0]) - eps <= r[0] && r[0] <= std::max(p[0],q[0]) + eps &&
                std::min(p[1],q[1]) - eps <= r[1] && r[1] <= std::max(p[1],q[1]) + eps);
    };

    const double o1 = orient(a,b,c);
    const double o2 = orient(a,b,d);
    const double o3 = orient(c,d,a);
    const double o4 = orient(c,d,b);

    // general case
    if ((o1*o2 < -eps) && (o3*o4 < -eps)) return true;

    // collinear / touching cases
    if (std::abs(o1) <= eps && onSeg(a,b,c)) return true;
    if (std::abs(o2) <= eps && onSeg(a,b,d)) return true;
    if (std::abs(o3) <= eps && onSeg(c,d,a)) return true;
    if (std::abs(o4) <= eps && onSeg(c,d,b)) return true;

    return false;
}

// min distance between two segments (squared)
inline double segmentSegmentDistance2DSq(
    const cv::Vec2d& a, const cv::Vec2d& b,
    const cv::Vec2d& c, const cv::Vec2d& d)
{
    if (segmentsIntersect2D(a,b,c,d)) return 0.0;

    double best = std::numeric_limits<double>::infinity();
    best = std::min(best, distPointSegment2DSq(a, c, d));
    best = std::min(best, distPointSegment2DSq(b, c, d));
    best = std::min(best, distPointSegment2DSq(c, a, b));
    best = std::min(best, distPointSegment2DSq(d, a, b));
    return best;
}

// --------------------------- detector ---------------------------

class SeamDetector
{
public:
    struct Params
    {
        // meters
        double pos_eps = 0.005;            // 5 mm
        double plane_eps = 0.005;          // 5 mm
        double min_seam_len = 0.010;       // 10 mm
        double face_clip_expand = 0.0005;  // 0.5 mm

        // degrees
        double parallel_angle_eps_deg = 5.0;   // allow up to ~5° pose error
        double edge_on_face_angle_eps_deg = 5.0;

        // numeric
        // double bisector_eps = 1e-6;     // for ||nA+nB||
        double bisector_eps = 0.3;     // for ||nA+nB||
        double match_ambiguity_eps = 0.15; // used only inside pairing function (optional)
    };

    explicit SeamDetector(Params p) : params_(p) {}

    std::vector<Seam> detectSeams(
        const std::vector<sdh::DetectedBox>& detected_boxes,
        const std::vector<sdh::RegisteredBox>& registered_boxes) const
    {
        // Build id->size map
        std::unordered_map<uint64_t, cv::Vec3d> id_to_size;
        id_to_size.reserve(registered_boxes.size());
        for (const auto& rb : registered_boxes)
            id_to_size[rb.id] = cv::Vec3d(rb.size_x, rb.size_y, rb.size_z);

        // Build geometries
        std::vector<BoxGeom> boxes;
        boxes.reserve(detected_boxes.size());
        for (const auto& db : detected_boxes)
        {
            auto it = id_to_size.find(db.id);
            if (it == id_to_size.end()) continue; // unknown shape id
            boxes.push_back(buildBoxGeom(db, it->second));
        }

        const int N = static_cast<int>(boxes.size());
        std::vector<std::vector<bool>> used_edge(N, std::vector<bool>(12, false));

        std::vector<Seam> seams;
        seams.reserve(256);

        // Pairwise
        for (int i = 0; i < N; ++i)
        {
            for (int j = i + 1; j < N; ++j)
            {
                // Phase 1: edge-edge
                detectEdgeEdge(boxes[i], boxes[j], i, j, used_edge, seams);

                // Phase 2: edge-face (both directions), skipping edges already used by edge-edge
                detectEdgeFace(boxes[i], boxes[j], i, j, used_edge, seams); // edges(i)->faces(j)
                detectEdgeFace(boxes[j], boxes[i], j, i, used_edge, seams); // edges(j)->faces(i)
            }
        }
        return seams;
    }

private:
    Params params_;

    // ------------ Pose conversion ------------
    static pu::SE3 poseToSE3(const sdh::Pose& p)
    {
        // Pose stores qx,qy,qz,qw ; SE3 expects [qw,qx,qy,qz]
        cv::Vec4d q(p.qw, p.qx, p.qy, p.qz);
        cv::Vec3d t(p.x, p.y, p.z);

        return pu::SE3::fromQuatTvec(q, t, true);
    }

    // ------------ Build cube topology ------------
    static cv::Vec3d vertexLocalFromBits(int idx, const cv::Vec3d& half)
    {
        const double sx = (idx & 1) ? +half[0] : -half[0];
        const double sy = (idx & 2) ? +half[1] : -half[1];
        const double sz = (idx & 4) ? +half[2] : -half[2];
        return cv::Vec3d(sx, sy, sz);
    }

    static BoxGeom buildBoxGeom(const sdh::DetectedBox& db, const cv::Vec3d& size) 
    {
        BoxGeom g;
        g.id = db.id;
        g.size = size;
        g.T_world_box = poseToSE3(db.pose);
        g.T_box_world = g.T_world_box.inverse();

        const cv::Vec3d half = 0.5 * size;

        // verts
        for (int vi = 0; vi < 8; ++vi)
        {
            const cv::Vec3d v_local = vertexLocalFromBits(vi, half);
            g.verts[vi] = g.T_world_box * v_local;
        }

        // world axes of the box
        const cv::Matx33d& R = g.T_world_box.R;
        cv::Vec3d ex(R(0,0), R(1,0), R(2,0));
        cv::Vec3d ey(R(0,1), R(1,1), R(2,1));
        cv::Vec3d ez(R(0,2), R(1,2), R(2,2));
        // assume R is close to orthonormal (as in your pipeline)
        normalize3(ex); normalize3(ey); normalize3(ez);

        const cv::Vec3d center = g.T_world_box.t;

        // faces
        // +X / -X
        g.faces[0] = BoxGeom::Face{0, +1, ex, center + ex*half[0], ey, ez, half[1], half[2]};
        g.faces[1] = BoxGeom::Face{0, -1, -ex, center - ex*half[0], ey, ez, half[1], half[2]};
        // +Y / -Y
        g.faces[2] = BoxGeom::Face{1, +1, ey, center + ey*half[1], ex, ez, half[0], half[2]};
        g.faces[3] = BoxGeom::Face{1, -1, -ey, center - ey*half[1], ex, ez, half[0], half[2]};
        // +Z / -Z
        g.faces[4] = BoxGeom::Face{2, +1, ez, center + ez*half[2], ex, ey, half[0], half[1]};
        g.faces[5] = BoxGeom::Face{2, -1, -ez, center - ez*half[2], ex, ey, half[0], half[1]};

        // edges (12): pairs of vertices differing by 1 bit
        static constexpr std::array<std::pair<int,int>,12> EDGE_V = {{
            {0,1},{2,3},{4,5},{6,7}, // along x
            {0,2},{1,3},{4,6},{5,7}, // along y
            {0,4},{1,5},{2,6},{3,7}  // along z
        }};

        for (int ei = 0; ei < 12; ++ei)
        {
            const int v0 = EDGE_V[ei].first;
            const int v1 = EDGE_V[ei].second;

            const int x = v0 ^ v1; // 1,2,4
            int axis = 0;
            if (x == 1) axis = 0;
            else if (x == 2) axis = 1;
            else axis = 2;

            // fixed signs for the other two axes from v0 bits
            int f0=-1, f1=-1;
            if (axis == 0)
            {
                const int ybit = (v0 & 2);
                const int zbit = (v0 & 4);
                const int ysign = ybit ? +1 : -1;
                const int zsign = zbit ? +1 : -1;
                f0 = faceIndex(1, ysign);
                f1 = faceIndex(2, zsign);
            }
            else if (axis == 1)
            {
                const int xbit = (v0 & 1);
                const int zbit = (v0 & 4);
                const int xsign = xbit ? +1 : -1;
                const int zsign = zbit ? +1 : -1;
                f0 = faceIndex(0, xsign);
                f1 = faceIndex(2, zsign);
            }
            else
            {
                const int xbit = (v0 & 1);
                const int ybit = (v0 & 2);
                const int xsign = xbit ? +1 : -1;
                const int ysign = ybit ? +1 : -1;
                f0 = faceIndex(0, xsign);
                f1 = faceIndex(1, ysign);
            }

            BoxGeom::Edge e;
            e.v0 = v0; e.v1 = v1;
            e.f0 = f0; e.f1 = f1;
            e.p0 = g.verts[v0];
            e.p1 = g.verts[v1];
            cv::Vec3d d = e.p1 - e.p0;
            e.len = norm3(d);
            if (e.len > 1e-12) d *= (1.0 / e.len);
            e.d = d;
            g.edges[ei] = e;
        }

        return g;
    }

    std::array<std::pair<int,int>,2> matchIncidentFacesBy2DCrossingRule(
        const BoxGeom& A, const BoxGeom::Edge& ea,
        const BoxGeom& B, const BoxGeom::Edge& eb,
        const cv::Vec3d& seam_t_unit,
        const cv::Vec3d& seam_mid_world) const
    {
        // Build 2D basis in plane ⟂ seam tangent
        cv::Vec3d u, v;
        if (!buildOrthonormalBasisFromT(seam_t_unit, u, v))
        {
            // degenerate; fall back to trivial pairing (won't matter much, bisector will likely skip)
            return { std::make_pair(ea.f0, eb.f0), std::make_pair(ea.f1, eb.f1) };
        }

        // Seam-local points on the *actual edge segments* (important with tilt)
        const cv::Vec3d pa = closestPointOnSegment3D(ea.p0, ea.p1, seam_mid_world);
        const cv::Vec3d pb = closestPointOnSegment3D(eb.p0, eb.p1, seam_mid_world);

        // Offset magnitude: a small fraction of box size (stable for ~40mm cubes)
        const double minA = std::min(A.size[0], std::min(A.size[1], A.size[2]));
        const double minB = std::min(B.size[0], std::min(B.size[1], B.size[2]));
        double off = 0.25 * std::min(minA, minB);        // e.g. 10mm for 40mm cube
        off = std::max(off, 0.005);                      // at least 5mm
        off = std::min(off, 0.020);                      // at most 20mm (safety)

        // Build 2 points per box representing the two incident faces around the seam
        const int a0 = ea.f0, a1 = ea.f1;
        const int b0 = eb.f0, b1 = eb.f1;

        const cv::Vec3d pA0w = pa + A.faces[a0].n * off;
        const cv::Vec3d pA1w = pa + A.faces[a1].n * off;
        const cv::Vec3d pB0w = pb + B.faces[b0].n * off;
        const cv::Vec3d pB1w = pb + B.faces[b1].n * off;

        // Project to 2D (no need to subtract origin; projection is linear)
        const cv::Vec2d A0 = proj2D(pA0w, u, v);
        const cv::Vec2d A1 = proj2D(pA1w, u, v);
        const cv::Vec2d B0 = proj2D(pB0w, u, v);
        const cv::Vec2d B1 = proj2D(pB1w, u, v);

        const double d0 = segmentSegmentDistance2DSq(A0, B0, A1, B1);
        const double d1 = segmentSegmentDistance2DSq(A0, B1, A1, B0);
        int choice = (d1 > d0) ? 1 : 0;

        if (choice == 0)
            return { std::make_pair(a0, b0), std::make_pair(a1, b1) };
        else
            return { std::make_pair(a0, b1), std::make_pair(a1, b0) };
    }


    // ------------ Edge-edge detection ------------
    void detectEdgeEdge(
        const BoxGeom& A, const BoxGeom& B,
        int idxA, int idxB,
        std::vector<std::vector<bool>>& used_edge,
        std::vector<Seam>& out) const
    {
        const double ang_eps_rad = params_.parallel_angle_eps_deg * (CV_PI / 180.0);
        const double cos_thr = std::cos(ang_eps_rad);

        for (int ea_i = 0; ea_i < 12; ++ea_i)
        {
            const auto& ea = A.edges[ea_i];
            const cv::Vec3d d = ea.d; // unit

            for (int eb_i = 0; eb_i < 12; ++eb_i)
            {
                const auto& eb = B.edges[eb_i];

                const double dd = dot3(d, eb.d);
                if (std::abs(dd) < cos_thr) continue; // not parallel enough

                // distance between parallel lines
                const cv::Vec3d delta = eb.p0 - ea.p0;
                const cv::Vec3d perp = delta - d * dot3(delta, d);
                const double dist = norm3(perp);
                if (dist > params_.pos_eps) continue;

                // 1D overlap on axis d (use dot with d)
                double sa0 = dot3(ea.p0, d);
                double sa1 = dot3(ea.p1, d);
                if (sa0 > sa1) std::swap(sa0, sa1);

                double sb0 = dot3(eb.p0, d);
                double sb1 = dot3(eb.p1, d);
                if (sb0 > sb1) std::swap(sb0, sb1);

                const double s0 = std::max(sa0, sb0);
                const double s1 = std::min(sa1, sb1);
                const double olen = s1 - s0;
                if (olen < params_.min_seam_len) continue;

                // Midline segment between the two noisy edges
                const cv::Vec3d mid_anchor = ea.p0 + 0.5 * perp;
                const double sa0_ref = dot3(ea.p0, d);

                const cv::Vec3d p0 = mid_anchor + d * (s0 - sa0_ref);
                const cv::Vec3d p1 = mid_anchor + d * (s1 - sa0_ref);

                // seam tangent
                cv::Vec3d t = p1 - p0;
                const double tlen = norm3(t);
                if (tlen < params_.min_seam_len) continue;
                t *= (1.0 / tlen);

                // Determine 1:1 face pairing (callable rule; adjust later if needed)
                // const auto face_pairs = matchIncidentFacesBySeparationDir(A, ea, B, eb, t, perp);
                
                // seam midpoint in world
                const cv::Vec3d seam_mid = 0.5 * (p0 + p1);

                // Determine 1:1 face pairing using YOUR 2D crossing rule
                const auto face_pairs = matchIncidentFacesBy2DCrossingRule(A, ea, B, eb, t, seam_mid);


                // Emit seams for both paired face-pairs if bisector is valid
                bool any_emitted = false;
                for (int k = 0; k < 2; ++k)
                {
                    const int fa = face_pairs[k].first;
                    const int fb = face_pairs[k].second;

                    const cv::Vec3d nA = A.faces[fa].n;
                    const cv::Vec3d nB = B.faces[fb].n;

                    cv::Vec3d torch = nA + nB;
                    if (norm3(torch) < params_.bisector_eps) continue; // degenerate
                    normalize3(torch);

                    Seam s;
                    s.p0_world = p0;
                    s.p1_world = p1;
                    s.tangent_world = t;
                    s.torch_dir_world = torch;
                    s.type = Seam::Type::EdgeEdge;
                    s.boxA = idxA; s.boxB = idxB;
                    s.edgeA = ea_i; s.edgeB = eb_i;
                    s.faceA = fa; s.faceB = fb;
                    out.push_back(s);
                    any_emitted = true;
                }

                // Edge-edge wins: mark both edges as "used" if we emitted anything
                if (any_emitted)
                {
                    used_edge[idxA][ea_i] = true;
                    used_edge[idxB][eb_i] = true;
                }
            }
        }
    }

    // ------------ Liang–Barsky 2D clip for rectangle [-hu,hu] x [-hv,hv] ------------
    static bool clipSegmentRect(
        double x0, double y0, double x1, double y1,
        double xmin, double xmax, double ymin, double ymax,
        double& t0, double& t1)
    {
        double dx = x1 - x0;
        double dy = y1 - y0;

        t0 = 0.0; t1 = 1.0;

        auto clip1 = [&](double p, double q) -> bool
        {
            if (std::abs(p) < 1e-15) return q >= 0.0; // parallel
            double r = q / p;
            if (p < 0.0) { if (r > t1) return false; if (r > t0) t0 = r; }
            else         { if (r < t0) return false; if (r < t1) t1 = r; }
            return true;
        };

        // x in [xmin,xmax]
        if (!clip1(-dx, x0 - xmin)) return false;
        if (!clip1(+dx, xmax - x0)) return false;
        // y in [ymin,ymax]
        if (!clip1(-dy, y0 - ymin)) return false;
        if (!clip1(+dy, ymax - y0)) return false;

        return t0 <= t1;
    }

    // ------------ Edge-face detection ------------
    void detectEdgeFace(
        const BoxGeom& EdgeBox, const BoxGeom& FaceBox,
        int idxEdgeBox, int idxFaceBox,
        const std::vector<std::vector<bool>>& used_edge,
        std::vector<Seam>& out) const
    {
        const double ang_eps_rad = params_.edge_on_face_angle_eps_deg * (CV_PI / 180.0);
        const double sin_thr = std::sin(ang_eps_rad);

        for (int e_i = 0; e_i < 12; ++e_i)
        {
            if (used_edge[idxEdgeBox][e_i]) continue; // edge-edge already owns this edge

            const auto& e = EdgeBox.edges[e_i];

            for (int f_i = 0; f_i < 6; ++f_i)
            {
                const auto& f = FaceBox.faces[f_i];

                // edge direction must lie in the face plane (approx)
                if (std::abs(dot3(e.d, f.n)) > sin_thr) continue;

                // signed distances to face plane (plane through f.c with normal f.n)
                const double d0 = dot3(f.n, e.p0 - f.c);
                const double d1 = dot3(f.n, e.p1 - f.c);

                // Simple coplanar-within-eps check (good enough for thesis demo)
                if (std::abs(d0) > params_.plane_eps || std::abs(d1) > params_.plane_eps) continue;

                // project endpoints to plane
                const cv::Vec3d p0p = e.p0 - f.n * d0;
                const cv::Vec3d p1p = e.p1 - f.n * d1;

                // face-local coords
                const cv::Vec3d r0 = p0p - f.c;
                const cv::Vec3d r1 = p1p - f.c;

                const double x0 = dot3(r0, f.u);
                const double y0 = dot3(r0, f.v);
                const double x1 = dot3(r1, f.u);
                const double y1 = dot3(r1, f.v);

                // clip to face rectangle (expand a bit)
                const double ex = params_.face_clip_expand;
                double t0, t1;
                if (!clipSegmentRect(
                        x0, y0, x1, y1,
                        -f.hu - ex, +f.hu + ex,
                        -f.hv - ex, +f.hv + ex,
                        t0, t1))
                    continue;

                const cv::Vec3d seg0 = p0p + (p1p - p0p) * t0;
                const cv::Vec3d seg1 = p0p + (p1p - p0p) * t1;

                cv::Vec3d t = seg1 - seg0;
                const double len = norm3(t);
                if (len < params_.min_seam_len) continue;
                t *= (1.0 / len);

                // Edge-face can yield TWO seams (two incident faces on the edge box)
                const int fa0 = e.f0;
                const int fa1 = e.f1;

                const cv::Vec3d nB = f.n;

                for (int which = 0; which < 2; ++which)
                {
                    const int fa = (which == 0) ? fa0 : fa1;
                    const cv::Vec3d nA = EdgeBox.faces[fa].n;

                    cv::Vec3d torch = nA + nB;
                    if (norm3(torch) < params_.bisector_eps) continue;
                    normalize3(torch);

                    Seam s;
                    s.p0_world = seg0;
                    s.p1_world = seg1;
                    s.tangent_world = t;
                    s.torch_dir_world = torch;
                    s.type = Seam::Type::EdgeFace;

                    s.boxA = idxEdgeBox;
                    s.boxB = idxFaceBox;
                    s.edgeA = e_i;
                    s.edgeB = -1;
                    s.faceA = fa;
                    s.faceB = f_i;

                    out.push_back(s);
                }
            }
        }
    }
};

} // namespace aergo::module::helpers::seams
