#include <iostream>
#include <algorithm>
#include <stdlib.h>
#include <sys/mman.h>
#include <cassert>
#include <random>
#include <chrono>
#include <cstring>

using namespace std;

struct alignas(64) CacheLine {
    size_t data;
    size_t filler[7];

    CacheLine(size_t x = 0): data(x) {
    }
};

constexpr static std::size_t CacheLineSize = 1 << 6; // 64 B
static_assert(sizeof(CacheLine) == CacheLineSize);

constexpr static std::size_t HugePageSize = 1 << 21; // 2 MiB
constexpr static std::size_t NormalPageSize = 1 << 12; // 4 KiB

double timeFunctionS(function<void()> F) {
    auto start = chrono::high_resolution_clock::now();
    F();
    auto end = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::microseconds>(end-start);
    double t = ((double)duration.count())/1000'000.0;
    return t;
}

void* allocPages(size_t n, bool useHugePages) {
    size_t curPageSize = useHugePages ? HugePageSize : NormalPageSize;
    void* p = aligned_alloc(curPageSize, n);
    if (useHugePages) {
        madvise(p, n, MADV_HUGEPAGE);
    }
    if (p == nullptr) {
      throw std::bad_alloc();
    }
    return p;
}

int main(int argc, char** argv) {
    size_t logMemSize = 31;
    size_t maxPointerChases = 32;
    bool useHugePages = false;
    if(argc >= 2)
        logMemSize = atoi(argv[1]);
    if(argc >= 3) 
        maxPointerChases = atoi(argv[2]);
    if(argc >= 4)
        useHugePages = atoi(argv[3]);

    size_t aliasedMemSize = 1ull << logMemSize;
    size_t curPageSize = useHugePages ? HugePageSize : NormalPageSize;
    size_t numPages = aliasedMemSize / curPageSize;

    size_t memSize = numPages * curPageSize;
    size_t N = memSize / CacheLineSize;
    CacheLine* mem = static_cast<CacheLine*>(allocPages(memSize, useHugePages));

    // generate(mem, mem+N, [] () {static size_t count = 0; return CacheLine(count++);});
    unsigned seed = chrono::steady_clock::now().time_since_epoch().count();
    mt19937 generator (seed);

    double linearPartialWriteTime = timeFunctionS([mem, N] () {
            for(size_t i = 0; i < N; i++){
                mem[i].data = i;
            }
        });
    double linearPartialWriteBandwidthGBs = memSize / (1000'000'000.0 * linearPartialWriteTime);
    cout << "Linear partial write bandwidth per second: " << linearPartialWriteBandwidthGBs << " GB/s" << endl;

    double linearFullWriteTime = timeFunctionS([mem, N] () {
            for(size_t i = 0; i < N; i++){
                mem[i] = CacheLine(i);
            }
        });
    double linearFullWriteBandwidthGBs = memSize / (1000'000'000.0 * linearFullWriteTime);
    cout << "Linear full write bandwidth per second: " << linearFullWriteBandwidthGBs << " GB/s" << endl;

    double linearBlockWriteTime = timeFunctionS([mem, N] () {
            constexpr size_t B = 16;
            CacheLine buff[B];
            for (size_t i = 0; i < N; i+=B)
            {
                for(size_t j = 0; j < B; j++){
                    buff[j].data = i+j;
                }
                memcpy(&mem[i], buff, CacheLineSize*B);
            }
        });
    double linearBlockWriteBandwidthGBs = memSize / (1000'000'000.0 * linearBlockWriteTime);
    cout << "Linear blocked write bandwidth per second: " << linearBlockWriteBandwidthGBs << " GB/s" << endl;

    shuffle(mem, mem+N, generator);
    
    double linearReadTime = timeFunctionS([mem, N] () {size_t sum = 0; 
            for(size_t i = 0; i < N; i++){
                sum+=mem[i].data;
            }
            cout << sum << endl;
        });
    double linearReadBandwidthGBs = memSize / (1000'000'000.0 * linearReadTime);
    cout << "Linear read bandwidth per second: " << linearReadBandwidthGBs << " GB/s" << endl;
}