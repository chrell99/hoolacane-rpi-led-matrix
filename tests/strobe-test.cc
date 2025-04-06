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

#define PCM_DEVICE "hw:1,0" // USB Dongle audio input
#define SAMPLE_RATE 44100   // 44.1 kHz sample rate
#define BUFFER_SIZE 1024    // FFT buffer size (must be power of 2)
/* Freq bins are calculated with the sample rate divided by the buffer size:

    44100 / 2048 = 21.53Hz
    44100 / 1024 = 43.07Hz
    44100 / 512  = 86.13Hz

*/

using rgb_matrix::Canvas;
using rgb_matrix::RGBMatrix;

uint64_t micros()
{
    static auto start_time = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();

    return std::chrono::duration_cast<std::chrono::microseconds>(now - start_time).count();
}

int processArguments(int argc, char *argv[], double *frequency, double *ampvalue) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <frequency> <double_value>" << std::endl;
        return -1;
    }

    *frequency = std::atof(argv[1]); 
    *ampvalue = std::atof(argv[2]); 

    

    std::cout << "Frequency: " << *frequency << std::endl;
    std::cout << "Value: " << *ampvalue << std::endl;

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

double computeFFT(std::vector<short>& buffer, double freq) {
    int N = BUFFER_SIZE;
    fftw_complex *in, *out;
    fftw_plan plan;

    // Allocate FFT input and output arrays
    in = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * N);
    out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * N);
    
    plan = fftw_plan_dft_1d(N, in, out, FFTW_FORWARD, FFTW_ESTIMATE);

    // Allocate the input data to the FFTW in buffer
    for (int i = 0; i < N; i++) {
        in[i][0] = buffer[i];  
        in[i][1] = 0.0;        
    }

    fftw_execute(plan);

    std::cout << "\r";

    int temp = round(freq/(SAMPLE_RATE / BUFFER_SIZE));
    for (int i = temp - 1; i < (temp + 2); i++) {
        double magnitude = sqrt(out[i][0] * out[i][0] + out[i][1] * out[i][1]);
        std::cout << std::setw(10) << (int)magnitude << "   ";
    }

    std::cout << std::flush;

    // Cleanup
    fftw_destroy_plan(plan);
    fftw_free(in);
    fftw_free(out);

    // 12 * 43.07Hz = 516Hz
    return sqrt(out[temp][0] * out[temp][0] + out[temp][1] * out[temp][1]);
}

int main(int argc, char *argv[]){
    double frequency = 0.0;
    double ampvalue = 0.0;
    double amplitude;

    if(processArguments(argc, argv, &frequency, &ampvalue) < 0){
        return -1;
    }

    //************ SOUND INIT **************/
    snd_pcm_t *pcm_handle;
    snd_pcm_hw_params_t *params;
    snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;
    unsigned int rate = SAMPLE_RATE;
    int channels = 1;
    int buffer_size = BUFFER_SIZE;

    if (configure_pcm_device(pcm_handle, params, format, rate, channels, buffer_size) < 0) {
        return -1; 
    }

    //************ RGB MATRIX VARS **************/
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
    matrix->SetBrightness(20);

    
    std::vector<short> buffer(buffer_size);

    int temp = round(frequency/(SAMPLE_RATE / BUFFER_SIZE));

    for (int i = temp - 1; i < (temp + 2); i++) {
        double frequency = (double)i * SAMPLE_RATE / BUFFER_SIZE;
        std::cout << std::setw(10) << (int)frequency << " Hz";
    }
    std::cout << "\n";

    while (true) {
        snd_pcm_readi(pcm_handle, buffer.data(), buffer_size);
        amplitude = computeFFT(buffer, frequency);
        if(amplitude > ampvalue){
            matrix->Fill(255, 255, 255);
        }
        else{
            matrix->Fill(0, 0, 0);
        }

    }



    if (matrix == NULL)
        return 1;

    matrix->Clear();
    delete matrix;
    return 0;
}