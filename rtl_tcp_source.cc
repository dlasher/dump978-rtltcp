// Copyright (c) 2024, FlightAware LLC.
// All rights reserved.
// Licensed under the 2-clause BSD license; see the LICENSE file

#include "rtl_tcp_source.h"
#include "exception.h"
#include "sample_source.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <chrono>
#include <iostream>
#include <thread>

using namespace flightaware::uat;

// RTL-TCP command codes
#define RTLTCP_SET_FREQ           0x01
#define RTLTCP_SET_SAMPLE_RATE    0x02
#define RTLTCP_SET_GAIN_MODE      0x03
#define RTLTCP_SET_GAIN           0x04
#define RTLTCP_SET_FREQ_CORR      0x05
#define RTLTCP_SET_IF_GAIN        0x06
#define RTLTCP_SET_AGC_MODE       0x08
#define RTLTCP_SET_DIRECT_SAMP    0x09
#define RTLTCP_SET_OFFSET_TUNING  0x0A
#define RTLTCP_SET_BIAS_TEE       0x0E

#pragma pack(push, 1)
struct rtltcp_command {
    unsigned char cmd;
    unsigned int param; // network byte order (big-endian)
};
#pragma pack(pop)

// Dongle info from rtl_tcp
struct dongle_info {
    char magic[4];            // "RTL0"
    uint32_t tuner_type;      // network byte order
    uint32_t tuner_gain_count; // network byte order
};

// Helper function to parse device settings (from librtlsdr)
static std::map<std::string, std::string> KwargsFromString(const std::string &markup) {
    std::map<std::string, std::string> kwargs;
    
    auto scan = markup.begin();
    while (scan < markup.end()) {
        auto key_start = scan;
        while (scan < markup.end() && *scan != '=' && *scan != ',') {
            ++scan;
        }
        
        std::string key(key_start, scan);
        if (scan == markup.end() || *scan == ',') {
            ++scan;
            kwargs[key] = "";
        } else {
            ++scan;
            auto value_start = scan;
            while (scan < markup.end() && *scan != ',') {
                ++scan;
            }
            
            std::string value(value_start, scan);
            ++scan;
            
            if (!key.empty()) {
                kwargs[key] = value;
            }
        }
    }
    
    return kwargs;
}

RtlTcpSampleSource::RtlTcpSampleSource(boost::asio::io_service &service, const std::string &device_name, const boost::program_options::variables_map &options)
    : device_name_(device_name), options_(options), socket_(-1), halt_(false), rtl_tcp_mode_(false),
      ppm_error_(0), gain_(0), auto_gain_(false), direct_sampling_(0), offset_tuning_(0), bias_tee_(0) {
    // Parse device_name to extract host and port
    // Expected format: "rtl_tcp:host:port" or "rtl_tcp:host" (default port 1234)
    std::string dev_str = device_name;
    if (dev_str.substr(0, 8) != "rtl_tcp:") {
        throw config_error("RTL_TCP device name must start with 'rtl_tcp:'");
    }
    
    std::string addr = dev_str.substr(8);
    size_t colon = addr.rfind(':');
    if (colon != std::string::npos) {
        host_ = addr.substr(0, colon);
        try {
            port_ = std::stoi(addr.substr(colon + 1));
        } catch (...) {
            throw config_error("Invalid port number in RTL_TCP device string");
        }
    } else {
        host_ = addr;
        port_ = 1234;
    }
    
    // Check for supported SDR options
    if (options_.count("sdr-ppm")) {
        ppm_error_ = options_["sdr-ppm"].as<double>();
    }
    
    if (options_.count("sdr-auto-gain")) {
        auto_gain_ = true;
    } else if (options_.count("sdr-gain")) {
        auto_gain_ = false;
        gain_ = static_cast<int>(options_["sdr-gain"].as<double>() * 10.0); // Convert dB to 0.1dB units
    } else {
        auto_gain_ = true; // Default to auto gain
    }
    
    // Parse device settings for optional parameters
    if (options_.count("sdr-device-settings")) {
        auto settings = KwargsFromString(options_["sdr-device-settings"].as<std::string>());
        for (auto &kv : settings) {
            if (kv.first == "direct") {
                direct_sampling_ = std::stoi(kv.second);
            } else if (kv.first == "offset") {
                offset_tuning_ = std::stoi(kv.second);
            } else if (kv.first == "bias") {
                bias_tee_ = std::stoi(kv.second);
            }
        }
    }
}

RtlTcpSampleSource::~RtlTcpSampleSource() {
    Stop();
}

void RtlTcpSampleSource::Init() {
    // Nothing to do in Init - we'll connect in Start
}

void RtlTcpSampleSource::Start() {
    if (!Connect()) {
        throw config_error("Failed to connect to RTL_TCP server");
    }
    
    // Send configuration to RTL_TCP server
    SendConfig();
    
    rtl_tcp_mode_ = true;
    halt_ = false;
    
    // Start the read thread
    read_thread_ = std::thread(&RtlTcpSampleSource::DoRead, this);
}

void RtlTcpSampleSource::Stop() {
    halt_ = true;
    Disconnect();
    if (read_thread_.joinable()) {
        read_thread_.join();
    }
}

