
#pragma once

#include <array>
#include <atomic>
#include <cstdint>

struct MidiPacket
{
    uint8_t data[3]{};
    uint8_t size = 0;
    int32_t sampleOffset = 0;
};

class MidiQueue
{
public:
    static constexpr uint32_t Capacity = 1024;

    bool push(const MidiPacket& packet)
    {
        const uint32_t head =
            writePos.load(std::memory_order_relaxed);

        const uint32_t tail =
            readPos.load(std::memory_order_acquire);

        if (head - tail >= Capacity)
            return false;

        packets[head % Capacity] = packet;

        writePos.store(
            head + 1,
            std::memory_order_release
        );

        return true;
    }

    bool pop(MidiPacket& packet)
    {
        const uint32_t tail =
            readPos.load(std::memory_order_relaxed);

        const uint32_t head =
            writePos.load(std::memory_order_acquire);

        if (tail == head)
            return false;

        packet = packets[tail % Capacity];

        readPos.store(
            tail + 1,
            std::memory_order_release
        );

        return true;
    }

private:
    std::array<MidiPacket, Capacity> packets{};

    std::atomic<uint32_t> writePos{0};
    std::atomic<uint32_t> readPos{0};
};

