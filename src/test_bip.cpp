#include <vector>
#include <random>
#include <algorithm>
#include <iterator>
#include <limits>
#include <array>
#include <thread>
#include <functional>
#include <iostream>
#include <mutex>
#include <condition_variable>

#include "Bip.h"

using elem_type = char;
constexpr size_t buf_size = 200;
constexpr size_t data_size = 5000;
constexpr size_t min_produce_len = 10;
constexpr size_t max_produce_len = 500;
constexpr size_t min_consume_len = 10;
constexpr size_t max_consume_len = 500;

class bip_threading {
public:

	std::unique_lock<std::mutex> lock() {
		return std::unique_lock<std::mutex>{m_mutex};
	}

	void wait_not_full(std::unique_lock<std::mutex>& lock) {
		while (m_full) {
			m_condition.wait(lock);
		}
	}

	void wait_not_empty(std::unique_lock<std::mutex>& lock) {
		while (m_empty) {
			m_condition.wait(lock);
		}
	}

	void signal_not_full() {
		m_full = false;
		m_condition.notify_all();
	}

	void signal_not_empty() {
		m_empty = false;
		m_condition.notify_all();
	}

private:
	std::mutex m_mutex;
	std::condition_variable m_condition;
	bool m_empty = false;
	bool m_full = false;
};

static std::default_random_engine& random_engine() {
	static std::default_random_engine engine;
	return engine;
}

static void produce(bip::BIP<elem_type>& bip, const std::vector<elem_type>& in_data, bip_threading& threading) {
	std::uniform_int_distribution<size_t> dist {min_produce_len, max_produce_len};
	size_t left = in_data.size();
	while (left > 0) {
		auto size = std::min(dist(random_engine()), left);
		size_t written = 0;
		{
			auto lock = threading.lock();
			threading.wait_not_full(lock);
			written = bip.put(in_data.data() + in_data.size() - left, size);
			threading.signal_not_empty();
		}
		left -= written;
	}
}

static void consume(bip::BIP<elem_type>& bip, std::vector<elem_type>& out_data, size_t total, bip_threading& threading) {
	std::uniform_int_distribution<size_t> dist {min_consume_len, max_consume_len};
	elem_type buf[max_consume_len];
	size_t left = total;
	while (left > 0) {
		auto size = std::min(dist(random_engine()), left);
		size_t read = 0;
		{
			auto lock = threading.lock();
			threading.wait_not_empty(lock);
			read = bip.get(buf, size);
			threading.signal_not_full();
		}
		left -= read;
		out_data.insert(std::end(out_data), buf, buf + read);
	}
}

static std::vector<elem_type> generate(size_t count) {

	std::uniform_int_distribution<elem_type> dist {
		std::numeric_limits<elem_type>::min(),
        std::numeric_limits<elem_type>::max()};

	std::vector<elem_type> out_data(count);
	std::generate(std::begin(out_data), std::end(out_data), [&dist](){
		return dist(random_engine());
	});
	return out_data;
}

int main(int, elem_type**) {


	std::array<elem_type, buf_size> buf;

	auto in_data = generate(data_size);

	std::vector<elem_type> out_data;

	bip::BIP<elem_type> bip{buf.data(), buf.size()};

	bip_threading threading;

	std::thread consume_thr(consume, std::ref(bip), std::ref(out_data), in_data.size(), std::ref(threading));

	produce(bip, in_data, threading);

	consume_thr.join();

	std::cout << "Input elements count: " << in_data.size() << std::endl;
	std::cout << "Output elements count: " << out_data.size() << std::endl;

	if (in_data.size() != out_data.size()) {
		std::cerr << "Element count mismatch." << std::endl;
		return 1;
	}

	for (size_t i = 0; i < in_data.size(); ++i) {
		if (in_data[i] != out_data[i]) {
			std::cerr << "Element mismatch at position " << i << std::endl;
			return 1;
		}
	}

	std::cout << "Success" << std::endl;
	return 0;
}
