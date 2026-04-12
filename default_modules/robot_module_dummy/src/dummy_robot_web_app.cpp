#include "robot_module_dummy/dummy_robot_web_app.h"

#include "robot_module_dummy/robot_module_dummy.h"

#include <Wt/WContainerWidget.h>
#include <Wt/WLineEdit.h>
#include <Wt/WPushButton.h>
#include <Wt/WTable.h>
#include <Wt/WText.h>
#include <Wt/WTimer.h>

#include <cmath>
#include <numbers>
#include <sstream>

namespace aergo::default_modules::robot_module_dummy
{
    namespace
    {
        constexpr double kRadToDeg = 180.0 / std::numbers::pi;
        constexpr double kDegToRad = std::numbers::pi / 180.0;
        constexpr double kMToMm = 1000.0;
        constexpr double kMmToM = 0.001;

        const char* kPoseLabels[6] = { "X [mm]", "Y [mm]", "Z [mm]", "Roll [deg]", "Pitch [deg]", "Yaw [deg]" };
        const char* kJointLabels[6] = { "J1 [deg]", "J2 [deg]", "J3 [deg]", "J4 [deg]", "J5 [deg]", "J6 [deg]" };
        const char* kAxisShortLabels[6] = { "X", "Y", "Z", "R", "P", "Y" };
    }

    DummyRobotWebApp::DummyRobotWebApp(const Wt::WEnvironment& env, RobotModuleDummy* module)
        : Wt::WApplication(env), module_(module)
    {
        setTitle("Dummy Robot Control");
        useStyleSheet("dummy_robot.css");
        buildUi();
        refreshSnapshot();
    }

