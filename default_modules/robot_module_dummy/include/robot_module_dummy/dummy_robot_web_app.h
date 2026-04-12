#pragma once

#include <Wt/WApplication.h>

#include <array>
#include <memory>
#include <string>

namespace Wt
{
    class WLineEdit;
    class WText;
    class WTimer;
}

namespace aergo::default_modules::robot_module_dummy
{
    class RobotModuleDummy;

    class DummyRobotWebApp : public Wt::WApplication
    {
    public:
        DummyRobotWebApp(const Wt::WEnvironment& env, RobotModuleDummy* module);

    private:
        void buildUi();
        void refreshSnapshot();
        void fillMoveJFromCurrent();
        void fillMoveLFromCurrent();
        void startMoveJFromInputs();
        void startMoveLFromInputs();
        void jogJoint(std::size_t joint_index, double direction);
        void jogCartesian(std::size_t axis_index, double direction);
        void jogJointFixed(std::size_t joint_index, double step_deg);
        void jogCartesianFixed(std::size_t axis_index, double step_ui_units);
        void cancelMove();
        void setMessage(const std::string& text, bool is_error);
        void syncCartesianReferenceFromSnapshot();
        void writeCartesianReferenceToInputs() const;

        static std::string formatFixed(double value, int precision = 3);
        static double parseLineEditOrDefault(Wt::WLineEdit* edit, double fallback);

        RobotModuleDummy* module_{nullptr};

        Wt::WText* status_text_{nullptr};
        Wt::WText* message_text_{nullptr};
        std::array<Wt::WText*, 6> current_joint_text_{};
        std::array<Wt::WText*, 6> current_tfc_text_{};
        std::array<Wt::WText*, 6> current_tcp_text_{};

        std::array<Wt::WLineEdit*, 6> movej_inputs_{};
        Wt::WLineEdit* movej_speed_input_{nullptr};

        std::array<Wt::WLineEdit*, 6> movel_inputs_{};
        Wt::WLineEdit* movel_speed_input_{nullptr};

        Wt::WLineEdit* joint_jog_step_input_{nullptr};
        Wt::WLineEdit* joint_jog_speed_input_{nullptr};
        Wt::WLineEdit* cartesian_pos_step_input_{nullptr};
        Wt::WLineEdit* cartesian_rot_step_input_{nullptr};
        Wt::WLineEdit* cartesian_speed_input_{nullptr};

        std::unique_ptr<Wt::WTimer> refresh_timer_;
        bool cartesian_reference_valid_{false};
        std::array<double, 6> cartesian_reference_xyzrpy_{};
    };
}
