/* Copyright (C) 2023 Intel Corporation
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
 * OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
 * OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 * SPDX-License-Identifier: MIT
 */

#include <sycl/sycl.hpp>

#include <opencv2/opencv.hpp>

#include <chrono>
#include <iostream>

#include "common.hpp"
#include "Utilities.h"
#include "Logging.h"

#define TIMER_START(name) time_start_ ## name = std::chrono::steady_clock::now();
#define TIMER_END(name)                                               \
    time_total_ ## name += std::chrono::duration<double, std::milli>( \
    std::chrono::steady_clock::now() - time_start_ ## name).count();
#define TIMER_SUBTRACT(name1, name2) time_total_ ## name1 -= time_total_ ## name2;
#define TIMER_PRINT(name, msg) std::cout << msg <<": " << time_total_ ## name / 1e3 << " s\n";

#define START_TIMER() start_time = std::chrono::steady_clock::now();
#define STOP_TIMER()                                                                        \
    stop_time = std::chrono::steady_clock::now();                                           \
    duration  = std::chrono::duration<double, std::milli>(stop_time - start_time).count();  \
    tot_time += duration;
#define PRINT_TIMER(name) std::cout <<name <<"      :" << duration << " ms\n";

// #ifndef DEBUG_TIME
// #define DEBUG_TIME
// #endif

#undef CPP_MODULE
#define CPP_MODULE "SYMN"

#ifndef BLOCK_SIZE
#define BLOCK_SIZE 64
#endif

using namespace cv;
using namespace std;

double get_kernel_time(sycl::event& e) {
    cl_ulong time_start =
        e.get_profiling_info<sycl::info::event_profiling::command_start>();

    cl_ulong time_end =
        e.get_profiling_info<sycl::info::event_profiling::command_end>();

    double elapsed = (time_end - time_start)/1e6;
    return elapsed;
}

void computeGradient(
    unsigned char* input,
    unsigned char* output,
    int rows,
    int cols,
    sycl::nd_item<3> item)
{
    float gradientx[3][3] = {{-1.f,  0.f,  1.f}, {-2.f, 0.f, 2.f}, {-1.f, 0.f, 1.f}}; 
    float gradienty[3][3] = {{-1.f, -2.f, -1.f}, { 0.f, 0.f, 0.f}, { 1.f, 2.f, 1.f}};

    float gradient_x, gradient_y;

    int row = item.get_group(1) * item.get_local_range(1) + item.get_local_id(1);
    int col = item.get_group(2) * item.get_local_range(2) + item.get_local_id(2);
    if (row >= rows - 2 || col >= cols - 2) return;

    int index = row * cols + col + cols + 1;
    int index_row_above = index - cols;
    int index_row_below = index + cols;

    gradient_x =    (gradientx[0][0] * input[index_row_above - 1])    +
                    (gradientx[0][1] * input[index_row_above])        +
                    (gradientx[0][2] * input[index_row_above + 1])    +
                    (gradientx[1][0] * input[index - 1])              +
                    (gradientx[1][1] * input[index])                  +
                    (gradientx[1][2] * input[index + 1])              +
                    (gradientx[2][0] * input[index_row_below - 1])    +
                    (gradientx[2][1] * input[index_row_below])        +
                    (gradientx[2][2] * input[index_row_below + 1]);

    gradient_y =    (gradienty[0][0] * input[index_row_above - 1])    +
                    (gradienty[0][1] * input[index_row_above])        +
                    (gradienty[0][2] * input[index_row_above + 1])    +
                    (gradienty[1][0] * input[index - 1])              +
                    (gradienty[1][1] * input[index])                  +
                    (gradienty[1][2] * input[index + 1])              +
                    (gradienty[2][0] * input[index_row_below - 1])    +
                    (gradienty[2][1] * input[index_row_below])        +
                    (gradienty[2][2] * input[index_row_below + 1]);

    // output[index] = sycl::sqrt(sycl::pow<float>(gradient_x, 2.f) +
                            //    sycl::pow<float>(gradient_y, 2.f));
    output[index] = sycl::sqrt(gradient_x * gradient_x + gradient_y * gradient_y);
}