    void DummyRobotWebApp::buildUi()
    {
        root()->clear();
        root()->setStyleClass("dummy-ui");

        auto header = root()->addWidget(std::make_unique<Wt::WContainerWidget>());
        header->setStyleClass("dummy-panel");
        header->addWidget(std::make_unique<Wt::WText>("<div class='dummy-title'>Dummy Robot Control</div><div class='dummy-subtitle'>Standalone showcase jogging and direct move interface</div>"))->setTextFormat(Wt::TextFormat::XHTML);
        status_text_ = header->addWidget(std::make_unique<Wt::WText>("Status: unavailable"));
        status_text_->setStyleClass("dummy-status-main");
        message_text_ = header->addWidget(std::make_unique<Wt::WText>(""));
        message_text_->setStyleClass("dummy-message");

        auto current_wrap = root()->addWidget(std::make_unique<Wt::WContainerWidget>());
        current_wrap->setStyleClass("dummy-card-grid");
        for (int section = 0; section < 3; ++section)
        {
            auto panel = current_wrap->addWidget(std::make_unique<Wt::WContainerWidget>());
            panel->setStyleClass("dummy-panel");
            const char* titles[3] = { "Current Joints", "Current TFC", "Current TCP" };
            panel->addWidget(std::make_unique<Wt::WText>(std::string("<div class='dummy-section-title'>") + titles[section] + "</div>"))->setTextFormat(Wt::TextFormat::XHTML);

            auto table = panel->addWidget(std::make_unique<Wt::WTable>());
            table->setStyleClass("dummy-grid");
            for (int i = 0; i < 6; ++i)
            {
                const char* label = (section == 0) ? kJointLabels[i] : kPoseLabels[i];
                table->elementAt(i, 0)->setStyleClass("label");
                table->elementAt(i, 0)->addWidget(std::make_unique<Wt::WText>(label));
                table->elementAt(i, 1)->setStyleClass("value");
                if (section == 0)
                {
                    current_joint_text_[static_cast<std::size_t>(i)] = table->elementAt(i, 1)->addWidget(std::make_unique<Wt::WText>("0.0"));
                }
                else if (section == 1)
                {
                    current_tfc_text_[static_cast<std::size_t>(i)] = table->elementAt(i, 1)->addWidget(std::make_unique<Wt::WText>("0.0"));
                }
                else
                {
                    current_tcp_text_[static_cast<std::size_t>(i)] = table->elementAt(i, 1)->addWidget(std::make_unique<Wt::WText>("0.0"));
                }
            }
        }

        auto jog_wrap = root()->addWidget(std::make_unique<Wt::WContainerWidget>());
        jog_wrap->setStyleClass("dummy-card-grid-2");

        auto cart_panel = jog_wrap->addWidget(std::make_unique<Wt::WContainerWidget>());
        cart_panel->setStyleClass("dummy-panel");
        cart_panel->addWidget(std::make_unique<Wt::WText>("<div class='dummy-section-title'>Cartesian Jog</div>"))->setTextFormat(Wt::TextFormat::XHTML);
        auto cart_settings = cart_panel->addWidget(std::make_unique<Wt::WContainerWidget>());
        cart_settings->setStyleClass("dummy-inline");
        cart_settings->addWidget(std::make_unique<Wt::WText>("Speed [mm/s]"));
        cartesian_speed_input_ = cart_settings->addWidget(std::make_unique<Wt::WLineEdit>("50.0"));
        cartesian_speed_input_->setStyleClass("dummy-input");
        cart_settings->addWidget(std::make_unique<Wt::WText>("Fine [mm]"));
        cartesian_pos_step_input_ = cart_settings->addWidget(std::make_unique<Wt::WLineEdit>("5.0"));
        cartesian_pos_step_input_->setStyleClass("dummy-input");
        cart_settings->addWidget(std::make_unique<Wt::WText>("Fine rot [deg]"));
        cartesian_rot_step_input_ = cart_settings->addWidget(std::make_unique<Wt::WLineEdit>("2.0"));
        cartesian_rot_step_input_->setStyleClass("dummy-input");

        auto cart_table = cart_panel->addWidget(std::make_unique<Wt::WTable>());
        cart_table->setStyleClass("dummy-controls");
        const double cart_steps[3] = { 100.0, 25.0, 5.0 };
        const double rot_steps[3] = { 30.0, 10.0, 2.0 };
        const double cart_steps_positive[3] = { 5.0, 25.0, 100.0 };
        const double rot_steps_positive[3] = { 2.0, 10.0, 30.0 };
        for (int i = 0; i < 6; ++i)
        {
            cart_table->elementAt(i, 0)->setStyleClass("axis-label");
            cart_table->elementAt(i, 0)->addWidget(std::make_unique<Wt::WText>(kPoseLabels[i]));

            auto left_group = cart_table->elementAt(i, 1)->addWidget(std::make_unique<Wt::WContainerWidget>());
            left_group->setStyleClass("step-group");
            auto center = cart_table->elementAt(i, 2)->addWidget(std::make_unique<Wt::WPushButton>(i < 3 ? "TCP" : "ORI"));
            center->setStyleClass("dummy-home-btn");
            const auto* step_array = (i < 3) ? cart_steps : rot_steps;
            for (int step_idx = 0; step_idx < 3; ++step_idx)
            {
                const double step = step_array[step_idx];
                auto btn = left_group->addWidget(std::make_unique<Wt::WPushButton>(std::string(kAxisShortLabels[i]) + "-" + formatFixed(step, 0)));
                btn->setStyleClass("dummy-btn");
                btn->clicked().connect([this, i, step] { jogCartesianFixed(static_cast<std::size_t>(i), -step); });
            }

            auto right_group = cart_table->elementAt(i, 3)->addWidget(std::make_unique<Wt::WContainerWidget>());
            right_group->setStyleClass("step-group");
            const auto* positive_step_array = (i < 3) ? cart_steps_positive : rot_steps_positive;
            for (int step_idx = 0; step_idx < 3; ++step_idx)
            {
                const double step = positive_step_array[step_idx];
                auto btn = right_group->addWidget(std::make_unique<Wt::WPushButton>(std::string(kAxisShortLabels[i]) + "+" + formatFixed(step, 0)));
                btn->setStyleClass("dummy-btn");
                btn->clicked().connect([this, i, step] { jogCartesianFixed(static_cast<std::size_t>(i), step); });
            }
        }

        auto joint_panel = jog_wrap->addWidget(std::make_unique<Wt::WContainerWidget>());
        joint_panel->setStyleClass("dummy-panel");
        joint_panel->addWidget(std::make_unique<Wt::WText>("<div class='dummy-section-title'>Joint Jog</div>"))->setTextFormat(Wt::TextFormat::XHTML);
        auto joint_settings = joint_panel->addWidget(std::make_unique<Wt::WContainerWidget>());
        joint_settings->setStyleClass("dummy-inline");
        joint_settings->addWidget(std::make_unique<Wt::WText>("Speed [deg/s]"));
        joint_jog_speed_input_ = joint_settings->addWidget(std::make_unique<Wt::WLineEdit>("30.0"));
        joint_jog_speed_input_->setStyleClass("dummy-input");
        joint_settings->addWidget(std::make_unique<Wt::WText>("Fine [deg]"));
        joint_jog_step_input_ = joint_settings->addWidget(std::make_unique<Wt::WLineEdit>("1.0"));
        joint_jog_step_input_->setStyleClass("dummy-input");

        auto joint_table = joint_panel->addWidget(std::make_unique<Wt::WTable>());
        joint_table->setStyleClass("dummy-controls");
        const double joint_steps[3] = { 15.0, 5.0, 1.0 };
        const double joint_steps_positive[3] = { 1.0, 5.0, 15.0 };
        for (int i = 0; i < 6; ++i)
        {
            joint_table->elementAt(i, 0)->setStyleClass("axis-label");
            joint_table->elementAt(i, 0)->addWidget(std::make_unique<Wt::WText>(kAxisShortLabels[i]));

            auto left_group = joint_table->elementAt(i, 1)->addWidget(std::make_unique<Wt::WContainerWidget>());
            left_group->setStyleClass("step-group");
            for (double step : joint_steps)
            {
                auto btn = left_group->addWidget(std::make_unique<Wt::WPushButton>(std::string("-") + formatFixed(step, 0)));
                btn->setStyleClass("dummy-btn");
                btn->clicked().connect([this, i, step] { jogJointFixed(static_cast<std::size_t>(i), -step); });
            }

            auto center = joint_table->elementAt(i, 2)->addWidget(std::make_unique<Wt::WPushButton>(kJointLabels[i]));
            center->setStyleClass("dummy-home-btn");

            auto right_group = joint_table->elementAt(i, 3)->addWidget(std::make_unique<Wt::WContainerWidget>());
            right_group->setStyleClass("step-group");
            for (int step_idx = 0; step_idx < 3; ++step_idx)
            {
                const double step = joint_steps_positive[step_idx];
                auto btn = right_group->addWidget(std::make_unique<Wt::WPushButton>(std::string("+") + formatFixed(step, 0)));
                btn->setStyleClass("dummy-btn");
                btn->clicked().connect([this, i, step] { jogJointFixed(static_cast<std::size_t>(i), step); });
            }
        }

        auto forms_wrap = root()->addWidget(std::make_unique<Wt::WContainerWidget>());
        forms_wrap->setStyleClass("dummy-card-grid-2");

        auto movej_panel = forms_wrap->addWidget(std::make_unique<Wt::WContainerWidget>());
        movej_panel->setStyleClass("dummy-panel");
        movej_panel->addWidget(std::make_unique<Wt::WText>("<div class='dummy-section-title'>Direct MoveJ</div>"))->setTextFormat(Wt::TextFormat::XHTML);
        auto movej_table = movej_panel->addWidget(std::make_unique<Wt::WTable>());
        movej_table->setStyleClass("dummy-form");
        for (int i = 0; i < 6; ++i)
        {
            movej_table->elementAt(i, 0)->setStyleClass("form-label");
            movej_table->elementAt(i, 0)->addWidget(std::make_unique<Wt::WText>(kJointLabels[i]));
            movej_inputs_[static_cast<std::size_t>(i)] = movej_table->elementAt(i, 1)->addWidget(std::make_unique<Wt::WLineEdit>("0.0"));
            movej_inputs_[static_cast<std::size_t>(i)]->setStyleClass("dummy-input");
        }
        movej_table->elementAt(6, 0)->setStyleClass("form-label");
        movej_table->elementAt(6, 0)->addWidget(std::make_unique<Wt::WText>("Speed [deg/s]"));
        movej_speed_input_ = movej_table->elementAt(6, 1)->addWidget(std::make_unique<Wt::WLineEdit>("30.0"));
        movej_speed_input_->setStyleClass("dummy-input");
        auto movej_actions = movej_table->elementAt(7, 1)->addWidget(std::make_unique<Wt::WContainerWidget>());
        movej_actions->setStyleClass("dummy-inline");
        auto fill_joints = movej_actions->addWidget(std::make_unique<Wt::WPushButton>("Use Current"));
        fill_joints->setStyleClass("dummy-fill-btn");
        auto send_movej = movej_actions->addWidget(std::make_unique<Wt::WPushButton>("MoveJ"));
        send_movej->setStyleClass("dummy-home-btn");
        fill_joints->clicked().connect([this] { fillMoveJFromCurrent(); });
        send_movej->clicked().connect([this] { startMoveJFromInputs(); });

        auto movel_panel = forms_wrap->addWidget(std::make_unique<Wt::WContainerWidget>());
        movel_panel->setStyleClass("dummy-panel");
        movel_panel->addWidget(std::make_unique<Wt::WText>("<div class='dummy-section-title'>Direct MoveL (TCP)</div>"))->setTextFormat(Wt::TextFormat::XHTML);
        auto movel_table = movel_panel->addWidget(std::make_unique<Wt::WTable>());
        movel_table->setStyleClass("dummy-form");
        for (int i = 0; i < 6; ++i)
        {
            movel_table->elementAt(i, 0)->setStyleClass("form-label");
            movel_table->elementAt(i, 0)->addWidget(std::make_unique<Wt::WText>(kPoseLabels[i]));
            movel_inputs_[static_cast<std::size_t>(i)] = movel_table->elementAt(i, 1)->addWidget(std::make_unique<Wt::WLineEdit>("0.0"));
            movel_inputs_[static_cast<std::size_t>(i)]->setStyleClass("dummy-input");
        }
        movel_table->elementAt(6, 0)->setStyleClass("form-label");
        movel_table->elementAt(6, 0)->addWidget(std::make_unique<Wt::WText>("Speed [mm/s]"));
        movel_speed_input_ = movel_table->elementAt(6, 1)->addWidget(std::make_unique<Wt::WLineEdit>("50.0"));
        movel_speed_input_->setStyleClass("dummy-input");
        auto movel_actions = movel_table->elementAt(7, 1)->addWidget(std::make_unique<Wt::WContainerWidget>());
        movel_actions->setStyleClass("dummy-inline");
        auto fill_pose = movel_actions->addWidget(std::make_unique<Wt::WPushButton>("Use Current"));
        fill_pose->setStyleClass("dummy-fill-btn");
        auto send_movel = movel_actions->addWidget(std::make_unique<Wt::WPushButton>("MoveL"));
        send_movel->setStyleClass("dummy-home-btn");
        fill_pose->clicked().connect([this] { fillMoveLFromCurrent(); });
        send_movel->clicked().connect([this] { startMoveLFromInputs(); });

        auto footer = root()->addWidget(std::make_unique<Wt::WContainerWidget>());
        footer->setStyleClass("dummy-panel");
        auto footer_actions = footer->addWidget(std::make_unique<Wt::WContainerWidget>());
        footer_actions->setStyleClass("dummy-inline");
        auto cancel_button = footer_actions->addWidget(std::make_unique<Wt::WPushButton>("Cancel Active Move"));
        cancel_button->setStyleClass("dummy-danger-btn");
        cancel_button->clicked().connect([this] { cancelMove(); });

        refresh_timer_ = std::make_unique<Wt::WTimer>();
        refresh_timer_->setInterval(std::chrono::milliseconds(250));
        refresh_timer_->timeout().connect([this] { refreshSnapshot(); });
        refresh_timer_->start();
    }

