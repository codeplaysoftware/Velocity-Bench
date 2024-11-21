/*
  This file is part of Leela Chess Zero.
  Copyright (C) 2018-2019 The LCZero Authors

  Leela Chess is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Leela Chess is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Leela Chess.  If not, see <http://www.gnu.org/licenses/>.

  Additional permission under GNU GPL version 3 section 7

  If you modify this Program, or any covered work, by linking or
  combining it with NVIDIA Corporation's libraries from the NVIDIA CUDA
  Toolkit and the NVIDIA CUDA Deep Neural Network library (or a
  modified version of those libraries), containing parts covered by the
  terms of the respective license agreement, the licensors of this
  Program grant you additional permission to convey the resulting work.
*/

/*   This file is part of Leela Chess Zero.
    Modifications Copyright (C) 2023 Intel Corporation

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>. 
   
   SPDX license identifier to be GNU General Public License v3.0 or later
*/


#pragma once

#include <hipblas.h>
#include <hip/hip_fp16.h>
#include <hip/hip_runtime.h>

#include "utils/exception.h"

#ifdef USE_CUDNN
#include <hipDNN.h>
#else
typedef void* hipdnnHandle_t;
#endif

#if CUBLAS_VER_MAJOR < 11
#define CUBLAS_PEDANTIC_MATH CUBLAS_DEFAULT_MATH
#endif

namespace lczero {
namespace cudnn_backend {

static constexpr int kNumOutputPolicy = 1858;

// max supported filter count for fast path
// TODO: extend it to cover bigger networks!
// (We are limited by no of registers per thread)
static constexpr int kMaxResBlockFusingChannels = 384;  // limit on num_filters
static constexpr int kMaxResBlockFusingSeKFp16Ampere =
    512;  // (use a different kernel with reduced register pressure)
static constexpr int kMaxResBlockFusingSeK =
    128;  // limit on (num_filters / se_ratio)
static constexpr int kMaxResBlockFusingSeFp16AmpereSmem =
    72 * kMaxResBlockFusingSeKFp16Ampere *
    sizeof(half);  // shared memory used by the special
                   // kernel

#ifdef USE_CUDNN
void CudnnError(hipdnnStatus_t status, const char* file, const int& line);
#endif
void CublasError(hipblasStatus_t status, const char* file, const int& line);
void CudaError(hipError_t status, const char* file, const int& line);

#ifdef USE_CUDNN
#define ReportCUDNNErrors(status) CudnnError(status, __FILE__, __LINE__)
#endif
#define ReportCUBLASErrors(status) CublasError(status, __FILE__, __LINE__)
#define ReportCUDAErrors(status) CudaError(status, __FILE__, __LINE__)

inline int DivUp(int a, int b) { return (a + b - 1) / b; }

enum ActivationFunction { NONE, RELU, TANH, SIGMOID, SELU, MISH };

}  // namespace cudnn_backend
}  // namespace lczero
