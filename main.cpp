#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>

#include "lock_free.hpp"

const int MAX_THREADS = 100;
const int ITER_CNT = 2000;

void bench_one(std::ofstream& out_file) {
  LockFreeStack<int> stack;
  for (int i = 1; i <= MAX_THREADS; i++) {
    std::vector<std::thread> threads;
    std::vector<std::chrono::steady_clock::duration> times(i);
    threads.reserve(i);
    for (int j = 0; j < i; j++) {
      threads.emplace_back([&stack, &times, j](){
        for (int i = 0; i < ITER_CNT; i++) {
          auto time_before = std::chrono::steady_clock::now();
          stack.push(i);
          times[j] += std::chrono::steady_clock::now() - time_before;
        }
        times[j] /= ITER_CNT;
      });
    }
    std::chrono::steady_clock::duration sum;
    for (int j = 0; j < i; j++) {
      threads[j].join();
      if (j == 0) {
        sum = times[j];
      } else {
        sum += times[j];
      }
    }
    sum /= i;
    out_file << sum.count() << " ";
  }
  out_file << std::endl;
}

void bench_two(std::ofstream& out_file) {
  LockFreeStack<int> stack;
  for (int i = 1; i <= MAX_THREADS; i++) {
    std::vector<std::thread> threads;
    std::vector<std::chrono::steady_clock::duration> times(i);
    threads.reserve(i);
    for (int j = 0; j < i; j++) {
      threads.emplace_back([&stack, &times, j](){
        for (int i = 0; i < ITER_CNT; i++) {
          auto time_before = std::chrono::steady_clock::now();
          stack.push(i);
          times[j] += std::chrono::steady_clock::now() - time_before;
        }
        for (int i = 0; i < ITER_CNT; i++) {
          auto time_before = std::chrono::steady_clock::now();
          stack.pop();
          times[j] += std::chrono::steady_clock::now() - time_before;
        }
        times[j] /= 2 * ITER_CNT;
      });
    }
    std::chrono::steady_clock::duration sum;
    for (int j = 0; j < i; j++) {
      threads[j].join();
      if (j == 0) {
        sum = times[j];
      } else {
        sum += times[j];
      }
    }
    sum /= i;
    out_file << sum.count() << " ";
  }
  out_file << std::endl;
}

void bench_three(std::ofstream& out_file) {
  LockFreeStack<int> stack;
  for (int i = 1; i <= MAX_THREADS; i++) {
    std::vector<std::thread> threads;
    std::vector<std::chrono::steady_clock::duration> times(i);
    threads.reserve(i);
    for (int j = 0; j < i; j++) {
      threads.emplace_back([&stack, &times, j](){
        for (int i = 0; i < ITER_CNT; i++) {
          auto time_before = std::chrono::steady_clock::now();
          if (rand() % 2)
            stack.push(i);
          else
            stack.pop();
          times[j] += std::chrono::steady_clock::now() - time_before;
        }
        times[j] /= ITER_CNT;
      });
    }
    std::chrono::steady_clock::duration sum;
    for (int j = 0; j < i; j++) {
      threads[j].join();
      if (j == 0) {
        sum = times[j];
      } else {
        sum += times[j];
      }
    }
    sum /= i;
    out_file << sum.count() << " ";
  }
  out_file << std::endl;
}

int main() {
  std::ofstream out_file("bench.txt");
  bench_one(out_file);
  bench_two(out_file);
  bench_three(out_file);
}