    void DummyRobotWebApp::refreshSnapshot()
    {
        if (!module_)
        {
            status_text_->setText("Status: module unavailable");
            return;
        }

        const RobotModuleDummy::UiSnapshot snapshot = module_->getUiSnapshot();
        if (!snapshot.valid)
        {
            status_text_->setText("Status: unavailable");
            return;
        }

        std::string status = snapshot.moving ? "MOVING" : "IDLE";
        if (snapshot.has_error)
        {
            status += " | last error: " + snapshot.error_text;
        }
        if (snapshot.active_action_id != 0)
        {
            status += " | action " + std::to_string(snapshot.active_action_id);
        }
        status_text_->setText("Status: " + status);

        for (std::size_t i = 0; i < 6; ++i)
        {
            current_joint_text_[i]->setText(formatFixed(snapshot.joints_rad[i] * kRadToDeg, 2));

            const double tfc_value = (i < 3) ? snapshot.tfc_xyzrpy[i] * kMToMm : snapshot.tfc_xyzrpy[i] * kRadToDeg;
            current_tfc_text_[i]->setText(formatFixed(tfc_value, 2));

            const double tcp_value = (i < 3) ? snapshot.tcp_xyzrpy[i] * kMToMm : snapshot.tcp_xyzrpy[i] * kRadToDeg;
            current_tcp_text_[i]->setText(formatFixed(tcp_value, 2));
        }

        if (!cartesian_reference_valid_)
        {
            syncCartesianReferenceFromSnapshot();
        }
    }

