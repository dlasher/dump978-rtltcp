// -*- c++ -*-

// Copyright (c) 2024, FlightAware LLC.
// All rights reserved.
// Licensed under the 2-clause BSD license; see the LICENSE file

#ifndef DUMP978_RTL_TCP_SOURCE_H
#define DUMP978_RTL_TCP_SOURCE_H

#include <atomic>
#include <memory>
#include <thread>

#include "sample_source.h"

namespace flightaware::uat {
    class RtlTcpSampleSource : public SampleSource {
      public:
        static SampleSource::Pointer Create(boost::asio::io_service &service, const std::string &device_name, const boost::program_options::variables_map &options) { return Pointer(new RtlTcpSampleSource(service, device_name, options)); }

        virtual ~RtlTcpSampleSource();

        void Init() override;
        void Start() override;
        void Stop() override;
        SampleFormat Format() override { return SampleFormat::CU8; }

      private:
        RtlTcpSampleSource(boost::asio::io_service &service, const std::string &device_name, const boost::program_options::variables_map &options);

        bool Connect();
        void Disconnect();
        bool SendCommand(unsigned char cmd, unsigned int param);
        void SendConfig();
        void DoRead();

        std::string device_name_;
        boost::program_options::variables_map options_;
        std::string host_;
        int port_;
        int socket_;
        std::thread read_thread_;
        std::atomic_bool halt_;
        std::atomic_bool rtl_tcp_mode_;
        
        // Configuration
        int ppm_error_;
        int gain_;
        bool auto_gain_;
        int direct_sampling_;
        int offset_tuning_;
        int bias_tee_;
    };
}; // namespace flightaware::uat

#endif
