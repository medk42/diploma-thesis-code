#include "pen/pen_board_model.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <string>

using json = nlohmann::json;
using namespace aergo::default_modules::pen_calibration_multicam_module;
using namespace aergo::default_modules::pen_calibration_multicam_module::pen;
namespace pose_utils = aergo::default_modules::robot_stereo_camera_calibration_module::calib::pose_utils;

namespace
{
    calib::SE3 identitySE3()
    {
        return { cv::Matx33d::eye(), cv::Vec3d(0.0, 0.0, 0.0) };
    }

    json se3ToJson(const calib::SE3& T)
    {
        json j;
        j["R"] = { T.R(0,0), T.R(0,1), T.R(0,2),
                   T.R(1,0), T.R(1,1), T.R(1,2),
                   T.R(2,0), T.R(2,1), T.R(2,2) };
        j["t"] = { T.t[0], T.t[1], T.t[2] };
        return j;
    }

    bool jsonToSe3(const json& j, calib::SE3& out, std::string& err)
    {
        if (!j.contains("R") || !j.contains("t") || !j["R"].is_array() || !j["t"].is_array())
        {
            err = "missing R or t";
            return false;
        }

        if (j["R"].size() != 9 || j["t"].size() != 3)
        {
            err = "unexpected R/t size";
            return false;
        }

        calib::SE3 T;
        try
        {
            T.R = cv::Matx33d(
                j["R"][0].get<double>(), j["R"][1].get<double>(), j["R"][2].get<double>(),
                j["R"][3].get<double>(), j["R"][4].get<double>(), j["R"][5].get<double>(),
                j["R"][6].get<double>(), j["R"][7].get<double>(), j["R"][8].get<double>());
            T.t = cv::Vec3d(j["t"][0].get<double>(), j["t"][1].get<double>(), j["t"][2].get<double>());
        }
        catch (const std::exception& e)
        {
            err = e.what();
            return false;
        }

        out = T;
        return true;
    }
}

PenBoardModel::PenBoardModel(
    std::vector<MarkerSpec> markers,
    double marker_size_m,
    int reference_marker_id,
    calib::Vector3 tip_init_P_m,
    int dictionary)
: markers_(std::move(markers))
, ref_id_(reference_marker_id)
, tip_P_(tip_init_P_m)
, dict_id_(dictionary)
, dict_(cv::aruco::getPredefinedDictionary(dictionary))
, marker_size_m_(marker_size_m)
{
    delta_by_id_.reserve(markers_.size());
    delta_by_id_[ref_id_] = identitySE3();

    rebuildMarkerLookup();
}

void PenBoardModel::setDelta(int marker_id, const calib::SE3& dT_P_P)
{
    if (marker_id == ref_id_)
    {
        return;
    }
    delta_by_id_[marker_id] = dT_P_P;
}

calib::SE3 PenBoardModel::delta(int marker_id) const
{
    if (marker_id == ref_id_)
    {
        return identitySE3();
    }

    auto it = delta_by_id_.find(marker_id);
    if (it == delta_by_id_.end())
    {
        return identitySE3();
    }

    return it->second;
}

calib::SE3 PenBoardModel::T_P_M_cad(int marker_id) const
{
    auto it = marker_by_id_.find(marker_id);
    if (it == marker_by_id_.end())
    {
        return identitySE3();
    }
    return it->second;
}

calib::SE3 PenBoardModel::T_P_M(int marker_id) const
{
    const auto d = delta(marker_id);
    const auto T_P_M_cad_ = T_P_M_cad(marker_id);
    return pose_utils::compose(d, T_P_M_cad_);
}