    void DummyRobotWebApp::fillMoveJFromCurrent()
    {
        const auto snapshot = module_->getUiSnapshot();
        if (!snapshot.valid) return;
        for (std::size_t i = 0; i < 6; ++i)
        {
            movej_inputs_[i]->setText(formatFixed(snapshot.joints_rad[i] * kRadToDeg, 2));
        }
    }

    void DummyRobotWebApp::fillMoveLFromCurrent()
    {
        syncCartesianReferenceFromSnapshot();
        writeCartesianReferenceToInputs();
    }

    void DummyRobotWebApp::startMoveJFromInputs()
    {
        std::array<double, 6> joints_rad{};
        for (std::size_t i = 0; i < 6; ++i)
        {
            joints_rad[i] = parseLineEditOrDefault(movej_inputs_[i], 0.0) * kDegToRad;
        }

        const double speed_rad_s = parseLineEditOrDefault(movej_speed_input_, 30.0) * kDegToRad;
        const auto result = module_->startUiMoveJoint(joints_rad, speed_rad_s);
        setMessage(result.message, !result.success);
        refreshSnapshot();
    }

    void DummyRobotWebApp::startMoveLFromInputs()
    {
        std::array<double, 6> tcp_xyzrpy{};
        for (std::size_t i = 0; i < 6; ++i)
        {
            const double raw = parseLineEditOrDefault(movel_inputs_[i], 0.0);
            tcp_xyzrpy[i] = (i < 3) ? raw * kMmToM : raw * kDegToRad;
        }

        const double speed_m_s = parseLineEditOrDefault(movel_speed_input_, 50.0) * kMmToM;
        const auto result = module_->startUiMoveLinear(tcp_xyzrpy, speed_m_s);
        if (result.success)
        {
            cartesian_reference_xyzrpy_ = tcp_xyzrpy;
            cartesian_reference_valid_ = true;
        }
        setMessage(result.message, !result.success);
        refreshSnapshot();
    }

