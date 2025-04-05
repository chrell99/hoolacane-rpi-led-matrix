#include <iostream>
#include <alsa/asoundlib.h>
#include <cmath>
#include <vector>
#include <fftw3.h>
#include <chrono>
#include <iomanip>

#include "led-matrix.h"
#include <unistd.h>
#include <math.h>
#include <stdio.h>
#include <signal.h>

using rgb_matrix::RGBMatrix;
using rgb_matrix::Canvas;


uint64_t micros() {
    static auto start_time = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();

    return std::chrono::duration_cast<std::chrono::microseconds>(now - start_time).count();
}

int main(int argc, char *argv[]) {
    std::cout << "TIMESTAMP 1: " << micros() << std::endl;
    RGBMatrix::Options options;
    options.hardware_mapping = "regular"; 
    options.rows = 32;
    options.chain_length = 3;
    options.parallel = 3;
    options.show_refresh_rate = false;
    options.multiplexing = 1;
    rgb_matrix::RuntimeOptions rOptions;
    rOptions.gpio_slowdown = 2;
    
    RGBMatrix* canvas = RGBMatrix::CreateFromOptions(options, rOptions);
    canvas->SetBrightness(50);

    for (int i = 0; i < argc; ++i) {
        std::cout << "Argument " << i << ": " << argv[i] << std::endl;
    }
  
    if (canvas == NULL)
      return 1;
  
    canvas->Clear();
    delete canvas;
    return 0;
  }