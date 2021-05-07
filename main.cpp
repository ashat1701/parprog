#include <iostream>
#include <thread>
#include <vector>

//#define TEST
#define BENCH

#define PARTITION_FACTOR 4
#define array(matrix, i, j) matrix.arr[(int) matrix.m * (int) i + (int) j]

const auto N = 1024, M = 1024, K = 1024;
// Struct for matrix
struct Matrix {
  int *arr;
  int n, m;
  Matrix(int n, int m) : n(n), m(m), arr(new int[n * m]) {}
  // Fill with test data
  void fill() const {
    for (int i = 0; i < n; i++) {
      for (int j = 0; j < m; j++) {
        arr[m * i + j] = i + j;
      }
    }
  }
  ~Matrix() {
    delete [] arr;
  }
};

struct ThreadInfo {
  int id;
  int thread_count;
  const Matrix &a, &b;
  Matrix &result;
  ThreadInfo(int id, int thread_count, const Matrix& a, const Matrix& b, Matrix& result) :
    id(id), thread_count(thread_count), a(a), b(b), result(result) {}
};

/* There are optimization for last cycle with avx/sse instructions:
 *
    int new_m = m >> 3;
    __m256 ymm0, ymm1, ymm2, ymm3;
    ymm0 = _mm256_set1_ps(0);
    for (int i = 0; i < new_m; i++) {
        ymm1 = _mm256_loadu_ps(a + (i << 3));
        ymm2 = _mm256_loadu_ps(b + (i << 3));
        ymm3 = _mm256_dp_ps(ymm1, ymm2, 0b11110001);
        ymm0 = _mm256_add_ps(ymm0, ymm3);
    }
    __m128 xmm1 = _mm256_extractf128_ps(ymm0, 1);
    __m128 xmm0 = _mm256_extractf128_ps(ymm0, 0);
    xmm0 = _mm_add_ps(xmm0, xmm1);
    float ans = _mm_cvtss_f32(xmm0);
    int rem = m & 7;
    int offset = new_m << 3;

    for (int j = 0; j < rem; j++)
        ans += a[j + offset] * b[j + offset];
  We can speed up even faster when use such type of 'kernel'
*/
inline void multiplyBlock(const Matrix& a, const Matrix& b, Matrix& result,
                          int i_start, int i_fin,
                          int j_start, int j_fin,
                          int k_start, int k_fin) {
  for (int i = i_start; i < i_fin; i++) {
    for (int j = j_start; j < j_fin; j++) {
      for (int k = k_start; k < k_fin; k++) {
        array(result, i, j) += array(a, i, k) * array(b, k, j);
      }
    }
  }
}

void ThreadWork(ThreadInfo info) {
  int tile_width = info.result.m / PARTITION_FACTOR;
  for (int ii = 0; ii < PARTITION_FACTOR; ii++) {
    for (int jj = 0; jj < PARTITION_FACTOR; jj++) {
      int left_edge = info.id * (tile_width / info.thread_count) + jj * tile_width;
      int right_edge = (info.id + 1) * (tile_width / info.thread_count) + jj * tile_width;
      for (int kk = 0; kk < PARTITION_FACTOR; kk++) {
        multiplyBlock(info.a, info.b, info.result, ii * tile_width,
                      (ii + 1) * tile_width, left_edge, right_edge, kk * tile_width, (kk + 1) * tile_width);
      }
    }
  }
}

void stupidMultiplication(const Matrix& a, const Matrix& b, Matrix& result) {
  for (int i = 0; i < a.n; i++) {
    for (int j = 0; j < b.m; j++) {
      for (int k = 0; k < a.m; k++) {
        array(result, i, j) += array(a, i, k) * array(b, k, j);
      }
    }
  }
}

void fastMultiplication(const Matrix& a, const Matrix& b, Matrix& result, int thread_count) {
  std::vector<std::thread> working_threads;
  working_threads.reserve(thread_count);
  for (int i = 0; i < thread_count; i++) {
     working_threads.emplace_back([thread_count, &a, &b, &result, i]() {
      ThreadWork(ThreadInfo(i, thread_count, a, b, result));
    });
  }
  for (int i = 0; i < thread_count; i++) {
    working_threads[i].join();
  }
}

bool checkCorrect(const Matrix& a, const Matrix& b, int thread_count) {
  Matrix result_stupid(a.n, b.m);
  Matrix result_fast(a.n, b.m);
  stupidMultiplication(a, b, result_stupid);
  fastMultiplication(a, b, result_fast, thread_count);
  for (int i = 0; i < result_fast.n; i++) {
    for (int j = 0; j < result_fast.m; j++) {
      if (array(result_fast, i, j) != array(result_stupid, i, j)) {
        return false;
      }
    }
  }
  return true;
}

int main(int argc, char ** argv) {
  if (argc < 2) {
    std::cerr << "Usage: mult threadCount" << std::endl;
    return 1;
  }
  auto thread_count = std::atoi(argv[1]);

  Matrix a(N, M);
  Matrix b(M, K);
  a.fill();
  b.fill();
  #ifdef TEST
  if (!checkCorrect(a, b, thread_count)) {
    std::cerr << "Fast multiplication incorrect" << std::endl;
    std::abort();
  }
  std::cout << "Fast multiplication correct" << std::endl;
  #endif
#ifdef BENCH
  for (int count = 1; count < 10; count++) {
    Matrix result(N, K);
    auto before = std::chrono::steady_clock::now();
    fastMultiplication(a, b, result, count);
    auto after = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed_seconds = after - before;
    std::cout << count << " " << (elapsed_seconds).count() << std::endl;
  }
#endif
  return 0;
}
