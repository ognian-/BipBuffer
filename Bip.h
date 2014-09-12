/*
 * Bi-partitioned circular buffer.
 */

#ifndef BIP_H_INCLUDED
#define BIP_H_INCLUDED

#include <cstdint>
#include <cstring>

#ifdef BIP_HAVE_THREAD

#include <thread>
#include <mutex>
#include <condition_variable>

#endif // BIP_HAVE_THREAD

namespace bip {

namespace internal {

struct Partition {
    inline Partition(uint8_t* const * l, const uint8_t* const * u);
    inline void put(const uint8_t* d, std::size_t s);
    inline void get(uint8_t* d, std::size_t s);
    inline void skip(std::size_t s);
    inline std::size_t avail() const;
    inline std::size_t free() const;
    inline void reset();
    uint8_t* begin;
    uint8_t* end;
private:
    uint8_t* const * lower;
    const uint8_t* const * upper;
}; // struct Partition

} // namespace internal

class BIP {
public:
    /*
     * Construct a BIP buffer at memory block 'buf', with total size 's'
     */
    BIP(uint8_t* buf, std::size_t s) :
            lower(buf),
            upper(buf + s),
            A(&lower, &B.begin),
            B(&A.end, &upper),
            Get(&B),
            Put(&B),
            NextGet(&B),
            NextPut(&B),
            cons(false) {
    }

    /*
     * Attempt to write 's' bytes from 'd'. Returns actual bytes written
     */
    std::size_t put(const uint8_t* d, std::size_t s) {
        const std::size_t f = free();
        if (s >= f) {
            Put->put(d, f);
            NextGet = Put;
            Put = NextPut;
            return f;
        }
        Put->put(d, s);
        return s;
    }

    /*
     * Attempt to read 's' bytes into 'd'. Returns actual bytes read
     */
    std::size_t get(uint8_t* d, std::size_t s) {
        const std::size_t a = avail();
        if (s >= a) {
            Get->get(d, a);
            Get->reset();
            NextPut = Get;
            Get = NextGet;
            return a;
        }
        Get->get(d, s);
        return s;
    }

    /*
     * Attempt to skip 's' bytes. Returns actual bytes skipped
     */
    inline std::size_t skip(std::size_t s) {
        const std::size_t a = avail();
        if (s >= a) {
            Get->skip(a);
            Get->reset();
            NextPut = Get;
            Get = NextGet;
            return a;
        }
        Get->skip(s);
        return s;
    }

    /*
     * Returns how many bytes are available for a single read
     */
    inline std::size_t avail() const {
        return Get->avail();
    }

    /*
     * Returns how many bytes can be written at most
     */
    inline std::size_t free() const {
        return Put->free();
    }

    /*
     * Returns true if there are no bytes available for read
     */
    inline bool empty() const {
        return avail() == 0;
    }

    /*
     * Returns true if the buffer can't accept more bytes
     */
    inline bool full() const {
        return free() == 0;
    }

    /*
     * Returns true if there are any bytes to be read
     */
    inline bool have() const {
        return !empty();
    }

    /*
     * Returns true if the input is marked as consumed
     */
    inline bool consumed() const {
        return cons;
    }

    /*
     * Marks the input as consumed
     */
    inline void setConsumed() {
        cons = true;
    }

private:

    uint8_t* const lower;
    const uint8_t* const upper;
    internal::Partition A;
    internal::Partition B;
    internal::Partition* Get;
    internal::Partition* Put;
    internal::Partition* NextGet;
    internal::Partition* NextPut;
    bool cons;
}; // class BIP

#ifdef BIP_HAVE_THREAD

class LockedBIP {
public:
    /*
     * Construct a BIP buffer at memory block 'buf', with total size 's'
     */
    LockedBIP(uint8_t* buf, std::size_t s) : bip(buf, s) {
    }

    /*
     * Attempt to write 's' bytes from 'd'. Returns actual bytes written
     */
    std::size_t put(const uint8_t* d, std::size_t s) {
        std::unique_lock<std::mutex> guard(mutex);
        while (bip.full()) notFull.wait(guard);
        std::size_t r = bip.put(d, s);
        notEmpty.notify_one();
        return r;
    }

