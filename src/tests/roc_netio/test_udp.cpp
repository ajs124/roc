/*
 * Copyright (c) 2015 Roc authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <CppUTest/TestHarness.h>

#include "roc_core/buffer_pool.h"
#include "roc_core/heap_allocator.h"
#include "roc_netio/transceiver.h"
#include "roc_packet/concurrent_queue.h"
#include "roc_packet/packet_pool.h"
#include "roc_packet/parse_address.h"

namespace roc {
namespace netio {

namespace {

enum { NumIterations = 20, NumPackets = 10, BufferSize = 125 };

core::HeapAllocator allocator;
core::BufferPool<uint8_t> buffer_pool(allocator, BufferSize, true);
packet::PacketPool packet_pool(allocator, true);

} // namespace

TEST_GROUP(udp) {
    packet::Address new_address() {
        packet::Address addr;
        CHECK(packet::parse_address("127.0.0.1:0", addr));
        return addr;
    }

    core::Slice<uint8_t> new_buffer(int value) {
        core::Slice<uint8_t> buf = new (buffer_pool) core::Buffer<uint8_t>(buffer_pool);
        CHECK(buf);
        buf.resize(BufferSize);
        for (int n = 0; n < BufferSize; n++) {
            buf.data()[n] = uint8_t((value + n) & 0xff);
        }
        return buf;
    }

    packet::PacketPtr
    new_packet(packet::Address tx_addr, packet::Address rx_addr, int value) {
        packet::PacketPtr pp = new (packet_pool) packet::Packet(packet_pool);
        CHECK(pp);

        pp->add_flags(packet::Packet::FlagUDP);

        pp->udp()->src_addr = tx_addr;
        pp->udp()->dst_addr = rx_addr;

        pp->set_data(new_buffer(value));

        return pp;
    }

    void check_packet(const packet::PacketPtr& pp,
                      packet::Address tx_addr,
                      packet::Address rx_addr,
                      int value) {
        CHECK(pp);

        CHECK(pp->udp());
        CHECK(pp->data());

        CHECK(pp->udp()->src_addr == tx_addr);
        CHECK(pp->udp()->dst_addr == rx_addr);

        core::Slice<uint8_t> expected = new_buffer(value);

        UNSIGNED_LONGS_EQUAL(expected.size(), pp->data().size());
        CHECK(memcmp(pp->data().data(), expected.data(), expected.size()) == 0);
    }
};

TEST(udp, one_sender_one_receiver_single_thread) {
    packet::ConcurrentQueue rx_queue;

    packet::Address tx_addr = new_address();
    packet::Address rx_addr = new_address();

    Transceiver trx(packet_pool, buffer_pool, allocator);
    CHECK(trx.valid());

    packet::IWriter* tx_sender = trx.add_udp_sender(tx_addr);
    CHECK(tx_sender);

    CHECK(trx.add_udp_receiver(rx_addr, rx_queue));

    CHECK(trx.start());

    for (int i = 0; i < NumIterations; i++) {
        for (int p = 0; p < NumPackets; p++) {
            tx_sender->write(new_packet(tx_addr, rx_addr, p));
        }
        for (int p = 0; p < NumPackets; p++) {
            check_packet(rx_queue.read(), tx_addr, rx_addr, p);
        }
    }

    trx.stop();
    trx.join();

    trx.remove_port(tx_addr);
    trx.remove_port(rx_addr);
}

TEST(udp, one_sender_one_receiver_separate_threads) {
    packet::ConcurrentQueue rx_queue;

    packet::Address tx_addr = new_address();
    packet::Address rx_addr = new_address();

    Transceiver tx(packet_pool, buffer_pool, allocator);
    CHECK(tx.valid());

    packet::IWriter* tx_sender = tx.add_udp_sender(tx_addr);
    CHECK(tx_sender);

    Transceiver rx(packet_pool, buffer_pool, allocator);
    CHECK(rx.valid());

    CHECK(rx.add_udp_receiver(rx_addr, rx_queue));

    CHECK(tx.start());
    CHECK(rx.start());

    for (int i = 0; i < NumIterations; i++) {
        for (int p = 0; p < NumPackets; p++) {
            tx_sender->write(new_packet(tx_addr, rx_addr, p));
        }
        for (int p = 0; p < NumPackets; p++) {
            check_packet(rx_queue.read(), tx_addr, rx_addr, p);
        }
    }

    tx.stop();
    tx.join();

    rx.stop();
    rx.join();

    tx.remove_port(tx_addr);
    rx.remove_port(rx_addr);
}

TEST(udp, one_sender_multiple_receivers) {
    packet::ConcurrentQueue rx_queue1;
    packet::ConcurrentQueue rx_queue2;
    packet::ConcurrentQueue rx_queue3;

    packet::Address tx_addr = new_address();

    packet::Address rx_addr1 = new_address();
    packet::Address rx_addr2 = new_address();
    packet::Address rx_addr3 = new_address();

    Transceiver tx(packet_pool, buffer_pool, allocator);
    CHECK(tx.valid());

    packet::IWriter* tx_sender = tx.add_udp_sender(tx_addr);
    CHECK(tx_sender);

    Transceiver rx1(packet_pool, buffer_pool, allocator);
    CHECK(rx1.valid());
    CHECK(rx1.add_udp_receiver(rx_addr1, rx_queue1));

    Transceiver rx23(packet_pool, buffer_pool, allocator);
    CHECK(rx23.valid());
    CHECK(rx23.add_udp_receiver(rx_addr2, rx_queue2));
    CHECK(rx23.add_udp_receiver(rx_addr3, rx_queue3));

    CHECK(tx.start());

    CHECK(rx1.start());
    CHECK(rx23.start());

    for (int i = 0; i < NumIterations; i++) {
        for (int p = 0; p < NumPackets; p++) {
            tx_sender->write(new_packet(tx_addr, rx_addr1, p * 10));
            tx_sender->write(new_packet(tx_addr, rx_addr2, p * 20));
            tx_sender->write(new_packet(tx_addr, rx_addr3, p * 30));
        }
        for (int p = 0; p < NumPackets; p++) {
            check_packet(rx_queue1.read(), tx_addr, rx_addr1, p * 10);
            check_packet(rx_queue2.read(), tx_addr, rx_addr2, p * 20);
            check_packet(rx_queue3.read(), tx_addr, rx_addr3, p * 30);
        }
    }

    tx.stop();
    tx.join();

    rx1.stop();
    rx1.join();

    rx23.stop();
    rx23.join();

    tx.remove_port(tx_addr);
    rx1.remove_port(rx_addr1);
    rx23.remove_port(rx_addr2);
    rx23.remove_port(rx_addr3);
}

TEST(udp, multiple_senders_one_receiver) {
    packet::ConcurrentQueue rx_queue;

    packet::Address tx_addr1 = new_address();
    packet::Address tx_addr2 = new_address();
    packet::Address tx_addr3 = new_address();

    packet::Address rx_addr = new_address();

    Transceiver tx1(packet_pool, buffer_pool, allocator);
    CHECK(tx1.valid());

    packet::IWriter* tx_sender1 = tx1.add_udp_sender(tx_addr1);
    CHECK(tx_sender1);

    Transceiver tx23(packet_pool, buffer_pool, allocator);
    CHECK(tx1.valid());

    packet::IWriter* tx_sender2 = tx23.add_udp_sender(tx_addr2);
    CHECK(tx_sender2);

    packet::IWriter* tx_sender3 = tx23.add_udp_sender(tx_addr3);
    CHECK(tx_sender3);

    Transceiver rx(packet_pool, buffer_pool, allocator);
    CHECK(rx.valid());
    CHECK(rx.add_udp_receiver(rx_addr, rx_queue));

    CHECK(tx1.start());
    CHECK(tx23.start());

    CHECK(rx.start());

    for (int i = 0; i < NumIterations; i++) {
        for (int p = 0; p < NumPackets; p++) {
            tx_sender1->write(new_packet(tx_addr1, rx_addr, p * 10));
        }
        for (int p = 0; p < NumPackets; p++) {
            check_packet(rx_queue.read(), tx_addr1, rx_addr, p * 10);
        }
        for (int p = 0; p < NumPackets; p++) {
            tx_sender2->write(new_packet(tx_addr2, rx_addr, p * 20));
        }
        for (int p = 0; p < NumPackets; p++) {
            check_packet(rx_queue.read(), tx_addr2, rx_addr, p * 20);
        }
        for (int p = 0; p < NumPackets; p++) {
            tx_sender3->write(new_packet(tx_addr3, rx_addr, p * 30));
        }
        for (int p = 0; p < NumPackets; p++) {
            check_packet(rx_queue.read(), tx_addr3, rx_addr, p * 30);
        }
    }

    tx1.stop();
    tx1.join();

    tx23.stop();
    tx23.join();

    rx.stop();
    rx.join();

    tx1.remove_port(tx_addr1);
    tx23.remove_port(tx_addr2);
    tx23.remove_port(tx_addr3);
    rx.remove_port(rx_addr);
}

} // namespace netio
} // namespace roc
