#pragma once

#include "module_helpers/synchronous_request_helper/synchronous_request_helper.h"
#include "module_helpers/mixed_buffer_allocator/mixed_buffered_allocator.h"
#include "module_helpers/robot_interface/features/robot_control/messages.h"

#include "module_common/base_module.h"

#include <expected>
#include <functional>
#include <map>
#include <set>
#include <optional>
#include <string_view>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>

namespace aergo::module::helpers::robot_interface::robot_control
{
    namespace sync_req = aergo::module::helpers::synchronous_request_helper;
    namespace ri = aergo::module::helpers::robot_interface;
    namespace rc = ri::robot_control;

    /// @brief Result of a move request issued through the robot control interface.
    /// success_ indicates whether the request succeeded, err_message_ contains an optional error description, and action_id_ is set for non-blocking requests.
    struct MoveRequestResult
    {
        bool success_;            // true on success, false on failure
        std::string err_message_; // empty if success_ is true
        uint64_t action_id_;      // for async actions on success, 0 otherwise
    };

    using StatusMessage = rc::status_messages::deserialization::StatusMessage;

    class RobotWrapper
    {
    public:
        /// @brief Construct a wrapper around the robot control feature using the provided base module.
        /// Initializes request/subscribe channels and allocators; check valid() before issuing commands.
        /// @param base_module Reference to the base module to use for channel access and logging.
        /// @param sync_request_timeout_ms Timeout in milliseconds for robot requests. This module uses synchronous requests for robot commands (waits for response). sync_request_timeout_ms parameter sets the timeout for these requests.
        RobotWrapper(
            aergo::module::BaseModule &base_module,
            uint32_t sync_request_timeout_ms = 100
        );

        /// @brief Set optional callback for asynchronous robot status updates. Parameter is the status message.
        void setStatusCallback(std::function<void(const StatusMessage&)> rc_status_callback) { rc_status_callback_ = rc_status_callback; }

        /// @brief Set optional callback for asynchronous robot action finished notifications. Parameters are action ID, success flag, and optional error message (if success is false).
        void setFinishedCallback(std::function<void(uint64_t, bool, std::string_view)> rc_finished_callback) { rc_finished_callback_ = rc_finished_callback; }

        /// @brief True if the wrapper initialized its channels and allocators successfully.
        bool valid() const noexcept { return valid_; }

        /// @brief Check if this wrapper handles an ingress message for synchronous robot responses.
        /// Forward the ingress to onIngress when this returns true; wraps the behavior of SynchronousRequestHelper.
        /// @param kind Processing type passed to the module ingress callback.
        /// @param local_channel_id Local channel that received the message.
        /// @param src Source channel identifier (module and channel ID).
        /// @return true if the message belongs to the robot wrapper's synchronous request flow.
        bool handlesIngress(aergo::module::IModule::ProcessingType kind, uint32_t local_channel_id, aergo::module::ChannelIdentifier src) const noexcept;;

        /// @brief Handle ingress messages for synchronous robot responses when handlesIngress returned true.
        /// Delegates to SynchronousRequestHelper to drop queue-full responses and keep pending requests consistent.
        /// @param kind Processing type passed to the module ingress callback.
        /// @param msg Message header to inspect.
        /// @param queue_status Current queue status supplied by the module.
        /// @return Ingress decision from the synchronous request helper.
        aergo::module::IModule::IngressDecision onIngress(aergo::module::IModule::ProcessingType kind, const aergo::module::message::MessageHeader& msg, aergo::module::IModule::QueueStatus queue_status) noexcept;

        /// @brief Check if this wrapper should consume a response message for a pending request.
        /// @param request_consumer_id Local consumer ID for the response channel.
        /// @param src Source channel identifier (module and channel ID).
        /// @return true if the response matches the pending synchronous request.
        bool handlesResponse(uint32_t request_consumer_id, aergo::module::ChannelIdentifier src) const noexcept;

        /// @brief Process a response message for a pending synchronous robot request.
        /// Pass the raw header from processResponse into the synchronous request helper.
        /// @param message Response message header.
        void processResponse(aergo::module::message::MessageHeader message) noexcept;

