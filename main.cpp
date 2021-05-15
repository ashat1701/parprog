#include <iostream>
#include <thread>
#include <vector>
#include <immintrin.h>
#include <random>
#include <chrono>
#include <fstream>
#include <functional>

using namespace std::chrono_literals;
const int MAGIC = 13; // maybe it should be power of 2 :-| (check would be easier)
const int MAX_THREADS = 10;
const int ITER_CNT = 200;
// Simple spinlock interface
class Spinlock {
 public:
  virtual void lock() = 0;
  virtual void unlock() = 0;
  virtual bool try_lock() = 0;
};

//TAS with yield, pause, and backoff
class TASpinlock : public Spinlock {
 public:
  TASpinlock() : lock_(0) {}

  void lock() override {
    auto wait_dur = 1us;
    for (int spin_count = 0; lock_.exchange(1, std::memory_order_acquire); spin_count++) {
      if (spin_count == MAGIC) {
        std::this_thread::yield();
      } else {
        if (spin_count == MAGIC * 2) {
          wait_dur *= 2;
          std::this_thread::sleep_for(wait_dur);
          spin_count = 0;
        } else {
          _mm_pause();
        }
      }
    }
  }

  bool try_lock() override {
    return !lock_.exchange(1, std::memory_order_acquire);
  }

  void unlock() override  {
    lock_.store(0, std::memory_order_release);
  }
 private:
  std::atomic<unsigned int> lock_;
};

//TTAS with yield, pause and backoff
class TTASpinlock: public Spinlock {
 public:
  TTASpinlock() : lock_(0) {}

  void lock() override {
    auto wait_dur = 1us;
    for (; lock_.exchange(1, std::memory_order_acquire);) {
      for (int spin_count = 0; lock_.load(std::memory_order_relaxed); spin_count++) {
        if (spin_count == MAGIC) {
          std::this_thread::yield();
        } else {
          if (spin_count == 2 * MAGIC) {
            wait_dur *= 2;
            std::this_thread::sleep_for(wait_dur);
          }
          _mm_pause();
        }
      }
    }
  }

  bool try_lock() override {
    return !lock_.load(std::memory_order_relaxed) &&
        !lock_.exchange(1, std::memory_order_acquire);
  }

  void unlock() override {
    lock_.store(0, std::memory_order_release);
  }
 private:
  std::atomic<unsigned int> lock_;
};

class TicketSpinlock : public Spinlock{
 public:
  TicketSpinlock() : in_(0), out_(0) {}
  void lock() override {
    auto wait_dur = 1us;
    auto cur_ticket = in_.fetch_add(1, std::memory_order_relaxed);
    for (int spin_count = 0; out_.load(std::memory_order_acquire) != cur_ticket; spin_count++) {
      if (spin_count == MAGIC) {
        std::this_thread::yield();
      } else
        if (spin_count == MAGIC * 2) {
          wait_dur *= 2;
          std::this_thread::sleep_for(wait_dur);
          spin_count = 0;
        } else {
          _mm_pause();
        }
    }
  }

  void unlock() override {
    // If we use here fetch_add it would be slower ((
    out_.store(out_.load(std::memory_order_relaxed) + 1, std::memory_order_release);
  }
  // I dont know how to implement try_lock there (maybe we should fetch_add(-1) after unsuccessful try)
  bool try_lock() override {
    std::abort();
  }
 private:
  std::atomic<unsigned int> in_, out_;
};

// Simple benchmark where threads are contenting on one mutex
// We measure longest_wait to check if there starvation
auto bench1(Spinlock& spin) {
  auto longest_wait = 0ns;
  for (int i = 0; i < ITER_CNT; i++) {
    auto time_before = std::chrono::high_resolution_clock::now();
    spin.lock();
    auto wait_time = std::chrono::high_resolution_clock::now() - time_before;
    longest_wait = std::max(wait_time, longest_wait);
    spin.unlock();
  }
  return longest_wait;
}

int Rand() {
  static std::random_device rd;
  static std::mt19937 mt(rd());
  static std::uniform_int_distribution<int> dist(1, 100);
  return dist(mt);
}

// Lets try to aquire mutex in different time
auto bench2(Spinlock& spin) {
  auto longest_wait = 0ns;
  for (int i = 0; i < ITER_CNT; i++) {
    std::this_thread::sleep_for(1ms * (Rand()));
    auto time_before = std::chrono::high_resolution_clock::now();
    spin.lock();
    auto wait_time = std::chrono::high_resolution_clock::now() - time_before;
    longest_wait = std::max(wait_time, longest_wait);
    spin.unlock();
  }
  return longest_wait;
}

// Let's do or one very hard job, or one light randomly
auto bench3(Spinlock& spin) {
  for (int i = 0; i < ITER_CNT; i++) {
    spin.lock();
    if (Rand() % 2) {
      std::this_thread::sleep_for(100ms);
    }
    spin.unlock();
  }
  return 1ns;
}

void do_bench(const std::function<decltype(1ns)(Spinlock&)>& bench, Spinlock& spin, std::ofstream& file) {
  for (int threads = 1; threads < MAX_THREADS; threads++) {
    std::vector<std::thread> thr;
    std::vector<decltype(1ns)> b1(threads);
    auto time_before = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < threads; i++) {
      thr.emplace_back([&spin, &b1, i, &bench](){
        b1[i] = bench(spin);
      });
    }
    for (int i = 0; i < threads; i++) {
      thr[i].join();
    }
    auto time_after = std::chrono::high_resolution_clock::now();
    file << (time_after - time_before).count() << " ";
    for (auto&& it : b1) {
      file << it.count() << " ";
    }
    file << std::endl;
  }
}

void doBenchSeries(Spinlock& spin, const std::string& filename) {
  std::ofstream out(filename);
  do_bench(bench1, spin, out);
  std::cerr << "bench1 completed" << std::endl;
  do_bench(bench2, spin, out);
  do_bench(bench3, spin, out);
  out.close();
}

int main() {
  TASpinlock tas;
  TTASpinlock ttas;
  TicketSpinlock ticket;
  //doBenchSeries(tas, "tas.txt");
  //doBenchSeries(ttas, "ttas.txt");
  doBenchSeries(ticket, "ticket.txt");
}