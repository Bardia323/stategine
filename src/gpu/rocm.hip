// Stategine - the algebra's batches on an AMD device (ROCm, HIP): the CUDA
// kernel, as HIP.
#include <hip/hip_runtime.h>

#include <algorithm>

#define SG_DEVICE __device__
#define SG_GLOBAL __global__
#define SG_THREAD_INDEX (blockIdx.x * blockDim.x + threadIdx.x)
#include "job.inl"

#define SG_RT(x) hip##x
#define SG_NS rocm
#include "device.inl"