        /// @brief Process subscribed robot status updates and finished notifications.
        /// Call when handling subscribe callbacks from the robot status or finished channels.
        /// @param subscribe_consumer_id Local consumer ID for the subscribe channel.
        /// @param message Message header containing the status or finished payload.
        void processMessage(uint32_t subscribe_consumer_id, aergo::module::message::MessageHeader message) noexcept;

        /// @brief Move robot's joints to the specified target positions. Maximum supported joint count is 16.
        /// If blocking is false, returns immediately after starting the movement with an action ID. Success indicates if the movement was started successfully.
        /// If blocking is true, waits for the movement to finish and returns success only if the movement completed successfully.
        /// @param joint_targets The target joint positions to move to, in radians.
        /// @param speed The speed to move at, in rad/s.
        /// @param acceleration The acceleration to use, in rad/s^2.
        /// @param blocking If true, the request will block until the movement is finished. If false, the request will return immediately after starting the movement.
        /// Blocking requests will return success only if the movement was completed successfully. Non-blocking requests will return success if the movement was started successfully.
        /// Non-blocking requests will return an action ID that can be used to query the status of the movement later or to cancel it.
        /// @return MoveRequestResult containing success flag, error message (when success == false) and action ID (when success == true and blocking == false).
        MoveRequestResult moveJoint(Span<const double> joint_targets, double speed, double acceleration, bool blocking = true);

        /// @brief Move the robot from its current pose to a target pose in a straight line.
        /// Poses are specified in world coordinates and represent the end-effector pose.
        /// If blocking is false, returns immediately after starting the movement with an action ID. Success indicates if the movement was started successfully.
        /// If blocking is true, waits for the movement to finish and returns success only if the movement completed successfully.
        /// @param pose_target Target pose to move to.
        /// @param speed The speed to move at, in m/s.
        /// @param acceleration The acceleration to use, in m/s^2.
        /// @param blocking If true, waits for the movement to finish; if false, returns immediately with an action ID when started successfully.
        /// @return MoveRequestResult containing success flag, error message (when success == false) and action ID (when success == true and blocking == false).
        MoveRequestResult moveLinear(rc::Pose pose_target, double speed, double acceleration, bool blocking = true);

        /// @brief Move along an arc passing through a control point and ending at a target pose.
        /// Poses are specified in world coordinates and represent the end-effector pose.
        /// If blocking is false, returns immediately after starting the movement with an action ID. Success indicates if the movement was started successfully.
        /// If blocking is true, waits for the movement to finish and returns success only if the movement completed successfully.
        /// @param pose_through Control point for the arc.
        /// @param pose_target Target pose to finish at.
        /// @param speed The speed to move at, in m/s.
        /// @param acceleration The acceleration to use, in m/s^2.
        /// @param orientation_type Orientation behavior to apply along the path.
        /// @param as_circle If true, treat the path as a percentage of a full circle instead of an arc segment.
        /// @param circle_percentage Percentage of the full circle to traverse when as_circle is true (1.0 = full circle, can exceed 1.0).
        /// @param blocking If true, waits for the movement to finish; if false, returns immediately with an action ID when started successfully.
        /// @return MoveRequestResult containing success flag, error message (when success == false) and action ID (when success == true and blocking == false).
        MoveRequestResult moveArc(rc::Pose pose_through, rc::Pose pose_target, double speed, double acceleration, rc::OrientationType orientation_type, bool as_circle, double circle_percentage, bool blocking = true);

        /// @brief Move along a trajectory defined by multiple target poses, stopping at the last pose.
        /// Robot fits a smooth path between intermediate poses instead of stopping at each one.
        /// If blocking is false, returns immediately after starting the movement with an action ID. Success indicates if the movement was started successfully.
        /// If blocking is true, waits for the movement to finish and returns success only if the movement completed successfully.
        /// @param pose_targets Target poses to traverse in order; maximum supported count is 32768.
        /// @param speed The speed to move at, in m/s.
        /// @param acceleration The acceleration to use, in m/s^2.
        /// @param orientation_type Orientation behavior to apply along the path.
        /// @param blocking If true, waits for the movement to finish; if false, returns immediately with an action ID when started successfully.
        /// @return MoveRequestResult containing success flag, error message (when success == false) and action ID (when success == true and blocking == false).
        MoveRequestResult moveTrajectory(Span<const rc::Pose> pose_targets, double speed, double acceleration, OrientationType orientation_type, bool blocking = true);

