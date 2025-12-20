#pragma once
#include <opencv2/aruco.hpp>
#include <opencv2/objdetect/aruco_detector.hpp>

#include <string>
#include <unordered_map>
#include <vector>

#include "calib/types.h"
#include "calib/pose_utils.h"

namespace aergo::default_modules::pen_calibration_multicam_module::pen {

    class PenBoardModel {
    public:
        struct MarkerSpec
        {
            int id{ -1 };
            calib::SE3 T_P_M_cad;
        };

        PenBoardModel(
            std::vector<MarkerSpec> markers,
            double marker_size_m,       
            int reference_marker_id,
            calib::Vector3 tip_init_P_m,     
            int dictionary
        );

        // Access
        const std::vector<MarkerSpec>& cadMarkers() const { return markers_; }
        double markerSizeM() const { return marker_size_m_; }
        int dictId() const { return dict_id_; }
        
        int referenceMarkerId() const { return ref_id_; }
        calib::Vector3 tip_P() const { return tip_P_; }
        void setTip_P(const calib::Vector3& v) { tip_P_ = v; }

        // Small per-marker deltas (Pen <- Pen), left-multiplied: T_P_M = Δ ∘ T_P_M_cad
        void setDelta(int marker_id, const calib::SE3& dT_P_P);     // Δ for id (no-op if ref id)
        calib::SE3 delta(int marker_id) const;                      // identity for ref id
        calib::SE3 T_P_M(int marker_id) const;                      // Δ ∘ CAD
        calib::SE3 T_P_M_cad(int marker_id) const;                  // CAD pose

        // Build an OpenCV board in the **Pen** frame (for estimatePoseBoard fallback)
        cv::Ptr<cv::aruco::Board> buildCvBoard() const;

        // JSON I/O (return/consume compact JSON string; validation inside)
        std::string toJson() const;
        static bool fromJson(const std::string& s, PenBoardModel& out, std::string& err);

    private:
        void rebuildMarkerLookup();
        std::vector<MarkerSpec> markers_;
        int ref_id_{-1};
        std::unordered_map<int, calib::SE3> marker_by_id_; // id -> cad pose   T_Pos_MarkerCad
        std::unordered_map<int, calib::SE3> delta_by_id_; // ref id -> identity  Δ
        calib::Vector3 tip_P_;
        int dict_id_;
        cv::aruco::Dictionary dict_;
        double marker_size_m_{0.0};
    };

} // namespace pen
