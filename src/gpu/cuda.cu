// Stategine - the algebra's batches on an NVIDIA device (CUDA).
#include <cuda_runtime.h>

#include <algorithm>

#define SG_DEVICE __device__
#define SG_GLOBAL __global__
#define SG_THREAD_INDEX (blockIdx.x * blockDim.x + threadIdx.x)
#include "job.inl"

#define SG_RT(x) cuda##x
#define SG_NS cuda
#include "device.inl"
