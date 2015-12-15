/*
 * Copyright (c) 2015 Mikhail Baranov
 * Copyright (c) 2015 Victor Gaydov
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "roc_core/helpers.h"
#include "roc_core/panic.h"
#include "roc_core/log.h"

#include "roc_fec/decoder.h"

#define SEQ_IS_BEFORE(a, b) ROC_IS_BEFORE(packet::signed_seqnum_t, a, b)
#define SEQ_IS_BEFORE_EQ(a, b) ROC_IS_BEFORE_EQ(packet::signed_seqnum_t, a, b)
#define SEQ_SUBTRACT(a, b) (packet::seqnum_t) ROC_SUBTRACT(packet::signed_seqnum_t, a, b)

namespace roc {
namespace fec {

Decoder::Decoder(IBlockDecoder& block_decoder,
                 packet::IPacketReader& data_reader,
                 packet::IPacketReader& fec_reader,
                 packet::IPacketParser& parser)
    : block_decoder_(block_decoder)
    , data_reader_(data_reader)
    , fec_reader_(fec_reader)
    , parser_(parser)
    , data_queue_(0)
    , fec_queue_(0)
    , data_packets_(N_DATA_PACKETS)
    , fec_packets_(N_FEC_PACKETS)
    , decoding_started_(false)
    , can_repair_(false)
    , next_packet_(0)
    , cur_block_sn_(0)
    , n_packets_(0) {
}

packet::IPacketConstPtr Decoder::read() {
    packet::IPacketConstPtr pp = read_();
    if (pp) {
        n_packets_++;
    }
    return pp;
}

packet::IPacketConstPtr Decoder::read_() {
    fetch_packets_();

    if (!decoding_started_) {
        packet::IPacketConstPtr pp = data_queue_.head();
        if (pp) {
            cur_block_sn_ = pp->seqnum();
            skip_fec_packets_();
        }

        if (!pp || !pp->marker()) {
            return data_queue_.read();
        }

        roc_log(LOG_DEBUG, "decoder: got marker bit, start decoding:"
                           " n_packets_before=%u blk_sn=%lu",
                n_packets_, (unsigned long)cur_block_sn_);

        decoding_started_ = true;
    }

    return get_next_packet_();
}

packet::IPacketConstPtr Decoder::get_next_packet_() {
    update_packets_();

    packet::IPacketConstPtr pp = data_packets_[next_packet_];

    do {
        if (!pp) {
            try_repair_();

            size_t pos;
            for (pos = next_packet_; pos < data_packets_.size(); pos++) {
                if (data_packets_[pos]) {
                    break;
                }
            }

            if (pos == data_packets_.size()) {
                if (data_queue_.size() == 0) {
                    return NULL;
                }
            } else {
                pp = data_packets_[pos++];
            }

            next_packet_ = pos;
        } else {
            next_packet_++;
        }

        if (next_packet_ == data_packets_.size()) {
            next_block_();
        }
    } while (!pp);

    return pp;
}

void Decoder::next_block_() {
    roc_log(LOG_FLOOD, "decoder: next block: sn=%lu", (unsigned long)cur_block_sn_);

    for (size_t n = 0; n < data_packets_.size(); n++) {
        data_packets_[n] = NULL;
    }

    for (size_t n = 0; n < fec_packets_.size(); n++) {
        fec_packets_[n] = NULL;
    }

    cur_block_sn_ += data_packets_.size();
    next_packet_ = 0;

    can_repair_ = false;
    update_packets_();
}

void Decoder::try_repair_() {
    if (!can_repair_) {
        return;
    }

    for (size_t n = 0; n < data_packets_.size(); n++) {
        if (!data_packets_[n]) {
            continue;
        }
        block_decoder_.write(n, data_packets_[n]->raw_data());
    }

    for (size_t n = 0; n < fec_packets_.size(); n++) {
        if (!fec_packets_[n]) {
            continue;
        }
        block_decoder_.write(data_packets_.size() + n, fec_packets_[n]->payload());
    }

    for (size_t n = 0; n < data_packets_.size(); n++) {
        if (data_packets_[n]) {
            continue;
        }

        core::IByteBufferConstSlice buffer = block_decoder_.repair(n);
        if (!buffer) {
            continue;
        }

        packet::IPacketConstPtr pp = parser_.parse(buffer);
        if (!pp) {
            roc_log(LOG_TRACE, "decoder: dropping unparsable defecated packet");
            continue;
        }

        data_packets_[n] = pp;
    }

    block_decoder_.reset();
    can_repair_ = false;
}

void Decoder::fetch_packets_() {
    while (data_queue_.size() <= data_packets_.size() * 2) {
        if (packet::IPacketConstPtr pp = data_reader_.read()) {
            data_queue_.write(pp);
        } else {
            break;
        }
    }

    while (fec_queue_.size() <= fec_packets_.size() * 2) {
        if (packet::IPacketConstPtr pp = fec_reader_.read()) {
            if (pp->type() != packet::IFECPacket::Type) {
                roc_panic("decoder: fec reader returned packet of wrong type");
            }
            fec_queue_.write(pp);
        } else {
            break;
        }
    }
}

void Decoder::update_packets_() {
    update_data_packets_();
    update_fec_packets_();
}

void Decoder::update_data_packets_() {
    unsigned n_fetched = 0, n_added = 0, n_dropped = 0;

    for (;;) {
        packet::IPacketConstPtr pp = data_queue_.head();
        if (!pp) {
            break;
        }

        if (!SEQ_IS_BEFORE(pp->seqnum(), cur_block_sn_ + data_packets_.size())) {
            break;
        }

        data_queue_.read();
        n_fetched++;

        if (SEQ_IS_BEFORE(pp->seqnum(), cur_block_sn_)) {
            roc_log(LOG_TRACE, "decoder: dropping data packet from previous block:"
                               " blk_sn=%lu pkt_sn=%lu",
                    (unsigned long)cur_block_sn_, (unsigned long)pp->seqnum());
            n_dropped++;
            continue;
        }

        const size_t p_num = SEQ_SUBTRACT(pp->seqnum(), cur_block_sn_);

        if (!data_packets_[p_num]) {
            can_repair_ = true;
            data_packets_[p_num] = pp;
            n_added++;
        }
    }

    if (n_dropped != 0 || n_fetched != n_added) {
        roc_log(LOG_DEBUG, "decoder: data queue: fetched=%u added=%u dropped=%u",
                n_fetched, n_added, n_dropped);
    }
}

void Decoder::update_fec_packets_() {
    unsigned n_fetched = 0, n_added = 0, n_dropped = 0;

    for (;;) {
        packet::IPacketConstPtr pp = fec_queue_.head();
        if (!pp) {
            break;
        }

        packet::IFECPacketConstPtr fp = static_cast<const packet::IFECPacket*>(pp.get());

        if (!SEQ_IS_BEFORE(fp->data_blknum(), cur_block_sn_ + data_packets_.size())) {
            break;
        }

        fec_queue_.read();
        n_fetched++;

        if (SEQ_IS_BEFORE(fp->data_blknum(), cur_block_sn_)) {
            roc_log(LOG_TRACE, "decoder: dropping fec packet from previous block:"
                               " blk_sn=%lu pkt_data_blk=%lu",
                    (unsigned long)cur_block_sn_, (unsigned long)fp->data_blknum());
            n_dropped++;
            continue;
        }

        if (!SEQ_IS_BEFORE_EQ(fp->fec_blknum(), fp->seqnum())) {
            roc_log(LOG_TRACE, "decoder: dropping invalid fec packet:"
                               " pkt_sn=%lu pkt_fec_blk=%lu",
                    (unsigned long)fp->seqnum(), (unsigned long)fp->fec_blknum());
            n_dropped++;
            continue;
        }

        const size_t p_num = SEQ_SUBTRACT(fp->seqnum(), fp->fec_blknum());

        if (!fec_packets_[p_num]) {
            can_repair_ = true;
            fec_packets_[p_num] = fp;
            n_added++;
        }
    }

    if (n_dropped != 0 || n_fetched != n_added) {
        roc_log(LOG_DEBUG, "decoder: fec queue: fetched=%u added=%u dropped=%u",
                n_fetched, n_added, n_dropped);
    }
}

void Decoder::skip_fec_packets_() {
    unsigned n_skipped = 0;

    for (;;) {
        packet::IPacketConstPtr pp = fec_queue_.head();
        if (!pp) {
            break;
        }

        packet::IFECPacketConstPtr fp = static_cast<const packet::IFECPacket*>(pp.get());

        if (!SEQ_IS_BEFORE(fp->data_blknum(), cur_block_sn_)) {
            break;
        }

        roc_log(LOG_TRACE, "decoder: dropping fec packet, decoding not started:"
                           " min_sn=%lu pkt_data_blk=%lu",
                (unsigned long)cur_block_sn_, (unsigned long)fp->data_blknum());

        fec_queue_.read();
        n_skipped++;
    }

    if (n_skipped != 0) {
        roc_log(LOG_DEBUG, "decoder: fec queue: skipped=%u", n_skipped);
    }
}

} // namespace fec
} // namespace roc