    void DummyRobotWebApp::jogJoint(std::size_t joint_index, double direction)
    {
        const auto snapshot = module_->getUiSnapshot();
        if (!snapshot.valid)
        {
            setMessage("Robot snapshot unavailable.", true);
            return;
        }

        std::array<double, 6> target = snapshot.joints_rad;
        const double step_rad = parseLineEditOrDefault(joint_jog_step_input_, 5.0) * kDegToRad;
        const double speed_rad_s = parseLineEditOrDefault(joint_jog_speed_input_, 30.0) * kDegToRad;
        target[joint_index] += direction * step_rad;

        const auto result = module_->startUiMoveJoint(target, speed_rad_s);
        setMessage(result.message, !result.success);
        refreshSnapshot();
    }

    void DummyRobotWebApp::jogCartesian(std::size_t axis_index, double direction)
    {
        const auto snapshot = module_->getUiSnapshot();
        if (!snapshot.valid)
        {
            setMessage("Robot snapshot unavailable.", true);
            return;
        }

        std::array<double, 6> target = snapshot.tcp_xyzrpy;
        if (axis_index < 3)
        {
            target[axis_index] += direction * parseLineEditOrDefault(cartesian_pos_step_input_, 10.0) * kMmToM;
        }
        else
        {
            target[axis_index] += direction * parseLineEditOrDefault(cartesian_rot_step_input_, 5.0) * kDegToRad;
        }

        const double speed_m_s = parseLineEditOrDefault(cartesian_speed_input_, 50.0) * kMmToM;
        const auto result = module_->startUiMoveLinear(target, speed_m_s);
        setMessage(result.message, !result.success);
        refreshSnapshot();
    }