cv::Ptr<cv::aruco::Board> PenBoardModel::buildCvBoard() const
{
    std::vector<std::vector<cv::Point3f>> obj_points;
    std::vector<int> ids;
    obj_points.reserve(markers_.size());
    ids.reserve(markers_.size());

    const float half = static_cast<float>(marker_size_m_ * 0.5);
    const std::array<cv::Point3f, 4> square = {
        cv::Point3f(-half,  half, 0.0f),
        cv::Point3f( half,  half, 0.0f),
        cv::Point3f( half, -half, 0.0f),
        cv::Point3f(-half, -half, 0.0f)
    };

    for (const auto& m : markers_)
    {
        const auto T = T_P_M(m.id);
        std::vector<cv::Point3f> corners;
        corners.reserve(4);
        for (const auto& p_M : square)
        {
            const cv::Vec3d p_M_d{ p_M.x, p_M.y, p_M.z };
            const auto p_P = T.R * p_M_d + T.t;
            corners.emplace_back(static_cast<float>(p_P[0]), static_cast<float>(p_P[1]), static_cast<float>(p_P[2]));
        }

        obj_points.push_back(std::move(corners));
        ids.push_back(m.id);
    }

    return cv::makePtr<cv::aruco::Board>(obj_points, dict_, ids);
}

std::string PenBoardModel::toJson() const
{
    json j;
    j["marker_size_m"] = marker_size_m_;
    j["ref_id"] = ref_id_;
    j["tip_P"] = { tip_P_.x, tip_P_.y, tip_P_.z };
    j["dict_id"] = dict_id_;

    json j_markers = json::array();
    for (const auto& m : markers_)
    {
        json jm;
        jm["id"] = m.id;
        jm["T"] = se3ToJson(m.T_P_M_cad);
        j_markers.push_back(jm);
    }
    j["markers"] = std::move(j_markers);

    json j_deltas = json::array();
    for (const auto& kv : delta_by_id_)
    {
        if (kv.first == ref_id_) continue;
        json jd;
        jd["id"] = kv.first;
        jd["T"] = se3ToJson(kv.second);
        j_deltas.push_back(jd);
    }
    j["deltas"] = std::move(j_deltas);

    return j.dump();
}

bool PenBoardModel::fromJson(const std::string& s, PenBoardModel& out, std::string& err)
{
    json j;
    try
    {
        j = json::parse(s);
    }
    catch (const std::exception& e)
    {
        err = e.what();
        return false;
    }

    if (!j.contains("marker_size_m") || !j.contains("ref_id") || !j.contains("tip_P") || !j.contains("markers"))
    {
        err = "missing required fields";
        return false;
    }

    double marker_size_m = j["marker_size_m"].get<double>();
    int ref_id = j["ref_id"].get<int>();

    if (!j["tip_P"].is_array() || j["tip_P"].size() != 3)
    {
        err = "tip_P malformed";
        return false;
    }
    calib::Vector3 tip{ j["tip_P"][0].get<double>(), j["tip_P"][1].get<double>(), j["tip_P"][2].get<double>() };

    if (!j["markers"].is_array() || j["markers"].empty())
    {
        err = "markers malformed";
        return false;
    }

    std::vector<MarkerSpec> markers;
    markers.reserve(j["markers"].size());
    for (const auto& jm : j["markers"])
    {
        if (!jm.contains("id") || !jm.contains("T"))
        {
            err = "marker entry missing id or T";
            return false;
        }

        MarkerSpec m;
        m.id = jm["id"].get<int>();
        if (!jsonToSe3(jm["T"], m.T_P_M_cad, err))
        {
            return false;
        }
        markers.push_back(m);
    }

    int dict_id = cv::aruco::DICT_4X4_100;
    if (j.contains("dict_id"))
    {
        dict_id = j["dict_id"].get<int>();
    }
    else
    {
        err = "dict_id missing";
        return false;
    }

    PenBoardModel model(markers, marker_size_m, ref_id, tip, dict_id);

    if (j.contains("deltas") && j["deltas"].is_array())
    {
        for (const auto& jd : j["deltas"])
        {
            if (!jd.contains("id") || !jd.contains("T"))
            {
                err = "delta entry missing fields";
                return false;
            }
            int id = jd["id"].get<int>();
            calib::SE3 dT;
            if (!jsonToSe3(jd["T"], dT, err))
            {
                return false;
            }
            model.setDelta(id, dT);
        }
    }

    model.rebuildMarkerLookup();
    out = std::move(model);
    return true;
}

void PenBoardModel::rebuildMarkerLookup()
{
    marker_by_id_.clear();
    marker_by_id_.reserve(markers_.size());
    for (const auto& m : markers_)
    {
        marker_by_id_[m.id] = m.T_P_M_cad;
    }
    // Ensure reference marker delta is identity.
    delta_by_id_[ref_id_] = identitySE3();
}
