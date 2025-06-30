/*******************************************************************************

Filename    :   MessageQueue.h
Content     :   Simple, lock-free MPSC (multiple-producer, single-consumer) queue
                for passing messages to the render thread. On ARMv8 in particular,
                this implementation compiles to the optimal atomics for MPSC.

                TEMPLATE NOTE
                ------------------------------------------------------------------
                In the template app, this class is being used for exactly one thing
                (signalling exit), but real-world apps may want to use MessageQueue
                to communicate frequently between the main thread and the render
                thread. In general, you shouldn't cause the render thread to block
                for any reason during rendering, and so, in an effort to be as
                correct as possible, this class is non-blocking-on-read even in the
                completely hypothetical case of frequent writes.

Authors     :   Amanda Watson
License     :   Licensed under GPLv3 or any later version.
                Refer to the license.txt file included.

*******************************************************************************/

#pragma once

#include <atomic>
#include <array>
#include <cstdint>

/**
 * Message class
 *
 * Represents a message to be passed to the render thread.
 * Optionally contains a payload, which is interpreted based on the message
 * type.
 */

class Message {
public:
    enum class Type {
        EXIT_NEEDED = 0, // payload ignored
    };

    constexpr Message(const Type t = Type::EXIT_NEEDED,
                               const uint64_t payload = 0) noexcept
            : mType{t}, mPayload{payload} {}

    Type     mType;
    uint64_t mPayload;
};


/**
 * Message queue class -- MPSC ring buffer
 */
template <std::size_t kCapacityPow2 = 64>
class MessageQueue final {
    static_assert((kCapacityPow2 & (kCapacityPow2 - 1)) == 0,
                  "Capacity must be a power of two");
public:
    MessageQueue() = default;
    ~MessageQueue() = default;

    /**
     * Push a message onto the queue
     * @param msg The message to push
     * @return void
     */
    void Post(const Message& msg) noexcept {
        const std::size_t pos =
                mTail.fetch_add(1, std::memory_order_acquire);  // reserve slot

        if (pos - mHead.load(std::memory_order_acquire) >= kCapacityPow2) {
            // queue is full – roll back reservation & drop
            mTail.fetch_sub(1, std::memory_order_release);
            return;
        }
        mBuffer[pos & kMask] = msg;
        // publish: make sure payload is visible before tail is seen advanced
        std::atomic_thread_fence(std::memory_order_release);
    }

    /**
     * Pop a message off the queue.
     * @param msg The message to pop
     * @return bool false if the queue is empty and no message was popped, true
     * if a message was popped/returned.
     */
    [[nodiscard]] bool Poll(Message& outMsg) noexcept {
        std::size_t head = mHead.load(std::memory_order_acquire);

        if (head == mTail.load(std::memory_order_acquire)) {
            return false;                        // empty
        }

        outMsg = mBuffer[head & kMask];

        mHead.store(head + 1, std::memory_order_release);
        return true;
    }

private:
    static constexpr std::size_t kMask = kCapacityPow2 - 1;

    std::array<Message, kCapacityPow2> mBuffer{};

    // Aligning head and tail to separate cache lines prevents false sharing
    alignas(64) std::atomic<std::size_t> mHead{0};
    alignas(64) std::atomic<std::size_t> mTail{0};
};