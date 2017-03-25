/*
 * Copyright (c) 2015 Mikhail Baranov
 * Copyright (c) 2015 Victor Gaydov
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "roc_config/config.h"
#include "roc_core/panic.h"
#include "roc_core/log.h"
#include "roc_fec/of_block_decoder.h"

namespace roc {
namespace fec {

namespace {

const size_t SYMB_SZ = ROC_CONFIG_DEFAULT_PACKET_SIZE;

} // namespace

OF_BlockDecoder::OF_BlockDecoder(core::IByteBufferComposer& composer)
    : of_inst_(NULL)
    , of_inst_inited_(false)
    , composer_(composer)
    , buffers_(N_DATA_PACKETS + N_FEC_PACKETS)
    , sym_tab_(N_DATA_PACKETS + N_FEC_PACKETS)
    , received_(N_DATA_PACKETS + N_FEC_PACKETS) {

    // Use Reed-Solomon Codec.
    if (codec_id_ == OF_CODEC_REED_SOLOMON_GF_2_M_STABLE){
        roc_log(LOG_TRACE, "initializing Reed-Solomon decoder");

        fec_codec_params_.rs_params_.m = 8;

        of_inst_params_ = (of_parameters_t*)&fec_codec_params_.rs_params_;

    // Use LDPC-Staircase.
    } else {
        roc_log(LOG_TRACE, "initializing LDPC decoder");

        fec_codec_params_.ldpc_params_.prng_seed = 1297501556;
        fec_codec_params_.ldpc_params_.N1 = 7;

        of_inst_params_ = (of_parameters_t*)&fec_codec_params_.ldpc_params_; 
    }

    of_inst_params_->nb_source_symbols = N_DATA_PACKETS;
    of_inst_params_->nb_repair_symbols = N_FEC_PACKETS;
    of_inst_params_->encoding_symbol_length = SYMB_SZ;
    of_verbosity = 0;

    OF_BlockDecoder::reset(); // non-virtual call from ctor
}

OF_BlockDecoder::~OF_BlockDecoder() {
}

void OF_BlockDecoder::write(size_t index, const core::IByteBufferConstSlice& buffer) {
    if (index >= N_DATA_PACKETS + N_FEC_PACKETS) {
        roc_panic("ldpc decoder: index out of bounds: index=%lu, size=%lu",
                  (unsigned long)index, (unsigned long)(N_DATA_PACKETS + N_FEC_PACKETS));
    }

    if (!buffer) {
        roc_panic("ldpc decoder: NULL buffer");
    }

    if (buffer.size() != SYMB_SZ) {
        roc_panic("ldpc decoder: invalid payload size: size=%lu, expected=%lu",
                  (unsigned long)buffer.size(), (unsigned long)SYMB_SZ);
    }

    if (buffers_[index]) {
        roc_panic("ldpc decoder: can't overwrite buffer: index=%lu",
                  (unsigned long)index);
    }

    defecation_attempted_ = false;
    ++packets_rcvd_;

    // const_cast<> is OK since OpenFEC will not modify this buffer.
    sym_tab_[index] = const_cast<uint8_t*>(buffer.data());
    buffers_[index] = buffer;
    received_[index] = true;
}

core::IByteBufferConstSlice OF_BlockDecoder::repair(size_t index) {
    if (!buffers_[index] && !defecation_attempted_) {
        defecation_attempted_ = true;

        if ((packets_rcvd_ >= N_DATA_PACKETS)
            && (of_set_available_symbols(of_inst_, &sym_tab_[0]) != OF_STATUS_OK)) {
            return core::IByteBufferConstSlice();
        }

        of_finish_decoding(of_inst_);

        if (of_get_source_symbols_tab(of_inst_, &sym_tab_[0]) != OF_STATUS_OK) {
            return core::IByteBufferConstSlice();
        }
    }

    return buffers_[index];
}

void OF_BlockDecoder::reset() {
    report_();

    packets_rcvd_ = 0;
    defecation_attempted_ = false;
    if (of_inst_inited_ && of_inst_) {
        of_release_codec_instance(of_inst_);
    }

    if (OF_STATUS_OK != of_create_codec_instance(
                            &of_inst_, codec_id_, OF_DECODER, 0)) {
        roc_panic("ldpc decoder: of_create_codec_instance() failed");
    }

    roc_panic_if(of_inst_ == NULL);

    if (OF_STATUS_OK
        != of_set_fec_parameters(of_inst_, of_inst_params_)) {
        roc_panic("ldpc decoder: of_set_fec_parameters() failed");
    }

    of_inst_inited_ = true;

    if (OF_STATUS_OK
        != of_set_callback_functions(of_inst_, source_cb_,
            codec_id_ == OF_CODEC_REED_SOLOMON_GF_2_M_STABLE ? NULL : repair_cb_,
            (void*)this)) {
        roc_panic("ldpc decoder: of_set_callback_functions() failed");
    }

    for (size_t i = 0; i < buffers_.size(); ++i) {
        buffers_[i] = core::IByteBufferConstSlice();
        sym_tab_[i] = NULL;
        received_[i] = false;
    }
}

void OF_BlockDecoder::report_() {
    size_t n_lost = 0, n_repaired = 0;

    char status1[N_DATA_PACKETS + 1] = {};
    char status2[N_FEC_PACKETS + 1] = {};

    for (size_t i = 0; i < buffers_.size(); ++i) {
        char* status = (i < N_DATA_PACKETS ? &status1[i] : &status2[i - N_DATA_PACKETS]);

        if (buffers_[i]) {
            if (received_[i]) {
                *status = '.';
            } else {
                *status = 'r';
                n_repaired++;
                n_lost++;
            }
        } else {
            if (i < N_DATA_PACKETS) {
                *status = 'X';
            } else {
                *status = 'x';
            }
            n_lost++;
        }
    }

    if (n_lost == 0) {
        return;
    }

    roc_log(LOG_TRACE, "ldpc decoder: repaired %u/%u/%u %s %s",
            (unsigned)n_repaired,      //
            (unsigned)n_lost,          //
            (unsigned)buffers_.size(), //
            status1,                   //
            status2);
}

void* OF_BlockDecoder::make_buffer_(const size_t index) {
    roc_panic_if_not(index < N_DATA_PACKETS + N_FEC_PACKETS);

    if (core::IByteBufferPtr buffer = composer_.compose()) {
        buffer->set_size(SYMB_SZ);
        buffers_[index] = *buffer;
        return buffer->data();
    } else {
        roc_log(LOG_TRACE, "ldpc decoder: can't allocate buffer");
        return NULL;
    }
}

void* OF_BlockDecoder::source_cb_(void* context, uint32_t size, uint32_t index) {
    roc_panic_if(context == NULL);
    roc_panic_if(size != SYMB_SZ);

    OF_BlockDecoder& self = *(OF_BlockDecoder*)context;
    return self.make_buffer_(index);
}

void* OF_BlockDecoder::repair_cb_(void*, uint32_t, uint32_t) {
    return NULL;
}

} // namespace fec
} // namespace roc