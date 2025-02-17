#include <iostream>
#include <vector>
#include <array>
#include <map>
#include <queue>

#include <opencv2/opencv.hpp>
#include <ceres/ceres.h>

#include "logging.h"
#include "defaults.h"

using namespace aergo;

/*

Pen has 13x13mm calibration markers. DPoint multiplies this by 0.97.abort
White around markers is 19x19mm. 
Top of top cube to top of bottom cube is 30mm.
    (DPoint takes middle of top at 10mm from top and middle of bottom 39.5mm from the top)
Cubes are 20x20x20mm.
Pen tip is 170.7mm from pen top.

Let's consider:
    Origin is at the tip
    Z = bottom (makes sense for robot)
    Y = towards the buttons (= side with code 93)
    X = the remaining axis

We will originally define the position relative to the top of the pen.
    Easy to define for the markers.
    Then we will run the pen calibration procedure.
    That will fix marker 92 and the rest will get updated positions. 
    This will cause everything to be defined relative to marker 92 - if it is not glued properly, BAD.
        We will then run the procedure to find the pen tip. When we do, we will transform the pen 
        tracker positions based on the tip (tip will newly be at (0,0,0)). Or we will just remember the 
        transformation from marker to tip. 

    92 ... (0, 0.01, 0.04), rot -90=270
    93 ... (0, 0.01, 0.04), rot 0
    94 ... (0, 0.01, 0.04), rot 90
    95 ... (0, 0.01, 0.04), rot 180

    96 ... (0, 0.01, 0.01), rot -135 = 225
    97 ... (0, 0.01, 0.01), rot -45 = 315
    98 ... (0, 0.01, 0.01), rot 45
    99 ... (0, 0.01, 0.01), rot 135

*/


bool loadCameraCalibration(const std::string& filename, cv::Mat& camera_matrix, cv::Mat& distortion_coefficients)
{
    cv::FileStorage fs(filename, cv::FileStorage::READ);
    
    if (!fs.isOpened())
    {
        LOG_ERROR("Failed to open file for reading. Check if the file exists and has correct permissions.");
        return false; 
    }

    try
    {
        fs["CAMERA_MATRIX"] >> camera_matrix;
        fs["DISTORTION_COEFFICIENTS"] >> distortion_coefficients;

        if (camera_matrix.empty() || distortion_coefficients.empty())
        {
            LOG_ERROR("Camera Matrix or Distortion Coefficients not found in file.");
            return false;
        }

    } 
    catch (const cv::Exception& e)
    {
        LOG_ERROR("Camera Matrix or Distortion Coefficients failed to load, error: " << e.what());
        return false;
    }

    fs.release();

    return true;
}

void displayError(cv::Mat& image, cv::Point2f pos1, cv::Point2f pos2)
{
    int x = (int)pos1.x;
    int y = (int)pos1.y;

    cv::Rect2i roi(x - 25, y - 25, 50, 50);
    cv::Mat cropped = image(roi);
    cv::Mat scaled;
    cv::resize(cropped, scaled, cv::Size(1000, 1000), cv::INTER_CUBIC);

    float p1x = (pos1.x - x + 25) * 20;
    float p1y = (pos1.y - y + 25) * 20;
    float p2x = (pos2.x - x + 25) * 20;
    float p2y = (pos2.y - y + 25) * 20;

    cv::line(scaled, cv::Point2f(p1x - 20, p1y - 20), cv::Point2f(p1x + 20, p1y + 20), cv::Scalar(0, 0, 255));
    cv::line(scaled, cv::Point2f(p1x - 20, p1y + 20), cv::Point2f(p1x + 20, p1y - 20), cv::Scalar(0, 0, 255));

    cv::line(scaled, cv::Point2f(p2x - 20, p2y - 20), cv::Point2f(p2x + 20, p2y + 20), cv::Scalar(0, 255, 0));
    cv::line(scaled, cv::Point2f(p2x - 20, p2y + 20), cv::Point2f(p2x + 20, p2y - 20), cv::Scalar(0, 255, 0)); 

    cv::imshow("Error", scaled);
    cv::waitKey(0);
    cv::destroyAllWindows();
}

