#include "BenchHelper.hpp"
#include <iostream>
#include <algorithm>

using namespace std;

void readSequential(vector<vector<size_t>>& arrays) {
    for(auto& array: arrays) {
        for(auto& x: array) {
            (void)x;
        }
    }
}

//Assuming all the arrays have the same size!!
void readRandom(vector<vector<size_t>>& arrays, bool usePrefetch = true) {
    vector<size_t> pointers(arrays.size()); //should be default init to zero
    for(size_t i{0}; i < arrays[0].size(); i++) {
        if(usePrefetch) {
            for(size_t j{0}; j < arrays.size(); j++) {
                __builtin_prefetch(&arrays[j][pointers[j]]);
            }
        }
        for(size_t j{0}; j < arrays.size(); j++) {
            pointers[j] = arrays[j][pointers[j]];
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

    vector<vector<size_t>> arrays(numPointerChases, vector<size_t>(testSize));
    vector<vector<size_t>> singlePointerChaseComparison(1, vector<size_t>(numPointerChases*testSize));
    for(auto& array: arrays) {
        for(size_t i{0}; i < array.size(); i++) {
            array[i] = i;
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

    bench.timeFunction([&] () -> void {
        readRandom(arrays);
    }, "random read time");

    bench.timeFunction([&] () -> void {
        readRandom(arrays, false);
    }, "random read time without prefetch");

    bench.timeFunction([&] () -> void {
        readRandom(singlePointerChaseComparison, false);
    }, "random read time using a single pointer chase");

}