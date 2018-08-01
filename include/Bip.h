/*
 * Bi-partitioned circular buffer.
 */

#ifndef BIP_H_INCLUDED
#define BIP_H_INCLUDED

#include <cstdint>
#include <cstring>

namespace bip {

namespace internal {

template <typename T>
struct Partition {
    inline Partition(T* const* lower, const T* const* upper) noexcept;
    inline void put(const T* data, std::size_t size) noexcept;
    inline void get(T* data, std::size_t size) noexcept;
    inline void skip(std::size_t size) noexcept;
    inline std::size_t avail() const noexcept;
    inline std::size_t free() const noexcept;
    inline void reset() noexcept;
    T* begin;
    T* end;
private:
    T* const* m_lower;
    const T* const* m_upper;
}; // struct Partition

} // namespace internal

template <typename T>
class BIP {
public:
    /*
     * Construct a BIP buffer at memory block 'buf', with total elements count 'size'
     */
    BIP(T* buf, std::size_t size) noexcept;

    /*
     * Attempt to write 'size' elements from 'data'. Returns the count of actual elements written
     */
    std::size_t put(const T* data, std::size_t size) noexcept;

    /*
     * Attempt to read 'size' elements into 'data'. Returns the count of actual elements read
     */
    std::size_t get(T* data, std::size_t size) noexcept;

    /*
     * Attempt to skip 'size' elements. Returns the count of actual elements skipped
     */
    inline std::size_t skip(std::size_t size) noexcept;

    /*
     * Returns how many elements are available for a single read
     */
    inline std::size_t avail() const noexcept;

    /*
     * Returns how many elements can be written in a single write
     */
    inline std::size_t free() const noexcept;

    /*
     * Returns true if there are no elements available for read
     */
    inline bool empty() const noexcept;

    /*
     * Returns true if the buffer can't accept more elements
     */
    inline bool full() const noexcept;

    /*
     * Returns true if there are any elements to be read
     */
    inline bool have() const noexcept;

private:

    T* const lower;
    const T* const upper;
    internal::Partition<T> A;
    internal::Partition<T> B;
    internal::Partition<T>* Get;
    internal::Partition<T>* Put;
    internal::Partition<T>* NextGet;
    internal::Partition<T>* NextPut;
}; // class BIP

} // namespace bip

namespace bip {

template <typename T>
BIP<T>::BIP(T* buf, std::size_t size) noexcept :
		lower{buf},
		upper{buf + size},
		A{&lower, &B.begin},
		B{&A.end, &upper},
		Get{&B},
		Put{&B},
		NextGet{&A},
		NextPut{&A} {
}

template <typename T>
std::size_t BIP<T>::put(const T* data, std::size_t size) noexcept {
	const auto f = free();
	if (size >= f) {
		Put->put(data, f);
		NextGet = Put;
		Put = NextPut;
		return f;
	}
	Put->put(data, size);
	return size;
}

template <typename T>
std::size_t BIP<T>::get(T* data, std::size_t size) noexcept {
	const auto a = avail();
	if (size >= a) {
		Get->get(data, a);
		Get->reset();
		NextPut = Get;
		Get = NextGet;
		return a;
	}
	Get->get(data, size);
	return size;
}

template <typename T>
std::size_t BIP<T>::skip(std::size_t size) noexcept {
	const auto a = avail();
	if (size >= a) {
		Get->skip(a);
		Get->reset();
		NextPut = Get;
		Get = NextGet;
		return a;
	}
	Get->skip(size);
	return size;
}

template <typename T>
std::size_t BIP<T>::avail() const noexcept {
	return Get->avail();
}

template <typename T>
std::size_t BIP<T>::free() const noexcept {
	return Put->free();
}

template <typename T>
bool BIP<T>::empty() const noexcept {
	return avail() == 0;
}

template <typename T>
bool BIP<T>::full() const noexcept {
	return free() == 0;
}

template <typename T>
bool BIP<T>::have() const noexcept {
	return !empty();
}

namespace internal {

template <typename T>
Partition<T>::Partition(T* const* lower, const T* const* upper) noexcept :
		begin{},
		end{},
		m_lower{lower},
		m_upper{upper} {
    reset();
}

template <typename T>
void Partition<T>::put(const T* data, std::size_t size) noexcept {
    memcpy(end, data, size * sizeof(T));
    end += size;
}

template <typename T>
void Partition<T>::get(T* data, std::size_t size) noexcept {
    memcpy(data, begin, size * sizeof(T));
    begin += size;
}

template <typename T>
void Partition<T>::skip(std::size_t size) noexcept {
    begin += size;
}

template <typename T>
std::size_t Partition<T>::avail() const noexcept {
    return end - begin;
}

template <typename T>
std::size_t Partition<T>::free() const noexcept {
    return *m_upper - end;
}

template <typename T>
void Partition<T>::reset() noexcept {
    begin = end = *m_lower;
}

} // namespace internal
} // namespace bip

#endif // BIP_H_INCLUDED