struct observed_marker
{
    std::array<cv::Point2f, 4> markers_points_;
    int marker_id_;
};

struct Transformation {
    cv::Mat rotation;    // 3x3 rotation matrix
    cv::Mat translation; // 3x1 translation vector

    Transformation() : rotation(cv::Mat::eye(3, 3, CV_64F)), translation(cv::Mat::zeros(3, 1, CV_64F)) {}

    Transformation(const cv::Mat& rvec, const cv::Mat& tvec) 
    {
        cv::Rodrigues(rvec, rotation);
        translation = tvec.clone();

        rotation.clone().convertTo(rotation, CV_64F);
        translation.clone().convertTo(translation, CV_64F);
    }

    std::pair<cv::Mat, cv::Mat> asRvecTvec()
    {
        cv::Mat rvec;
        cv::Rodrigues(rotation, rvec);  // Convert rotation matrix back to rotation vector
        cv::Mat tvec = translation.clone();  // Clone the translation vector
        return std::make_pair(rvec, tvec);
    }

    /// Overload the multiply operator to perform the transformation compositions
    Transformation operator*(const Transformation& other) const
    {
        Transformation result;
        result.rotation = this->rotation * other.rotation;
        result.translation = this->rotation * other.translation + this->translation;
        return result;
    }

    cv::Point3d operator*(const cv::Point3f& other) const
    {
        cv::Mat other_mat(3, 1, CV_64F); 
        other_mat.at<double>(0) = other.x;
        other_mat.at<double>(1) = other.y;
        other_mat.at<double>(2) = other.z;
        
        cv::Mat res = rotation * other_mat;
        res += translation;

        return cv::Point3d(res.at<double>(0), res.at<double>(1), res.at<double>(2));
    }

    Transformation inverse() const
    {
        Transformation inv;
        cv::transpose(this->rotation, inv.rotation);
        inv.translation = -inv.rotation * this->translation;
        return inv;
    }

    void print() const
    {
        std::cout << "Rotation:\n" << this->rotation << std::endl;
        std::cout << "Translation:\n" << this->translation << std::endl;
    }
};

class TransformationGraph
{
    typedef std::list<std::pair<int, Transformation>> edge_list;

public:

    void addEdge(int start_node, int end_node, Transformation transformation)
    {
        graph_data_[start_node].push_back(std::make_pair(end_node, transformation));
        graph_data_[end_node].push_back(std::make_pair(start_node, transformation.inverse()));
    }

    const edge_list& getEdges(int node) const
    {
        if (!graph_data_.contains(node))
        {
            return empty_list;
        }
        
        return graph_data_.at(node);
    }

    bool containsNode(int node) const
    {
        return graph_data_.contains(node);
    }

private:
    std::map<int, edge_list> graph_data_;
    edge_list empty_list;
};

cv::aruco::ArucoDetector getArucoDetector()
{
    cv::aruco::DetectorParameters detector_parameters;
    detector_parameters.cornerRefinementMethod = cv::aruco::CORNER_REFINE_SUBPIX;

    cv::aruco::ArucoDetector aruco_detector(
        defaults::pen::DICTIONARY, 
        detector_parameters
    );

    return std::move(aruco_detector);
}

std::vector<std::vector<observed_marker>> getObservedMarkers(std::string pathname, bool display=false)
{
    std::vector<std::string> image_paths;
    cv::glob(pathname, image_paths);

    cv::aruco::ArucoDetector aruco_detector = getArucoDetector();

    std::vector<std::vector<observed_marker>> observed_markers;

    for (auto&& path : image_paths)
    {
        cv::Mat image = cv::imread(path);

        cv::Mat gray;
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);

        std::vector<std::vector<cv::Point2f>> corners;
        std::vector<int> ids;

        aruco_detector.detectMarkers(gray, corners, ids);

        std::vector<observed_marker> image_markers;
        for (int i = 0; i < corners.size(); ++i)
        {
            if (defaults::pen::USED_MARKER_IDS.contains(ids[i]))
            {
                observed_marker marker;
                marker.marker_id_ = ids[i];
                marker.markers_points_[0] = corners[i][0];
                marker.markers_points_[1] = corners[i][1];
                marker.markers_points_[2] = corners[i][2];
                marker.markers_points_[3] = corners[i][3];
                
                image_markers.emplace_back(std::move(marker));
            }
        }
        observed_markers.emplace_back(std::move(image_markers));

        if (display)
        {
            cv::aruco::drawDetectedMarkers(image, corners, ids);
            cv::imshow("Image", image);
            cv::waitKey(0);
            cv::destroyAllWindows();
        }
    }

    return observed_markers;
}

