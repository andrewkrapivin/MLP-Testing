#include "BenchHelper.hpp"
#include <iostream>
#include <algorithm>
#include <stdlib.h>
#include <sys/mman.h>
#include <cassert>

using namespace std;

struct alignas(64) CacheLine {
    size_t data;
    size_t filler[7];
};

template <typename T> struct thp_allocator {
  constexpr static std::size_t huge_page_size = 1 << 21; // 2 MiB
  using value_type = T;

  thp_allocator() = default;
  template <class U>
  constexpr thp_allocator(const thp_allocator<U> &) noexcept {}

  T *allocate(std::size_t n) {
    if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
      throw std::bad_alloc();
    }
    void *p = nullptr;
    posix_memalign(&p, huge_page_size, n * sizeof(T));
    madvise(p, n * sizeof(T), MADV_HUGEPAGE);
    if (p == nullptr) {
      throw std::bad_alloc();
    }
    return static_cast<T *>(p);
  }

  void deallocate(T *p, std::size_t n) { std::free(p); }
};

void readSequential(vector<vector<size_t, thp_allocator<size_t>>>& arrays) {
    for(auto& array: arrays) {
        for(auto& x: array) {
            (void)x;
        }
    }
}

//Assuming all the arrays have the same size!!
void readRandom(vector<vector<size_t, thp_allocator<size_t>>>& arrays, size_t numSimultaneousChases = 1, bool usePrefetch = true) {
    assert(numSimultaneousChases <= arrays.size());
    vector<size_t> pointers(arrays.size()); //should be default init to zero
    for(size_t i{0}; i < arrays[0].size(); i++) {
        if(usePrefetch) {
            for(size_t j{0}; j < numSimultaneousChases; j++) {
                __builtin_prefetch(&arrays[j][pointers[j]]);
            }
        }
        for(size_t j{0}; j < numSimultaneousChases; j++) {
            pointers[j] = arrays[j][pointers[j]];
        }
    }
}

void readRandomCacheline(vector<vector<CacheLine, thp_allocator<CacheLine>>>& arrays, size_t numSimultaneousChases = 1, bool usePrefetch = true) {
    assert(numSimultaneousChases <= arrays.size());
    vector<CacheLine> pointers(arrays.size()); //should be default init to zero
    for(size_t i{0}; i < arrays[0].size(); i++) {
        if(usePrefetch) {
            for(size_t j{0}; j < numSimultaneousChases; j++) {
                __builtin_prefetch(&arrays[j][pointers[j].data]);
            }
        }
        for(size_t j{0}; j < numSimultaneousChases; j++) {
            pointers[j] = arrays[j][pointers[j].data];
        }
    }
}

int main(int argc, char** argv) {
    unsigned seed = chrono::steady_clock::now().time_since_epoch().count();
    mt19937 generator (seed);

    size_t testSize = 10;
    size_t numPointerChases = 1;
    if(argc >= 2)
        testSize = atoi(argv[1]);
    if(argc >= 3) 
        numPointerChases = atoi(argv[2]);

    vector<vector<size_t, thp_allocator<size_t>>> arrays(numPointerChases, vector<size_t, thp_allocator<size_t>>(testSize));
    vector<vector<CacheLine, thp_allocator<CacheLine>>> arraysFullCacheline(numPointerChases, vector<CacheLine, thp_allocator<CacheLine>>(testSize));
    vector<vector<size_t, thp_allocator<size_t>>> singlePointerChaseComparison(1, vector<size_t, thp_allocator<size_t>>(testSize));
    for(auto& array: arrays) {
        for(size_t i{0}; i < array.size(); i++) {
            array[i] = i;
        }
        shuffle(array.begin(), array.end(), generator);
    }

    for(auto& array: arraysFullCacheline) {
        for(size_t i{0}; i < array.size(); i++) {
            array[i].data = i;
        }
        shuffle(array.begin(), array.end(), generator);
    }
    
    for(size_t i{0}; i < singlePointerChaseComparison[0].size(); i++){ 
        singlePointerChaseComparison[0][i] = i;
    }
    shuffle(singlePointerChaseComparison[0].begin(), singlePointerChaseComparison[0].end(), generator);

    BenchHelper bench;

    bench.timeFunction([&] ()-> void {
        readSequential(arrays);
    }, "sequential read time");

    for(size_t i{1}; i <= numPointerChases; i++) {
        bench.timeFunction([&] () -> void {
            readRandom(arrays, i);
        }, "random read time with " + to_string(i) + " simultaneous pointer chases");
    }

    for(size_t i{1}; i <= numPointerChases; i++) {
        bench.timeFunction([&] () -> void {
            readRandom(arrays, i, false);
        }, "random read time without prefetch with " + to_string(i) + " simultaneous pointer chases");
    }

    for(size_t i{1}; i <= numPointerChases; i++) {
        bench.timeFunction([&] () -> void {
            readRandomCacheline(arraysFullCacheline, i);
        }, "full cacheline random read time with " + to_string(i) + " simultaneous pointer chases");
    }

    for(size_t i{1}; i <= numPointerChases; i++) {
        bench.timeFunction([&] () -> void {
            readRandomCacheline(arraysFullCacheline, i, false);
        }, "full cacheline random read time without prefetch with " + to_string(i) + " simultaneous pointer chases");
    }

    bench.timeFunction([&] () -> void {
        readRandom(singlePointerChaseComparison, 1, false);
    }, "random read time using a single pointer chase");

}