int main(int argc, const char* argv[])
{
    std::chrono::steady_clock::time_point time_start_init;
    std::chrono::steady_clock::time_point time_start_exec;
    std::chrono::steady_clock::time_point time_start_io;
    double time_total_init = 0.0;
    double time_total_exec = 0.0;
    double time_total_io = 0.0;

    TIMER_START(init)

    try {
    LOG("Welcome to the SYCL version of Sobel filter workload.");

    VelocityBench::CommandLineParser parser;
    InitializeCmdLineParser(parser);
    parser.Parse(argc, argv);

    // int iScaleFactor(parser.GetIntegerSetting("-f"));
    std::string inputfile = parser.GetSetting("-i");
    int nIterations = parser.GetIntegerSetting("-n");
    if(nIterations < 0 || nIterations > 100) {
        LOG_ERROR("# of iterations must be within range [1, 100]");
    }

    // TIMER_START(io)
    // LOG("Input image file: "<< inputfile);
    // Mat inputimage = imread(inputfile, IMREAD_COLOR);
    // if (inputimage.empty()) {
    //     LOG_ERROR("Failed to open input image\n");
    //     return -1;
    // }
    // TIMER_END(io)

    TIMER_START(io)
    LOG("Input image file: "<< inputfile);
    Mat scaledImage = imread(inputfile, IMREAD_GRAYSCALE);
    if (scaledImage.empty()) {
        LOG_ERROR("Failed to open input image\n");
        return -1;
    }
    TIMER_END(io)

    // LOG("Scaling image...");
    // Mat scaledImage = preprocess(inputimage, iScaleFactor);
    // LOG("Scaling image...done");
    // imwrite("../../res/silverfalls_32Kx32K.png", scaledImage);
    // std::cout << "\nsizes: " << inputimage.rows << " " << inputimage.cols << " " << scaledImage.rows << " "  << scaledImage.cols << "\n\n";

    int rows = scaledImage.rows;
    int cols = scaledImage.cols;
    size_t globalsize = rows * cols;
    Mat gradientimage(rows, cols, CV_8UC1);

    LOG("Launching SYCL kernel with # of iterations: "<< nIterations);

    int counter(nIterations);

#ifdef DEBUG_TIME
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point stop_time;
    double duration = 0.0;
    double tot_time = 0.0;

    START_TIMER();
#endif

    sycl::queue qsf;

#ifdef DEBUG_TIME
    STOP_TIMER();
    PRINT_TIMER("init     ");
    std::cout << std::endl;
#endif

    TIMER_START(exec)
    while(counter > 0) {

#ifdef DEBUG_TIME
        START_TIMER();
#endif

        //Allocate the device memory
        unsigned char *d_input, *d_gradient;
        d_input    = sycl::malloc_device<unsigned char>(globalsize, qsf);
        d_gradient = sycl::malloc_device<unsigned char>(globalsize, qsf);

#ifdef DEBUG_TIME
        STOP_TIMER();
        PRINT_TIMER("malloc   ");

        START_TIMER();
#endif

        //Copy source vectors from host to device
        qsf.memcpy(d_input, scaledImage.data, globalsize * sizeof(unsigned char));
        qsf.wait();

#ifdef DEBUG_TIME
        STOP_TIMER();
        PRINT_TIMER("memcpyH2D");

        START_TIMER();
#endif

        //Step 3 Gradient strength and direction
        constexpr int blockDim_x = 64;
        constexpr int blockDim_y = 2;
        sycl::range block(1, blockDim_y, blockDim_x);
        sycl::range grid(1, (rows - 2 + blockDim_y - 1) / blockDim_y, (cols - 2 + blockDim_x - 1) / blockDim_x);
        qsf.parallel_for(
            sycl::nd_range<3>(grid * block, block),
            [=](sycl::nd_item<3> item) {
                computeGradient(d_input, d_gradient, rows, cols, item);
            }
        );
        qsf.wait_and_throw();

#ifdef DEBUG_TIME
        STOP_TIMER();
        PRINT_TIMER("kernel   ");

        START_TIMER();
#endif

        //Copy back result from device to host
        qsf.memcpy(gradientimage.data, d_gradient, globalsize * sizeof(unsigned char));
        qsf.wait();

#ifdef DEBUG_TIME
        STOP_TIMER();
        PRINT_TIMER("memcpyD2H");
#endif

        //Free up device allocation
        sycl::free(d_input,    qsf);
        sycl::free(d_gradient, qsf);
        counter--;

#ifdef DEBUG_TIME
        std::cout << std::endl;
#endif
    }
    TIMER_END(exec)

    TIMER_START(io)
    Mat outputimage = cv::Mat::zeros(rows - 2, cols - 2, CV_8UC1);
    for(int i = 0; i< rows - 2; i++) {
        for(int j = 0; j< cols - 2; j++) {
            outputimage.at<uchar>(i, j) = gradientimage.at<uchar>(i + 1, j + 1);
        }
    }

    // write output
    if(parser.IsSet("-o")) {
        std::string outputfile = parser.GetSetting("-o");
        LOG("Writing output image into: "<< outputfile);
        if(outputfile.empty()) {
            LOG_ERROR("Invalid output filename provided.");
        }
        imwrite(outputfile, outputimage);
    }

    // run verification
    if(parser.IsSet("-v")) {
        Mat refimage = compute_reference_image(scaledImage);
        verify_results(outputimage, refimage, 5);
        if(parser.IsSet("-saveref")) {
            LOG("Saving reference image for debugging.");
            imwrite("./scalar.bmp", refimage);
        }
    }
    } catch (std::exception const& e) {
        std::cout << "Exception: " << e.what() << "\n";
    }
    TIMER_END(io)
    TIMER_END(init)
    TIMER_SUBTRACT(init, io)

    TIMER_PRINT(io, "sobelfilter - I/O time")
    TIMER_PRINT(exec, "sobelfilter - execution time")
    TIMER_PRINT(init, "sobelfilter - total init+exec time")

    return 0;
}
