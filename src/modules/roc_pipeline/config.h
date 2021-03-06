/*
 * Copyright (c) 2017 Roc authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

//! @file roc_pipeline/config.h
//! @brief Pipeline config.

#ifndef ROC_PIPELINE_CONFIG_H_
#define ROC_PIPELINE_CONFIG_H_

#include "roc_audio/latency_monitor.h"
#include "roc_audio/resampler.h"
#include "roc_audio/watchdog.h"
#include "roc_core/stddefs.h"
#include "roc_core/time.h"
#include "roc_fec/config.h"
#include "roc_packet/units.h"
#include "roc_rtp/headers.h"
#include "roc_rtp/validator.h"

namespace roc {
namespace pipeline {

//! Default sample rate, number of samples per second.
const size_t DefaultSampleRate = 44100;

//! Default channel mask.
const packet::channel_mask_t DefaultChannelMask = 0x3;

//! Default packet length.
const core::nanoseconds_t DefaultPacketLength = 7 * core::Millisecond;

//! Default internal frame size.
const size_t DefaultInternalFrameSize = 640;

//! Default minum latency relative to target latency.
const int DefaultMinLatencyFactor = -1;

//! Default maximum latency relative to target latency.
const int DefaultMaxLatencyFactor = 2;

//! Protocol identifier.
enum Protocol {
    //! Protocol is not set.
    Proto_None,

    //! Bare RTP.
    Proto_RTP,

    //! RTP source packet + FECFRAME Reed-Solomon footer (m=8).
    Proto_RTP_RSm8_Source,

    //! FEC repair packet + FECFRAME Reed-Solomon header (m=8).
    Proto_RSm8_Repair,

    //! RTP source packet + FECFRAME LDPC footer.
    Proto_RTP_LDPC_Source,

    //! FEC repair packet + FECFRAME LDPC header.
    Proto_LDPC_Repair
};

//! Port parameters.
//! @remarks
//!  On receiver, defines a listened port parameters. On sender,
//!  defines a destination port parameters.
struct PortConfig {
    //! Port address.
    packet::Address address;

    //! Port protocol.
    Protocol protocol;

    PortConfig()
        : protocol(Proto_None) {
    }
};

//! Sender parameters.
struct SenderConfig {
    //! Resampler parameters.
    audio::ResamplerConfig resampler;

    //! FEC scheme parameters.
    fec::Config fec;

    //! Number of samples per second per channel.
    size_t input_sample_rate;

    //! Channel mask.
    packet::channel_mask_t input_channels;

    //! Number of samples for internal frames.
    size_t internal_frame_size;

    //! Packet length, in nanoseconds.
    core::nanoseconds_t packet_length;

    //! RTP payload type for audio packets.
    rtp::PayloadType payload_type;

    //! Resample frames with a constant ratio.
    bool resampling;

    //! Interleave packets.
    bool interleaving;

    //! Constrain receiver speed using a CPU timer according to the sample rate.
    bool timing;

    //! Fill unitialized data with large values to make them more noticable.
    bool poisoning;

    SenderConfig()
        : input_sample_rate(DefaultSampleRate)
        , input_channels(DefaultChannelMask)
        , internal_frame_size(DefaultInternalFrameSize)
        , packet_length(DefaultPacketLength)
        , payload_type(rtp::PayloadType_L16_Stereo)
        , resampling(false)
        , interleaving(false)
        , timing(false)
        , poisoning(false) {
    }
};

//! Receiver session parameters.
//! @remarks
//!  Defines per-session parameters on the receiver side.
struct ReceiverSessionConfig {
    //! Channel mask.
    packet::channel_mask_t channels;

    //! Packet length, in nanoseconds.
    core::nanoseconds_t packet_length;

    //! Target latency, nanoseconds.
    core::nanoseconds_t target_latency;

    //! FEC scheme parameters.
    fec::Config fec;

    //! RTP validator parameters.
    rtp::ValidatorConfig rtp_validator;

    //! LatencyMonitor parameters.
    audio::LatencyMonitorConfig latency_monitor;

    //! Watchdog parameters.
    audio::WatchdogConfig watchdog;

    //! Resampler parameters.
    audio::ResamplerConfig resampler;

    ReceiverSessionConfig()
        : channels(DefaultChannelMask)
        , packet_length(DefaultPacketLength)
        , target_latency(200 * core::Millisecond) {
        latency_monitor.min_latency = target_latency * DefaultMinLatencyFactor;
        latency_monitor.max_latency = target_latency * DefaultMaxLatencyFactor;
    }
};

//! Receiver output parameters.
//! @remarks
//!  Defines common output parameters on the receiver side.
struct ReceiverOutputConfig {
    //! Number of samples per second per channel.
    size_t sample_rate;

    //! Channel mask.
    packet::channel_mask_t channels;

    //! Number of samples for internal frames.
    size_t internal_frame_size;

    //! Perform resampling to compensate sender and receiver frequency difference.
    bool resampling;

    //! Constrain receiver speed using a CPU timer according to the sample rate.
    bool timing;

    //! Fill uninitialized data with large values to make them more noticeable.
    bool poisoning;

    //! Insert weird beeps instead of silence on packet loss.
    bool beeping;

    ReceiverOutputConfig()
        : sample_rate(DefaultSampleRate)
        , channels(DefaultChannelMask)
        , internal_frame_size(DefaultInternalFrameSize)
        , resampling(false)
        , timing(false)
        , poisoning(false)
        , beeping(false) {
    }
};

//! Receiver parameters.
struct ReceiverConfig {
    //! Default parameters for receiver session.
    ReceiverSessionConfig default_session;

    //! Parameters for receiver output.
    ReceiverOutputConfig output;
};

} // namespace pipeline
} // namespace roc

#endif // ROC_PIPELINE_CONFIG_H_