        /// @brief Cancel an ongoing asynchronous action with the given ID.
        /// If the action is known, active and was created asynchronously, sends a cancel request to the robot.
        /// On success, returns a MoveRequestResult with success == true.
        /// If robot reports failure to cancel, returns success == false with error message.
        /// If the action is unknown or already finished, returns success == true (no-op).
        /// @param action_id ID of the action to cancel.
        /// @return MoveRequestResult indicating success or failure with error message.
        MoveRequestResult cancelAction(uint64_t action_id);

        /// @brief Fetch the robot's specifications (velocity/acceleration limits, joint limits, joint count).
        /// On success returns RobotSpecs; on failure returns an error message in the unexpected state.
        /// @return Expected containing robot specs on success or error string on failure.
        std::expected<rc::RobotSpecs, std::string> getRobotSpecs();

        /// @brief Check if an asynchronous action with the given ID is still active (in progress).
        /// Returns true if the action was asynchronously started by this wrapper and is still ongoing (the robot 
        /// has not reported it as finished via the finished channel). Returns false for unknown action IDs or 
        /// actions that already finished.
        /// @param action_id ID of the action to check.
        /// @return true if the action is active, false otherwise (unknown or finished).
        bool isActionActive(uint64_t action_id) const noexcept;

        /// @brief Get the latest received robot status message.
        /// Returns true and fills the status parameter if a valid status message was received at least once; returns false if no valid status message has been received yet.
        /// The status message is updated whenever a new valid status message is received on the robot status channel, and can be accessed at any time with this method. Note that the status message may be slightly outdated depending on the frequency of status updates from the robot.
        /// @param status Output parameter to fill with the latest status message if available.
        bool getLastStatus(StatusMessage& status) const noexcept;

    private:
        MoveRequestResult moveCommon(std::span<const std::byte> request_data, bool blocking, std::unique_lock<std::mutex>& lock);

        int64_t millis() const noexcept
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count();
        }

        enum class RequestType { ROBOT_REQUEST };

        aergo::module::BaseModule &base_module_;
        uint32_t sync_request_timeout_ms_;

        std::function<void(StatusMessage)> rc_status_callback_{ nullptr };
        std::function<void(uint64_t, bool, std::string_view)> rc_finished_callback_{ nullptr };

        bool valid_{ false };        
        std::unique_ptr<aergo::module::helpers::mixed_buffer_allocator::MixedBufferedAllocator> mixed_allocator_;
        std::unique_ptr<sync_req::SynchronousRequestHelper<RequestType>> sync_request_helper_;

        uint32_t robot_request_channel_;
        uint32_t robot_status_channel_;
        uint32_t robot_finished_channel_;

        mutable std::mutex mutex_;
        std::condition_variable cv_;
        std::map<uint64_t, std::optional<MoveRequestResult>> sync_action_ids_;
        std::set<uint64_t> async_action_ids_;
        std::vector<std::byte> request_data_buffer_;

        std::atomic<int64_t> last_move_status_ms_{ 0 };
        std::atomic<int64_t> last_send_move_request_ms_{ 0 };

        mutable std::mutex last_status_mutex_;
        bool last_status_valid_{ false };
        StatusMessage last_status_message_; // buffer for the last received status message, updated in processMessage and read in getLastStatus; unlike status_message_buffer_, this one should always be valid (status_message_buffer_ may be in an invalid state after failed deserialization, but last_status_message_ should only be updated on successful deserialization).
        StatusMessage status_message_buffer_; // buffer for deserializing status messages to prevent repeated allocations
    };
}
