#include <iostream>
#include <alsa/asoundlib.h>
#include <cmath>
#include <vector>
#include <chrono>
#include <iomanip>

#include "led-matrix.h"
#include <unistd.h>
#include <math.h>
#include <stdio.h>
#include <signal.h>

#define PCM_DEVICE "hw:1,0" // USB Dongle audio input
#define SAMPLE_RATE 44100   // 44.1 kHz sample rate
#define BUFFER_SIZE 1024    

using rgb_matrix::Canvas;
using rgb_matrix::RGBMatrix;

uint64_t micros()
{
    static auto start_time = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();

    return std::chrono::duration_cast<std::chrono::microseconds>(now - start_time).count();
}

int processArguments(int argc, char *argv[], double *maxamplitude, uint8_t *maxbrightness) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << "<maxamplitude> <brightness>" << std::endl;
        return -1;
    }

    *maxamplitude = std::atof(argv[1]); 
    double tmp = std::atof(argv[2]);
    *maxbrightness = static_cast<uint8_t>(tmp);

    std::cout << "Max Amplitude: " << *maxamplitude << std::endl;
    std::cout << "Max Brightness: " << *maxbrightness << std::endl;

    return 0;
}

int configure_pcm_device(snd_pcm_t *&pcm_handle, snd_pcm_hw_params_t *&params,
                         snd_pcm_format_t format, unsigned int rate, int channels, int buffer_size)
{
    if (snd_pcm_open(&pcm_handle, PCM_DEVICE, SND_PCM_STREAM_CAPTURE, 0) < 0)
    {
        std::cerr << "Failed to open PCM device" << std::endl;
        return -1;
    }

    snd_pcm_hw_params_alloca(&params);
    if (snd_pcm_hw_params_any(pcm_handle, params) < 0 ||
        snd_pcm_hw_params_set_access(pcm_handle, params, SND_PCM_ACCESS_RW_INTERLEAVED) < 0 ||
        snd_pcm_hw_params_set_format(pcm_handle, params, format) < 0 ||
        snd_pcm_hw_params_set_rate_near(pcm_handle, params, &rate, 0) < 0 ||
        snd_pcm_hw_params_set_channels(pcm_handle, params, channels) < 0 ||
        snd_pcm_hw_params(pcm_handle, params) < 0)
    {
        std::cerr << "Failed to configure PCM device" << std::endl;
        return -1;
    }

    if (snd_pcm_prepare(pcm_handle) < 0)
    {
        std::cerr << "Failed to prepare PCM device" << std::endl;
        return -1; 
    }

    std::cout << "Succesfully opened the PCM device" << std::endl;
    return 0; 
}

float calculate_rms(const short *buffer, size_t buffer_size) {
    float sum = 0.0f;

    for (size_t i = 0; i < buffer_size; ++i) {
        sum += buffer[i] * buffer[i];
    }

    float rms = std::sqrt(sum / buffer_size);

    return rms;
}

float rms_to_db(float rms) {
    if (rms == 0.0f) return 0.0;
    
    return 20 * std::log10(rms);
}

int main(int argc, char *argv[]){
    //************ INPUT VARS ************/
    double maxamplitude = 0.0;
    uint8_t maxbrightness = 100; 

    if(processArguments(argc, argv, &frequency, &maxamplitude, &maxbrightness) < 0){
        return -1;
    }


    //************ SOUND INIT ************/
    snd_pcm_t *pcm_handle;
    snd_pcm_hw_params_t *params;
    snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;
    unsigned int rate = SAMPLE_RATE;
    int channels = 1;
    int buffer_size = BUFFER_SIZE;

    if (configure_pcm_device(pcm_handle, params, format, rate, channels, buffer_size) < 0) {
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
    rgb_matrix::RuntimeOptions rOptions;
    rOptions.gpio_slowdown = 2;

    RGBMatrix *matrix = RGBMatrix::CreateFromOptions(options, rOptions);

    
    short buffer[buffer_size];
    float temp;

    while (true) {
        snd_pcm_readi(pcm_handle, buffer, buffer_size);
        
        rms = calculate_rms(buffer, buffer_size);
        double rmsdB = rms_to_db(rms);

        std::cout << rms << std::endl;
        std::cout << rmsdB << std::endl;
        /*
        exponent = 0.2 + (amplitude / maxamplitude);
        temp2 = pow(maxbrightness, exponent);

        if(temp2 >= maxbrightness){
            matrix->SetBrightness(maxbrightness);
        }
        else if(temp2 <= 10){
            matrix->SetBrightness(0);
            matrix->Fill(0, 0, 0);
        }
        else{
            matrix->SetBrightness(temp2);
            matrix->Fill(255, 255, 255);
        }
        */
    }

    if (matrix == NULL)
        return 1;

    matrix->Clear();
    delete matrix;
    return 0;
}