void traverseGraph(const TransformationGraph& transf_graph, std::map<int, Transformation>& fixed_marker_to_other_transformations, int start_node)
{
    fixed_marker_to_other_transformations.clear();

    std::queue<std::pair<int, Transformation>> node_queue;
    std::set<int> seen_nodes;
    node_queue.push(std::make_pair(start_node, Transformation()));
    seen_nodes.insert(start_node);
    
    while (!node_queue.empty())
    {
        auto [node, fixed_to_node] = node_queue.front();
        node_queue.pop();

        fixed_marker_to_other_transformations[node] = fixed_to_node;

        for (auto [other, node_to_other] : transf_graph.getEdges(node))
        {
            if (!seen_nodes.contains(other))
            {
                seen_nodes.insert(other);
                Transformation fixed_to_other = fixed_to_node * node_to_other;
                node_queue.push(std::make_pair(other, fixed_to_other));
            }
        }
    }
}

bool calibrateMarkers(cv::Mat& camera_matrix, cv::Mat& distortion_coefficients, std::vector<std::vector<observed_marker>> observed_markers)
{
    std::vector<int> ids;
    std::vector<std::vector<cv::Point2f>> positions;

    std::vector<cv::Point3f> marker_points = 
    {
        cv::Point3f(-defaults::pen::MARKER_SIZE / 2,  defaults::pen::MARKER_SIZE / 2, 0),
        cv::Point3f( defaults::pen::MARKER_SIZE / 2,  defaults::pen::MARKER_SIZE / 2, 0),
        cv::Point3f( defaults::pen::MARKER_SIZE / 2, -defaults::pen::MARKER_SIZE / 2, 0),
        cv::Point3f(-defaults::pen::MARKER_SIZE / 2, -defaults::pen::MARKER_SIZE / 2, 0)
    };

    int max_id = *std::max_element(defaults::pen::USED_MARKER_IDS.begin(), defaults::pen::USED_MARKER_IDS.end());
    int camera_first_id = max_id + 1000;
    int camera_count = 0;
    
    TransformationGraph transf_graph;

    std::set<int> active_cameras;

    for (auto& single_image : observed_markers)
    {
        std::map<int, Transformation> camera_to_marker_transformations;

        for (auto& marker : single_image)
        {
            cv::Mat rvec, tvec;
            cv::solvePnP(marker_points, marker.markers_points_, camera_matrix, distortion_coefficients, rvec, tvec, false, cv::SOLVEPNP_IPPE_SQUARE);

            double angle_off_axis = cv::abs(180 - cv::norm(rvec) * 180.0 / CV_PI);

            if (angle_off_axis < defaults::pen::IGNORE_MARKERS_ABOVE_ANGLE_DEG)
            {
                Transformation camera_to_marker = Transformation(rvec, tvec);
                camera_to_marker_transformations[marker.marker_id_] = camera_to_marker;
            }
            else
            {
                LOG("CAMERA " << camera_count << " ignoring " << marker.marker_id_ << " due to angle " << angle_off_axis << " > " << defaults::pen::IGNORE_MARKERS_ABOVE_ANGLE_DEG)
            }
            
        }

        if (camera_to_marker_transformations.size() >= 2)
        {
            active_cameras.insert(camera_count);
            for (const auto& first : camera_to_marker_transformations)
            {
                int first_key = first.first;
                Transformation camera_to_first = first.second;
                
                transf_graph.addEdge(camera_first_id + camera_count, first_key, camera_to_first);

                for (const auto& second : camera_to_marker_transformations)
                {
                    int second_key = second.first;
                    Transformation camera_to_second = second.second;

                    if (first_key != second_key)
                    {
                        Transformation first_to_second = camera_to_first.inverse() * camera_to_second;
                        transf_graph.addEdge(first_key, second_key, first_to_second);
                    }
                }
            }
        }
        else
        {
            LOG("Not enough markers detected on camera " << camera_count + 1)
        }
            
        ++camera_count;
    }
         
    std::map<int, Transformation> fixed_marker_to_other_transformations;
    traverseGraph(transf_graph, fixed_marker_to_other_transformations, defaults::pen::PEN_FIXED_MARKER_ID);

    std::vector<std::pair<int, int>> opposites = {{92, 94}, {93,95}, {96, 98}, {97,99}};
    for (auto [first, second] : opposites)
    {
        auto fixed_to_first = fixed_marker_to_other_transformations[first];
        auto fixed_to_second = fixed_marker_to_other_transformations[second];
        auto first_to_second = fixed_to_first.inverse() * fixed_to_second;
        auto [rvec, tvec] = first_to_second.asRvecTvec();
        LOG(first << "->" << second << ": " << tvec << " = " << cv::norm(tvec) * 1000 << "mm; " << (cv::norm(rvec) * 180.0 / CV_PI) << "deg\n")
    }

    LOG("\n\nTransform:\n\n92\n\t" << marker_points[0] << "\n\t" << marker_points[1] << "\n\t" << marker_points[2] << "\n\t" << marker_points[3] << "\n\n")

    // check that user photos contain all markers
    for (int marker_id : defaults::pen::USED_MARKER_IDS)
    {
        if (!fixed_marker_to_other_transformations.contains(marker_id))
        {
            LOG("Failed to detect marker " << marker_id << " in the provided photos.")
            return false;
        }

        Transformation fixed_to_marker = fixed_marker_to_other_transformations.at(marker_id);
        LOG("{\n\t'name': '" << marker_id << "',\n\t'points': np.array([\n\t\t" << (fixed_to_marker * marker_points[0]) << ",\n\t\t" << (fixed_to_marker * marker_points[1]) << ",\n\t\t" << (fixed_to_marker * marker_points[2]) << ",\n\t\t" << (fixed_to_marker * marker_points[3]) << "\n\t])\n},")
    }

    LOG("\n\n\nCAMERAS\n\n\n")

    // sanity check, this should never fail
    for (int camera_id : active_cameras)
    {
        if (!fixed_marker_to_other_transformations.contains(camera_first_id + camera_id))
        {
            LOG("Sanity check failed. Camera " << camera_id + 1 << " not in graph.")
            return false;
        }

        
        Transformation fixed_to_camera = fixed_marker_to_other_transformations.at(camera_first_id + camera_id);
        LOG("{\n\t'name': '" << camera_first_id + camera_id << "',\n\t'points': np.array([\n\t\t" << (fixed_to_camera * marker_points[0]) << ",\n\t\t" << (fixed_to_camera * marker_points[1]) << ",\n\t\t" << (fixed_to_camera * marker_points[2]) << ",\n\t\t" << (fixed_to_camera * marker_points[3]) << "\n\t])\n},")
    }
    
    return true;
}

int main(int argc, char* argv[]) {
    if (argc != 3)
    {
        LOG_ERROR("Requires two arguments!")
        return 0;
    }



    // File containing camera calibration data
    std::string filename = argv[2];

    cv::Mat camera_matrix, distortion_coefficients;

    if (loadCameraCalibration(filename, camera_matrix, distortion_coefficients))
    {
        LOG("Loaded Camera Matrix:\n" << camera_matrix << "\n\nLoaded Distortion Coefficients:\n" << distortion_coefficients << "\n")
    } 
    else 
    {
        return -1;
    }



    std::vector<std::vector<observed_marker>> observed_markers = getObservedMarkers(argv[1]);
    calibrateMarkers(camera_matrix, distortion_coefficients, observed_markers);


    return 0;
}