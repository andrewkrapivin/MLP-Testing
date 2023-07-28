# MLP-Testing

This is a really simple program to test the memory level parallelism of a computer. It essentially does N simultaneous pointer chases for different values of N.
On one extreme, when N=1, this measures latency bound memory bandwidth.
On the other extreme, for large N, this measures random memory bandwidth that is not latency constrained (due to prefetching).