    void DummyRobotWebApp::cancelMove()
    {
        const auto result = module_->cancelUiMove();
        setMessage(result.message, !result.success);
        refreshSnapshot();
    }

    void DummyRobotWebApp::setMessage(const std::string& text, bool is_error)
    {
        if (!message_text_) return;
        const std::string prefix = is_error ? "Error: " : "Info: ";
        message_text_->setText(prefix + text);
        message_text_->setStyleClass(is_error ? "dummy-message error" : "dummy-message");
    }

    std::string DummyRobotWebApp::formatFixed(double value, int precision)
    {
        std::ostringstream oss;
        oss.setf(std::ios::fixed, std::ios::floatfield);
        oss.precision(precision);
        oss << value;
        return oss.str();
    }

    double DummyRobotWebApp::parseLineEditOrDefault(Wt::WLineEdit* edit, double fallback)
    {
        if (!edit)
        {
            return fallback;
        }

        try
        {
            return std::stod(edit->text().toUTF8());
        }
        catch (...)
        {
            return fallback;
        }
    }

    void DummyRobotWebApp::jogJointFixed(std::size_t joint_index, double step_deg)
    {
        const auto snapshot = module_->getUiSnapshot();
        if (!snapshot.valid)
        {
            setMessage("Robot snapshot unavailable.", true);
            return;
        }

        std::array<double, 6> target = snapshot.joints_rad;
        const double speed_rad_s = parseLineEditOrDefault(joint_jog_speed_input_, 30.0) * kDegToRad;
        target[joint_index] += step_deg * kDegToRad;
        const auto result = module_->startUiMoveJoint(target, speed_rad_s);
        setMessage(result.message, !result.success);
        refreshSnapshot();
    }

    void DummyRobotWebApp::jogCartesianFixed(std::size_t axis_index, double step_ui_units)
    {
        if (!cartesian_reference_valid_)
        {
            syncCartesianReferenceFromSnapshot();
        }
        if (!cartesian_reference_valid_)
        {
            setMessage("Robot snapshot unavailable.", true);
            return;
        }

        std::array<double, 6> target = cartesian_reference_xyzrpy_;
        if (axis_index < 3)
        {
            target[axis_index] += step_ui_units * kMmToM;
        }
        else
        {
            target[axis_index] += step_ui_units * kDegToRad;
        }

        const double speed_m_s = parseLineEditOrDefault(cartesian_speed_input_, 50.0) * kMmToM;
        const auto result = module_->startUiMoveLinear(target, speed_m_s);
        if (result.success)
        {
            cartesian_reference_xyzrpy_ = target;
            cartesian_reference_valid_ = true;
            writeCartesianReferenceToInputs();
        }
        setMessage(result.message, !result.success);
        refreshSnapshot();
    }

    void DummyRobotWebApp::syncCartesianReferenceFromSnapshot()
    {
        if (!module_)
        {
            cartesian_reference_valid_ = false;
            return;
        }

        const auto snapshot = module_->getUiSnapshot();
        if (!snapshot.valid)
        {
            cartesian_reference_valid_ = false;
            return;
        }

        cartesian_reference_xyzrpy_ = snapshot.tcp_xyzrpy;
        cartesian_reference_valid_ = true;
    }

    void DummyRobotWebApp::writeCartesianReferenceToInputs() const
    {
        if (!cartesian_reference_valid_)
        {
            return;
        }

        for (std::size_t i = 0; i < 6; ++i)
        {
            const double value = (i < 3) ? cartesian_reference_xyzrpy_[i] * kMToMm : cartesian_reference_xyzrpy_[i] * kRadToDeg;
            movel_inputs_[i]->setText(formatFixed(value, 2));
        }
    }
}
