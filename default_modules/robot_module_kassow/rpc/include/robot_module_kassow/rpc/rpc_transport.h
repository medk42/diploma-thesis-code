#pragma once

#include "module_helpers/robot_interface/message_types.h"
#include "module_common/module_interface_.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace aergo::robot::kassow::rpc
{
    using ILogger = aergo::module::logging::ILogger;
    using LogType = aergo::module::logging::LogType;
    using Request = aergo::module::helpers::robot_interface::Request;
    using Response = aergo::module::helpers::robot_interface::Response;
    using StatusMessage = aergo::module::helpers::robot_interface::StatusMessage;
    using FinishedMessage = aergo::module::helpers::robot_interface::FinishedMessage;

    enum class RpcMessageKind : uint8_t
    {
        REQUEST = 0,
        RESPONSE = 1,
        STATUS = 2,
        FINISHED = 3
    };

    struct RpcFrameHeader
    {
        RpcMessageKind kind{};
        uint32_t request_id{};
        uint32_t payload_size{};
        uint32_t blob_size{};
    };

    class TcpSocket
    {
    public:
        TcpSocket();
        ~TcpSocket();

        static bool initializeSockets(const ILogger* logger);

        TcpSocket(const TcpSocket&) = delete;
        TcpSocket& operator=(const TcpSocket&) = delete;

        TcpSocket(TcpSocket&& other) noexcept;
        TcpSocket& operator=(TcpSocket&& other) noexcept;

        bool connect(const std::string& host, uint16_t port, const ILogger* logger);
        bool adopt(int socket_fd, const ILogger* logger);
        void close();

        bool isConnected() const noexcept { return socket_fd_.has_value(); }

        bool sendAll(const uint8_t* data, size_t size, const ILogger* logger);
        bool recvAll(uint8_t* data, size_t size, std::chrono::milliseconds timeout, const ILogger* logger);
        bool waitForData(std::chrono::milliseconds timeout, const ILogger* logger);

        int nativeHandle() const noexcept;

    private:
        bool ensureInitialized(const ILogger* logger);
        void logError(const ILogger* logger, std::string_view context) const;

        std::optional<int> socket_fd_;
    };

    class RpcClient
    {
    public:
        using StatusCallback = std::function<void(const StatusMessage&, std::span<const std::byte>)>;
        using FinishedCallback = std::function<void(const FinishedMessage&, std::span<const std::byte>)>;

        explicit RpcClient(const ILogger* logger);
        ~RpcClient();

        bool connect(const std::string& host, uint16_t port);
        void disconnect();
        bool isConnected() const noexcept;

        void setStatusCallback(StatusCallback cb);
        void setFinishedCallback(FinishedCallback cb);

        bool sendRequest(const Request& request,
                         std::span<const std::byte> request_blob,
                         Response& out_response,
                         std::vector<std::byte>& out_response_blob,
                         std::chrono::milliseconds timeout);

        bool pollOnce(std::chrono::milliseconds timeout);

    private:
        bool readFrame(RpcFrameHeader& header, std::vector<std::byte>& payload, std::vector<std::byte>& blob, std::chrono::milliseconds timeout);
        bool writeFrame(const RpcFrameHeader& header, std::span<const std::byte> payload, std::span<const std::byte> blob);
        void dispatchAsync(const RpcFrameHeader& header, const std::vector<std::byte>& payload, const std::vector<std::byte>& blob);
        void log(LogType type, std::string_view msg) const;

        const ILogger* logger_;
        TcpSocket socket_;
        std::mutex socket_mutex_;
        uint32_t next_request_id_;
        std::optional<uint32_t> pending_request_id_;
        StatusCallback status_cb_;
        FinishedCallback finished_cb_;
    };

    class RpcServer
    {
    public:
        struct IncomingRequest
        {
            uint32_t request_id{};
            Request request{};
            std::vector<std::byte> blob;
        };

        explicit RpcServer(const ILogger* logger);
        ~RpcServer();

        bool start(uint16_t port);
        void stop();
        bool pollOnce(std::function<void(const IncomingRequest&)> request_handler, std::chrono::milliseconds timeout);

        bool sendResponse(uint32_t request_id, const Response& response, std::span<const std::byte> blob);
        bool sendStatusMessage(const StatusMessage& status, std::span<const std::byte> blob);
        bool sendFinishedMessage(const FinishedMessage& finished, std::span<const std::byte> blob);

    private:
        bool ensureClient();
        void log(LogType type, std::string_view msg) const;

        const ILogger* logger_;
        int listen_fd_;
        TcpSocket client_;
    };
}