    /*
     * Attempt to read 's' bytes into 'd'. Returns actual bytes read
     */
    std::size_t get(uint8_t* d, std::size_t s) {
        std::unique_lock<std::mutex> guard(mutex);
        while (bip.empty() && !bip.consumed()) notEmpty.wait(guard);
        std::size_t r = bip.get(d, s);
        notFull.notify_one();
        return r;
    }

    /*
     * Attempt to skip 's' bytes. Returns actual bytes skipped
     */
    std::size_t skip(std::size_t s) {
        std::unique_lock<std::mutex> guard(mutex);
        while (bip.empty() && !bip.consumed()) notEmpty.wait(guard);
        std::size_t r = bip.skip(s);
        notFull.notify_one();
        return r;
    }

    /*
     * Block until all 's' bytes are written
     */
    std::size_t put_all(const uint8_t* d, std::size_t s) {
        std::unique_lock<std::mutex> guard(mutex);
        std::size_t all = 0;
        while (all < s) {
            while (bip.full()) notFull.wait(guard);
            all += bip.put(d + all, s - all);
        }
        notEmpty.notify_one();
        return all;
    }

    /*
     * Block until all 's' bytes are read, or the stream is consumed
     */
    std::size_t get_all(uint8_t* d, std::size_t s) {
        std::unique_lock<std::mutex> guard(mutex);
        std::size_t all = 0;
        while (all < s) {
            while (bip.empty() && !bip.consumed()) notEmpty.wait(guard);
            if (bip.consumed()) break;
            all += bip.get(d + all, s - all);
        }
        notFull.notify_one();
        return all;
    }

    /*
     * Block until all 's' bytes are skipped, or the stream is consumed
     */
    std::size_t skip_all(std::size_t s) {
        std::unique_lock<std::mutex> guard(mutex);

        while (bip.empty() && !bip.consumed()) notEmpty.wait(guard);
        std::size_t r = bip.skip(s);
        notFull.notify_one();

        return r;
    }

    /*
     * Returns how many bytes are available for a single read
     */
    inline std::size_t avail() const {
        std::unique_lock<std::mutex> guard(mutex);
        return bip.avail();
    }

    /*
     * Returns how many bytes can be written at most
     */
    inline std::size_t free() const {
        std::unique_lock<std::mutex> guard(mutex);
        return bip.free();
    }

    /*
     * Returns true if there are no bytes available for read
     */
    inline bool empty() const {
        std::unique_lock<std::mutex> guard(mutex);
        return bip.empty();
    }

    /*
     * Returns true if the buffer can't accept more bytes
     */
    inline bool full() const {
        std::unique_lock<std::mutex> guard(mutex);
        return bip.full();
    }

    /*
     * Returns true if there are any bytes to be read
     */
    inline bool have() const {
        std::unique_lock<std::mutex> guard(mutex);
        return bip.have();
    }

    /*
     * Returns true if the input is marked as consumed
     */
    inline bool consumed() const {
        std::unique_lock<std::mutex> guard(mutex);
        return bip.consumed();
    }

    /*
     * Marks the input as consumed
     */
    inline void setConsumed() {
        std::unique_lock<std::mutex> guard(mutex);
        bip.setConsumed();
        notEmpty.notify_all();
    }

private:
    BIP bip;
    mutable std::mutex mutex;
    std::condition_variable notFull;
    std::condition_variable notEmpty;

}; // class LockedBIP

#endif // BIP_HAVE_THREAD

} // namespace bip

namespace bip {
namespace internal {

Partition::Partition(uint8_t* const * l, const uint8_t* const * u) : lower(l), upper(u) {
    reset();
}

void Partition::put(const uint8_t* d, std::size_t s) {
    memcpy(end, d, s);
    end += s;
}

void Partition::get(uint8_t* d, std::size_t s) {
    memcpy(d, begin, s);
    begin += s;
}

void Partition::skip(std::size_t s) {
    begin += s;
}

std::size_t Partition::avail() const {
    return end - begin;
}

std::size_t Partition::free() const {
    return *upper - end;
}

void Partition::reset() {
    begin = end = *lower;
}

} // namespace internal
} // namespace bip

#endif // BIP_H_INCLUDED

