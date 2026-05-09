#include <iostream>
#include <cmath>
#include <vector>
#include <iomanip>

#include "led-matrix.h"
#include <unistd.h>
#include <math.h>
#include <stdio.h>
#include <signal.h>

using rgb_matrix::RGBMatrix;
using rgb_matrix::Canvas;

volatile bool interrupt_received = false;
static void InterruptHandler(int signo) {
  interrupt_received = true;
}

int processArguments(int argc, char *argv[], int *onTimems, int *offTimems, int *brightness) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <onTimems> <offTimems> <brightness>" << std::endl;
        return -1;
    }
 
    *onTimems = static_cast<int>(std::stoi(argv[1]));
    *offTimems = static_cast<int>(std::stoi(argv[2]));
    *brightness = static_cast<int>(std::stoi(argv[3]));

    std::cout << "On time in ms: " << static_cast<int>(*onTimems) << std::endl;
    std::cout << "Off time in ms: " << static_cast<int>(*offTimems) << std::endl;
    std::cout << "Brightness: " << static_cast<int>(*brightness) << std::endl;

    return 0;
}

static void MatrixStrobe(Canvas *canvas, int onTimems, int offTimems, int brightness) {

    while(true) {
        if (interrupt_received){
            return;
        }
        canvas->Fill(255, 255, 255);
        usleep(onTimems * 1000); 
        canvas->Fill(0, 0, 0);
        usleep(offTimems * 1000);
    }
  }

int main(int argc, char *argv[]){
    //************ INPUT VARS ************/
    int onTimems = 200;
    int offTimems = 200;
    int brightness = 80;

    if(processArguments(argc, argv, &onTimems, &offTimems, &brightness) < 0){
        return -1;
    }

    //************ RGB MATRIX VARS ************/
    RGBMatrix::Options options;
    options.hardware_mapping = "regular";
    options.rows = 32;
    options.cols = 32;
    options.chain_length = 3;
    options.parallel = 3;
    options.show_refresh_rate = false;
    options.multiplexing = 1;
    options.pixel_mapper_config = "Rotate:270";
    rgb_matrix::RuntimeOptions rOptions;
    rOptions.gpio_slowdown = 2;

    RGBMatrix *matrix = RGBMatrix::CreateFromOptions(options, rOptions);
    matrix->SetBrightness(brightness);

    signal(SIGTERM, InterruptHandler);
    signal(SIGINT, InterruptHandler);

    MatrixStrobe(matrix, onTimems, offTimems, brightness);

    matrix->Clear();
    delete matrix;

    return 0;

}




