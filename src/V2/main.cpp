#include <iostream>
#include <algorithm>
#include <stdlib.h>
#include <sys/mman.h>
#include <cassert>
#include <random>
#include <chrono>
#include <cstring>
#include <immintrin.h>

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

//We don't want to use up memory bandwidth finding the addresses for where to write random stuff. Therefore, we have a small array in L1 or L2 that is read and reread. Not sure this works
class SmallRandomizer {
    static constexpr size_t NumOffsets = 1ull << 12;
    size_t OffsetRange;
    size_t offsets[NumOffsets];

    public:
        SmallRandomizer(size_t N) {
            assert(N % NumOffsets == 0);
            OffsetRange = N / NumOffsets;
            unsigned seed = chrono::steady_clock::now().time_since_epoch().count();
            mt19937 generator (seed);
            uniform_int_distribution<size_t> dist(0, OffsetRange-1);
            for(size_t i =0; i < NumOffsets; i++) {
                offsets[i] = dist(generator);
            }
        }

        size_t getRandomized(size_t i) {
            size_t offsetIndex = i & (NumOffsets - 1);
            size_t start = offsetIndex * OffsetRange;
            size_t naturalOffset = i / NumOffsets;
            size_t realOffset = (offsets[offsetIndex] + naturalOffset) % OffsetRange;
            return start + realOffset;
        }
};

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
    CacheLine* mem = static_cast<CacheLine*>(allocPages(memSize, useHugePages));\

    SmallRandomizer rando(N);

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
            // CacheLine x(0);
            __m512i x{0, 0, 0, 0, 0, 0, 0, 0};
            __m512i off{1, 0, 0, 0, 0, 0, 0, 0};
            for(size_t i = 0; i < N; i++){
                // x.data++;
                x = _mm512_add_epi64(x, off);
                // mem[i] = 
                _mm512_stream_si512((__m512i*)&mem[i], x);
            }
        });
    double linearFullWriteBandwidthGBs = memSize / (1000'000'000.0 * linearFullWriteTime);
    cout << "Linear full write bandwidth per second: " << linearFullWriteBandwidthGBs << " GB/s" << endl;

    double randomFullWriteTime = timeFunctionS([mem, N, &rando] () {
            for(size_t i = 0; i < N; i++){
                mem[rando.getRandomized(i)] = CacheLine(i);
            }
        });
    double randomFullWriteBandwidthGBs = memSize / (1000'000'000.0 * randomFullWriteTime);
    cout << "Linear full cacheline random write bandwidth per second: " << randomFullWriteBandwidthGBs << " GB/s" << endl;

    // double linearBlockWriteTime = timeFunctionS([mem, N] () {
    //         constexpr size_t B = 16;
    //         CacheLine buff[B];
    //         for (size_t i = 0; i < N; i+=B)
    //         {
    //             for(size_t j = 0; j < B; j++){
    //                 buff[j].data = i+j;
    //             }
    //             memcpy(&mem[i], buff, CacheLineSize*B);
    //         }
    //     });
    // double linearBlockWriteBandwidthGBs = memSize / (1000'000'000.0 * linearBlockWriteTime);
    // cout << "Linear blocked write bandwidth per second: " << linearBlockWriteBandwidthGBs << " GB/s" << endl;

    shuffle(mem, mem+N, generator);
    
    double linearReadTime = timeFunctionS([mem, N] () {size_t sum = 0; 
            for(size_t i = 0; i < N; i++){
                sum+=mem[i].data;
            }
            cout << sum << endl;
        });
    double linearReadBandwidthGBs = memSize / (1000'000'000.0 * linearReadTime);
    cout << "Linear read bandwidth per second: " << linearReadBandwidthGBs << " GB/s" << endl;

    double randomReadTime = timeFunctionS([mem, N, &rando] () {size_t sum = 0; 
            for(size_t i = 0; i < N; i++){
                sum+=mem[rando.getRandomized(i)].data;
            }
            cout << sum << endl;
        });
    double randomReadBandwidthGBs = memSize / (1000'000'000.0 * randomReadTime);
    cout << "Random read bandwidth per second: " << randomReadBandwidthGBs << " GB/s" << endl;
}