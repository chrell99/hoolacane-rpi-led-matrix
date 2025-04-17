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

#define PCM_DEVICE "hw:0,0" // USB Dongle audio input
#define SAMPLE_RATE 44100   // 44.1 kHz sample rate
#define BUFFER_SIZE 1024    // FFT buffer size (must be power of 2)
/* Freq bins are calculated with the sample rate divided by the buffer size:

    44100 / 2048 = 21.53Hz
    44100 / 1024 = 43.07Hz
    44100 / 512  = 86.13Hz

*/

using rgb_matrix::Canvas;
using rgb_matrix::RGBMatrix;

template <typename T>
T clamp(T value, T minVal, T maxVal) {
    return std::max(minVal, std::min(value, maxVal));
}

int processArguments(int argc, char *argv[], double *freqFrom, double *freqTo, uint8_t *maxBrightness, double *dBMin, double *dBMax) {
    if (argc < 6) {
        std::cerr << "Usage: " << argv[0] << " <frequency_from> <frequency_to> <brightness> <dBMin> <dBMax>" << std::endl;
        return -1;
    }

    *freqFrom = std::atof(argv[1]); 
    *freqTo = std::atof(argv[2]); 
    *maxBrightness = static_cast<uint8_t>(std::stoi(argv[3]));
    *dBMin = std::atof(argv[4]);
    *dBMax = std::atof(argv[5]);

    std::cout << "Frequency Range: " << *freqFrom << " Hz to " << *freqTo << " Hz" << std::endl;
    std::cout << "Max Brightness: " << static_cast<int>(*maxBrightness) << std::endl;
    std::cout << "Min dB: " << *dBMin << std::endl;
    std::cout << "Max dB: " << *dBMax << std::endl;

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

std::vector<double> computeFFT(std::vector<short>& buffer) {
    int N = BUFFER_SIZE;
    fftw_complex *in, *out;
    fftw_plan plan;

    in = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * N);
    out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * N);

    plan = fftw_plan_dft_1d(N, in, out, FFTW_FORWARD, FFTW_ESTIMATE);

    for (int i = 0; i < N; i++) {
        double window = 0.5 * (1 - cos((2 * M_PI * i) / (N - 1)));
        in[i][0] = buffer[i] * window;
        in[i][1] = 0.0;
    }

    fftw_execute(plan);

    std::vector<double> magnitudesDB(N / 2);
    for (int i = 0; i < N / 2; i++) {
        double mag = sqrt(out[i][0] * out[i][0] + out[i][1] * out[i][1]);
        magnitudesDB[i] = 20.0 * log10(mag + 1e-10);
    }

    fftw_destroy_plan(plan);
    fftw_free(in);
    fftw_free(out);

    return magnitudesDB;
}

int main(int argc, char *argv[]){
    //************ INPUT VARS ************/
    double freqFrom = 0.0;
    double freqTo = 20000.0;
    uint8_t maxbrightness = 80;
    double dBMin = 80; 
    double dBMax = 130;

    if(processArguments(argc, argv, &freqFrom, &freqTo, &maxbrightness, &dBMin, &dBMax) < 0){
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
    options.pixel_mapper_config = "Rotate:270";
    rgb_matrix::RuntimeOptions rOptions;
    rOptions.gpio_slowdown = 2;

    RGBMatrix *matrix = RGBMatrix::CreateFromOptions(options, rOptions);
    matrix->SetBrightness(maxbrightness);

    
    std::vector<short> buffer(buffer_size);
    std::vector<double> magnitudesDB;

    while (true) {
        snd_pcm_readi(pcm_handle, buffer.data(), buffer_size);
        magnitudesDB = computeFFT(buffer, frequency);

        if(magnitudesDB[2] > (dBMax - 10)){
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