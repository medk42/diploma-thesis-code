#include "robot_module_kassow/rpc/rpc_transport.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <sstream>
#include <system_error>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
using socket_len_t = int;
using socket_send_recv_t = int;
static constexpr int INVALID_SOCKET_FD = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
using socket_len_t = socklen_t;
using socket_send_recv_t = ssize_t;
static constexpr int INVALID_SOCKET_FD = -1;
#endif

namespace aergo::robot::kassow::rpc
{
    namespace
    {
        constexpr size_t FRAME_HEADER_SIZE = 1 + 3 * sizeof(uint32_t); // kind + request_id + payload_size + blob_size
        constexpr uint32_t MAX_FRAME_SIZE = 64 * 1024 * 1024; // 64 MiB guard rail

        uint32_t toNetwork(uint32_t value)
        {
#ifdef _WIN32
            return _byteswap_ulong(value);
#else
            return htonl(value);
#endif
        }

        uint32_t fromNetwork(uint32_t value)
        {
#ifdef _WIN32
            return _byteswap_ulong(value);
#else
            return ntohl(value);
#endif
        }

        template <typename FrameHeaderT>
        std::array<uint8_t, FRAME_HEADER_SIZE> serializeHeader(const FrameHeaderT& header)
        {
            std::array<uint8_t, FRAME_HEADER_SIZE> data{};
            data[0] = static_cast<uint8_t>(header.kind);
            uint32_t net_request = toNetwork(header.request_id);
            uint32_t net_payload = toNetwork(header.payload_size);
            uint32_t net_blob = toNetwork(header.blob_size);
            std::memcpy(data.data() + 1, &net_request, sizeof(uint32_t));
            std::memcpy(data.data() + 1 + sizeof(uint32_t), &net_payload, sizeof(uint32_t));
            std::memcpy(data.data() + 1 + 2 * sizeof(uint32_t), &net_blob, sizeof(uint32_t));
            return data;
        }

        template <typename FrameHeaderT>
        bool deserializeHeader(const uint8_t* data, FrameHeaderT& out)
        {
            out.kind = static_cast<RpcMessageKind>(data[0]);
            uint32_t net_request{};
            uint32_t net_payload{};
            uint32_t net_blob{};
            std::memcpy(&net_request, data + 1, sizeof(uint32_t));
            std::memcpy(&net_payload, data + 1 + sizeof(uint32_t), sizeof(uint32_t));
            std::memcpy(&net_blob, data + 1 + 2 * sizeof(uint32_t), sizeof(uint32_t));
            out.request_id = fromNetwork(net_request);
            out.payload_size = fromNetwork(net_payload);
            out.blob_size = fromNetwork(net_blob);
            return true;
        }

        std::string lastErrorString()
        {
#ifdef _WIN32
            int err = WSAGetLastError();
            return std::system_category().message(err);
#else
            return std::system_category().message(errno);
#endif
        }
    } // namespace


    TcpSocket::TcpSocket() = default;
    TcpSocket::~TcpSocket()
    {
        close();
    }

    TcpSocket::TcpSocket(TcpSocket&& other) noexcept
    {
        socket_fd_ = other.socket_fd_;
        other.socket_fd_.reset();
    }

    TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept
    {
        if (this != &other)
        {
            close();
            socket_fd_ = other.socket_fd_;
            other.socket_fd_.reset();
        }
        return *this;
    }

    bool TcpSocket::initializeSockets(const ILogger* logger)
    {
        TcpSocket tmp;
        return tmp.ensureInitialized(logger);
    }