bool RtlTcpSampleSource::Connect() {
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port_);
    
    struct addrinfo hints, *res, *res0;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_ADDRCONFIG;
    
    int ret = getaddrinfo(host_.c_str(), port_str, &hints, &res0);
    if (ret) {
        std::cerr << "rtl_tcp: address lookup failed for " << host_ << ": " << gai_strerror(ret) << std::endl;
        return false;
    }
    
    socket_ = -1;
    for (res = res0; res; res = res->ai_next) {
        socket_ = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (socket_ >= 0) {
            ret = connect(socket_, res->ai_addr, res->ai_addrlen);
            if (ret == 0) break;
            close(socket_);
            socket_ = -1;
        }
    }
    freeaddrinfo(res0);
    
    if (socket_ < 0) {
        std::cerr << "rtl_tcp: connection failed to " << host_ << ":" << port_ << std::endl;
        return false;
    }
    
    // Enable TCP no-delay
    int one = 1;
    setsockopt(socket_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    
    // Receive dongle info
    struct dongle_info info;
    ssize_t received = recv(socket_, &info, sizeof(info), 0);
    if (received != (ssize_t)sizeof(info)) {
        std::cerr << "rtl_tcp: failed to receive dongle info (got " << received << " bytes)" << std::endl;
        close(socket_);
        socket_ = -1;
        return false;
    }
    
    if (strncmp(info.magic, "RTL0", 4) != 0) {
        std::cerr << "rtl_tcp: invalid dongle magic" << std::endl;
        close(socket_);
        socket_ = -1;
        return false;
    }
    
    uint32_t tuner_number = ntohl(info.tuner_type);
    uint32_t gain_count = ntohl(info.tuner_gain_count);
    const char *tuner_names[] = {"Unknown", "E4000", "FC0012", "FC0013", "FC2580", "R820T", "R828D"};
    const char *tuner_name = (tuner_number <= 6) ? tuner_names[tuner_number] : "Invalid";
    
    std::cerr << "rtl_tcp: connected to " << host_ << ":" << port_ 
              << " (Tuner: " << tuner_name << ", " << gain_count << " gain steps)" << std::endl;
    
    return true;
}

void RtlTcpSampleSource::Disconnect() {
    if (socket_ >= 0) {
        shutdown(socket_, SHUT_RDWR);
        close(socket_);
        socket_ = -1;
    }
    rtl_tcp_mode_ = false;
}

bool RtlTcpSampleSource::SendCommand(unsigned char cmd, unsigned int param) {
    struct rtltcp_command command;
    command.cmd = cmd;
    command.param = htonl(param);
    
    ssize_t sent = send(socket_, &command, sizeof(command), MSG_NOSIGNAL);
    return (sent == sizeof(command));
}

void RtlTcpSampleSource::SendConfig() {
    // Send configuration commands to RTL_TCP server
    // Frequency: 978 MHz
    SendCommand(0x01, 978000000);
    
    // Sample rate: 2083333 Hz
    SendCommand(0x02, 2083333);
    
    // Gain mode: 0 (auto), 1 (manual), Gain in 0.1dB units
    if (auto_gain_) {
        SendCommand(0x03, 0);  // Gain mode: auto
    } else {
        SendCommand(0x03, 1);  // Gain mode: manual
        SendCommand(0x04, static_cast<unsigned int>(gain_));  // Gain in 0.1dB units
    }
    
    // Frequency correction (PPM)
    if (ppm_error_ != 0) {
        SendCommand(0x05, static_cast<unsigned int>(ppm_error_));
    }
    
    // Direct sampling
    if (direct_sampling_ != 0) {
        SendCommand(0x09, static_cast<unsigned int>(direct_sampling_));
    }
    
    // Offset tuning
    if (offset_tuning_ != 0) {
        SendCommand(0x0A, static_cast<unsigned int>(offset_tuning_));
    }
    
    // Bias-Tee
    if (bias_tee_ != 0) {
        SendCommand(0x0E, 1);
    }
}

void RtlTcpSampleSource::DoRead() {
    static const size_t BUFFER_SIZE = 524288; // Same as SoapySDR default
    
    std::vector<uint8_t> buffer(BUFFER_SIZE);
    
    while (!halt_) {
        ssize_t received = recv(socket_, buffer.data(), BUFFER_SIZE, MSG_WAITALL);
        
        if (received <= 0) {
            if (halt_) break;
            
            std::cerr << "rtl_tcp: " << (received == 0 ? "connection closed by server" : strerror(errno)) << std::endl;
            
            // Attempt reconnection
            Disconnect();
            
            while (!halt_) {
                std::cerr << "rtl_tcp: attempting to reconnect..." << std::endl;
                if (Connect()) {
                    // Re-send configuration
                    SendConfig();
                    std::cerr << "rtl_tcp: reconnected successfully" << std::endl;
                    break;
                }
                std::cerr << "rtl_tcp: reconnect failed, retrying in 5 seconds..." << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        } else {
            // Create a buffer and dispatch it
            Bytes data;
            data.reserve(received);
            for (ssize_t i = 0; i < received; ++i) {
                data.push_back(buffer[i]);
            }
            
            // Create a timestamp (we'll use millis since epoch)
            auto now = std::chrono::system_clock::now();
            auto duration = now - unix_epoch;
            std::uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
            
            // Dispatch to consumer
            DispatchBuffer(timestamp, data);
        }
    }
}