    bool TcpSocket::ensureInitialized(const ILogger* logger)
    {
#ifdef _WIN32
        static bool initialized = false;
        if (initialized)
        {
            return true;
        }
        WSADATA wsaData{};
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0)
        {
            if (logger)
            {
                auto msg = "WSAStartup failed: " + std::to_string(result);
                logger->log(LogType::ERROR, msg.c_str());
            }
            return false;
        }
        initialized = true;
#endif
        (void)logger;
        return true;
    }

    void TcpSocket::logError(const ILogger* logger, std::string_view context) const
    {
        if (!logger)
        {
            return;
        }
        std::ostringstream oss;
        oss << context << ": " << lastErrorString();
        logger->log(LogType::ERROR, oss.str().c_str());
    }

    bool TcpSocket::connect(const std::string& host, uint16_t port, const ILogger* logger)
    {
        close();
        if (!ensureInitialized(logger))
        {
            return false;
        }

        struct addrinfo hints {};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        struct addrinfo* result = nullptr;
        std::string port_str = std::to_string(port);
        int res = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result);
        if (res != 0)
        {
            if (logger)
            {
                logger->log(LogType::ERROR, ("getaddrinfo failed: " + std::string(gai_strerror(res))).c_str());
            }
            return false;
        }

        bool connected = false;
        for (struct addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next)
        {
            int fd = ::socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
            if (fd == INVALID_SOCKET_FD)
            {
                continue;
            }

            if (::connect(fd, ptr->ai_addr, static_cast<socket_len_t>(ptr->ai_addrlen)) == 0)
            {
                socket_fd_ = fd;
                connected = true;
                break;
            }
#ifdef _WIN32
            closesocket(fd);
#else
            ::close(fd);
#endif
        }

        freeaddrinfo(result);

        if (!connected && logger)
        {
            logError(logger, "connect failed");
        }

        return connected;
    }

    bool TcpSocket::adopt(int socket_fd, const ILogger* logger)
    {
        close();
        if (!ensureInitialized(logger))
        {
            return false;
        }
        socket_fd_ = socket_fd;
        return true;
    }

    void TcpSocket::close()
    {
        if (socket_fd_.has_value())
        {
#ifdef _WIN32
            closesocket(*socket_fd_);
#else
            ::close(*socket_fd_);
#endif
            socket_fd_.reset();
        }
    }

    bool TcpSocket::waitForData(std::chrono::milliseconds timeout, const ILogger* logger)
    {
        if (!socket_fd_.has_value())
        {
            return false;
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(*socket_fd_, &readfds);

        struct timeval tv {};
        tv.tv_sec = static_cast<long>(timeout.count() / 1000);
        tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);

        int ret = select(*socket_fd_ + 1, &readfds, nullptr, nullptr, &tv);
        if (ret > 0 && FD_ISSET(*socket_fd_, &readfds))
        {
            return true;
        }
        if (ret < 0)
        {
            logError(logger, "select failed");
            close();
        }
        return false;
    }

    bool TcpSocket::sendAll(const uint8_t* data, size_t size, const ILogger* logger)
    {
        if (!socket_fd_.has_value())
        {
            return false;
        }

        size_t total_sent = 0;
        while (total_sent < size)
        {
            socket_send_recv_t sent = ::send(*socket_fd_, reinterpret_cast<const char*>(data + total_sent), static_cast<socket_send_recv_t>(size - total_sent), 0);
            if (sent <= 0)
            {
                logError(logger, "send failed");
                close();
                return false;
            }
            total_sent += static_cast<size_t>(sent);
        }
        return true;
    }

    bool TcpSocket::recvAll(uint8_t* data, size_t size, std::chrono::milliseconds timeout, const ILogger* logger)
    {
        if (!socket_fd_.has_value())
        {
            return false;
        }

        size_t total_recv = 0;
        auto deadline = std::chrono::steady_clock::now() + timeout;
        while (total_recv < size)
        {
            auto now = std::chrono::steady_clock::now();
            if (now >= deadline)
            {
                return false;
            }

            auto remaining_ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
            if (!waitForData(remaining_ms, logger))
            {
                return false;
            }

            socket_send_recv_t recvd = ::recv(*socket_fd_, reinterpret_cast<char*>(data + total_recv), static_cast<socket_send_recv_t>(size - total_recv), 0);
            if (recvd <= 0)
            {
                logError(logger, "recv failed");
                close();
                return false;
            }
            total_recv += static_cast<size_t>(recvd);
        }

        return true;
    }

    int TcpSocket::nativeHandle() const noexcept
    {
        return socket_fd_.value_or(INVALID_SOCKET_FD);
    }


    RpcClient::RpcClient(const ILogger* logger)
        : logger_(logger), next_request_id_(1)
    {}

    RpcClient::~RpcClient()
    {
        disconnect();
    }

    void RpcClient::log(LogType type, std::string_view msg) const
    {
        if (logger_)
        {
            logger_->log(type, std::string(msg).c_str());
        }
    }

    bool RpcClient::connect(const std::string& host, uint16_t port)
    {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        pending_request_id_.reset();
        return socket_.connect(host, port, logger_);
    }

    void RpcClient::disconnect()
    {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        pending_request_id_.reset();
        socket_.close();
    }

    bool RpcClient::isConnected() const noexcept
    {
        return socket_.isConnected();
    }

    void RpcClient::setStatusCallback(StatusCallback cb)
    {
        status_cb_ = std::move(cb);
    }

    void RpcClient::setFinishedCallback(FinishedCallback cb)
    {
        finished_cb_ = std::move(cb);
    }

    bool RpcClient::writeFrame(const RpcFrameHeader& header, std::span<const std::byte> payload, std::span<const std::byte> blob)
    {
        auto header_buf = serializeHeader(header);
        if (!socket_.sendAll(header_buf.data(), header_buf.size(), logger_))
        {
            return false;
        }
        if (!payload.empty() && !socket_.sendAll(reinterpret_cast<const uint8_t*>(payload.data()), payload.size(), logger_))
        {
            return false;
        }
        if (!blob.empty() && !socket_.sendAll(reinterpret_cast<const uint8_t*>(blob.data()), blob.size(), logger_))
        {
            return false;
        }
        return true;
    }

    bool RpcClient::readFrame(RpcFrameHeader& header, std::vector<std::byte>& payload, std::vector<std::byte>& blob, std::chrono::milliseconds timeout)
    {
        std::array<uint8_t, FRAME_HEADER_SIZE> header_raw{};
        if (!socket_.recvAll(header_raw.data(), header_raw.size(), timeout, logger_))
        {
            return false;
        }

        if (!deserializeHeader(header_raw.data(), header))
        {
            log(LogType::ERROR, "Failed to deserialize RPC frame header");
            return false;
        }

        if (header.payload_size > MAX_FRAME_SIZE || header.blob_size > MAX_FRAME_SIZE)
        {
            log(LogType::ERROR, "RPC frame exceeds maximum size");
            return false;
        }

        payload.resize(header.payload_size);
        blob.resize(header.blob_size);

        if (header.payload_size > 0)
        {
            if (!socket_.recvAll(reinterpret_cast<uint8_t*>(payload.data()), header.payload_size, timeout, logger_))
            {
                return false;
            }
        }
        if (header.blob_size > 0)
        {
            if (!socket_.recvAll(reinterpret_cast<uint8_t*>(blob.data()), header.blob_size, timeout, logger_))
            {
                return false;
            }
        }

        return true;
    }

    void RpcClient::dispatchAsync(const RpcFrameHeader& header, const std::vector<std::byte>& payload, const std::vector<std::byte>& blob)
    {
        switch (header.kind)
        {
            case RpcMessageKind::STATUS:
            {
                if (payload.size() == sizeof(StatusMessage) && status_cb_)
                {
                    StatusMessage status{};
                    std::memcpy(&status, payload.data(), sizeof(StatusMessage));
                    status_cb_(status, blob);
                }
                else
                {
                    log(LogType::WARNING, "Received status message with invalid payload");
                }
                break;
            }
            case RpcMessageKind::FINISHED:
            {
                if (payload.size() == sizeof(FinishedMessage) && finished_cb_)
                {
                    FinishedMessage finished{};
                    std::memcpy(&finished, payload.data(), sizeof(FinishedMessage));
                    finished_cb_(finished, blob);
                }
                else
                {
                    log(LogType::WARNING, "Received finished message with invalid payload");
                }
                break;
            }
            default:
                log(LogType::WARNING, "Received unexpected message while dispatching async data");
                break;
        }
    }

    bool RpcClient::sendRequest(const Request& request,
                                std::span<const std::byte> request_blob,
                                Response& out_response,
                                std::vector<std::byte>& out_response_blob,
                                std::chrono::milliseconds timeout)
    {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        if (!socket_.isConnected())
        {
            log(LogType::ERROR, "RPC client is not connected");
            return false;
        }

        pending_request_id_ = next_request_id_++;

        RpcFrameHeader header{
            .kind = RpcMessageKind::REQUEST,
            .request_id = *pending_request_id_,
            .payload_size = static_cast<uint32_t>(sizeof(Request)),
            .blob_size = static_cast<uint32_t>(request_blob.size())
        };

        std::span<const std::byte> payload(reinterpret_cast<const std::byte*>(&request), sizeof(Request));
        if (!writeFrame(header, payload, request_blob))
        {
            pending_request_id_.reset();
            return false;
        }

        auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            RpcFrameHeader incoming{};
            std::vector<std::byte> payload_buf;
            std::vector<std::byte> blob_buf;
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            if (!readFrame(incoming, payload_buf, blob_buf, remaining))
            {
                break;
            }

            if (incoming.kind == RpcMessageKind::RESPONSE)
            {
                if (!pending_request_id_.has_value() || incoming.request_id != *pending_request_id_)
                {
                    std::ostringstream oss;
                    oss << "Received response with unexpected request id: " << incoming.request_id
                        << ", expected: " << (pending_request_id_.has_value() ? std::to_string(*pending_request_id_) : std::string("none"));
                    log(LogType::WARNING, oss.str());
                    continue;
                }

                if (payload_buf.size() != sizeof(Response))
                {
                    log(LogType::ERROR, "Invalid response payload size");
                    pending_request_id_.reset();
                    return false;
                }

                std::memcpy(&out_response, payload_buf.data(), sizeof(Response));
                out_response_blob = std::move(blob_buf);
                pending_request_id_.reset();
                return true;
            }

            // Async message while waiting for response
            dispatchAsync(incoming, payload_buf, blob_buf);
        }

        pending_request_id_.reset();
        return false;
    }

    bool RpcClient::pollOnce(std::chrono::milliseconds timeout)
    {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        if (!socket_.isConnected())
        {
            return false;
        }

        if (!socket_.waitForData(timeout, logger_))
        {
            return false; // timeout
        }

        RpcFrameHeader header{};
        std::vector<std::byte> payload;
        std::vector<std::byte> blob;
        if (!readFrame(header, payload, blob, timeout))
        {
            return false;
        }

        if (header.kind == RpcMessageKind::RESPONSE)
        {
            log(LogType::WARNING, "Received stray response without pending request, dropping");
            return false;
        }

        dispatchAsync(header, payload, blob);
        return true;
    }


    RpcServer::RpcServer(const ILogger* logger)
        : logger_(logger), listen_fd_(INVALID_SOCKET_FD)
    {}

    RpcServer::~RpcServer()
    {
        stop();
    }

    void RpcServer::log(LogType type, std::string_view msg) const
    {
        if (logger_)
        {
            logger_->log(type, std::string(msg).c_str());
        }
    }

    bool RpcServer::start(uint16_t port)
    {
        stop();

        if (!TcpSocket::initializeSockets(logger_))
        {
            return false;
        }

        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listen_fd_ == INVALID_SOCKET_FD)
        {
            log(LogType::ERROR, "Failed to create listen socket");
            return false;
        }

        int opt = 1;
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&opt), sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port);

        if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        {
            log(LogType::ERROR, "Failed to bind listen socket");
            stop();
            return false;
        }

        if (::listen(listen_fd_, 1) < 0)
        {
            log(LogType::ERROR, "Failed to listen on socket");
            stop();
            return false;
        }

        log(LogType::INFO, "RPC server listening");
        return true;
    }

    void RpcServer::stop()
    {
        client_.close();
        if (listen_fd_ != INVALID_SOCKET_FD)
        {
#ifdef _WIN32
            closesocket(listen_fd_);
#else
            ::close(listen_fd_);
#endif
            listen_fd_ = INVALID_SOCKET_FD;
        }
    }

    bool RpcServer::ensureClient()
    {
        if (client_.isConnected())
        {
            return true;
        }
        if (listen_fd_ == INVALID_SOCKET_FD)
        {
            log(LogType::ERROR, "RPC server is not listening");
            return false;
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(listen_fd_, &readfds);

        struct timeval tv {};
        tv.tv_sec = 0;
        tv.tv_usec = 0;

        int ret = select(listen_fd_ + 1, &readfds, nullptr, nullptr, &tv);
        if (ret > 0 && FD_ISSET(listen_fd_, &readfds))
        {
            sockaddr_storage addr{};
            socket_len_t len = sizeof(addr);
            int new_fd = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len);
            if (new_fd != INVALID_SOCKET_FD)
            {
                client_.adopt(new_fd, logger_);
                log(LogType::INFO, "Accepted RPC client connection");
                return true;
            }
        }

        return client_.isConnected();
    }

    bool RpcServer::pollOnce(std::function<void(const IncomingRequest&)> request_handler, std::chrono::milliseconds timeout)
    {
        if (!ensureClient())
        {
            return false;
        }

        if (!client_.waitForData(timeout, logger_))
        {
            return false;
        }

        RpcFrameHeader header{};
        std::vector<std::byte> payload;
        std::vector<std::byte> blob;

        std::array<uint8_t, FRAME_HEADER_SIZE> header_raw{};
        if (!client_.recvAll(header_raw.data(), header_raw.size(), timeout, logger_))
        {
            return false;
        }

        if (!deserializeHeader(header_raw.data(), header))
        {
            log(LogType::ERROR, "RPC server failed to deserialize header");
            return false;
        }

        if (header.payload_size > MAX_FRAME_SIZE || header.blob_size > MAX_FRAME_SIZE)
        {
            log(LogType::ERROR, "RPC server frame too large");
            return false;
        }

        payload.resize(header.payload_size);
        blob.resize(header.blob_size);
        if (header.payload_size > 0)
        {
            if (!client_.recvAll(reinterpret_cast<uint8_t*>(payload.data()), header.payload_size, timeout, logger_))
            {
                return false;
            }
        }
        if (header.blob_size > 0)
        {
            if (!client_.recvAll(reinterpret_cast<uint8_t*>(blob.data()), header.blob_size, timeout, logger_))
            {
                return false;
            }
        }

        if (header.kind == RpcMessageKind::REQUEST && payload.size() == sizeof(Request))
        {
            IncomingRequest req{};
            req.request_id = header.request_id;
            std::memcpy(&req.request, payload.data(), sizeof(Request));
            req.blob = std::move(blob);
            request_handler(req);
            return true;
        }

        log(LogType::WARNING, "RPC server received unsupported frame");
        return false;
    }

    bool RpcServer::sendResponse(uint32_t request_id, const Response& response, std::span<const std::byte> blob)
    {
        if (!client_.isConnected())
        {
            return false;
        }

        RpcFrameHeader header{
            .kind = RpcMessageKind::RESPONSE,
            .request_id = request_id,
            .payload_size = static_cast<uint32_t>(sizeof(Response)),
            .blob_size = static_cast<uint32_t>(blob.size())
        };

        std::span<const std::byte> payload(reinterpret_cast<const std::byte*>(&response), sizeof(Response));
        auto header_buf = serializeHeader(header);
        if (!client_.sendAll(header_buf.data(), header_buf.size(), logger_))
        {
            return false;
        }
        if (!payload.empty() && !client_.sendAll(reinterpret_cast<const uint8_t*>(payload.data()), payload.size(), logger_))
        {
            return false;
        }
        if (!blob.empty() && !client_.sendAll(reinterpret_cast<const uint8_t*>(blob.data()), blob.size(), logger_))
        {
            return false;
        }
        return true;
    }

    bool RpcServer::sendStatusMessage(const StatusMessage& status, std::span<const std::byte> blob)
    {
        if (!client_.isConnected())
        {
            return false;
        }

        RpcFrameHeader header{
            .kind = RpcMessageKind::STATUS,
            .request_id = 0,
            .payload_size = static_cast<uint32_t>(sizeof(StatusMessage)),
            .blob_size = static_cast<uint32_t>(blob.size())
        };

        std::span<const std::byte> payload(reinterpret_cast<const std::byte*>(&status), sizeof(StatusMessage));
        auto header_buf = serializeHeader(header);
        if (!client_.sendAll(header_buf.data(), header_buf.size(), logger_))
        {
            return false;
        }
        if (!client_.sendAll(reinterpret_cast<const uint8_t*>(payload.data()), payload.size(), logger_))
        {
            return false;
        }
        if (!blob.empty() && !client_.sendAll(reinterpret_cast<const uint8_t*>(blob.data()), blob.size(), logger_))
        {
            return false;
        }
        return true;
    }

    bool RpcServer::sendFinishedMessage(const FinishedMessage& finished, std::span<const std::byte> blob)
    {
        if (!client_.isConnected())
        {
            return false;
        }

        RpcFrameHeader header{
            .kind = RpcMessageKind::FINISHED,
            .request_id = finished.action_id ? static_cast<uint32_t>(finished.action_id) : 0u,
            .payload_size = static_cast<uint32_t>(sizeof(FinishedMessage)),
            .blob_size = static_cast<uint32_t>(blob.size())
        };

        std::span<const std::byte> payload(reinterpret_cast<const std::byte*>(&finished), sizeof(FinishedMessage));
        auto header_buf = serializeHeader(header);
        if (!client_.sendAll(header_buf.data(), header_buf.size(), logger_))
        {
            return false;
        }
        if (!client_.sendAll(reinterpret_cast<const uint8_t*>(payload.data()), payload.size(), logger_))
        {
            return false;
        }
        if (!blob.empty() && !client_.sendAll(reinterpret_cast<const uint8_t*>(blob.data()), blob.size(), logger_))
        {
            return false;
        }
        return true;
    }
}
