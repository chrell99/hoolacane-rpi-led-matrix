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
#include <deque>

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

const int NUM_BINS = BUFFER_SIZE / 2;  // only half is useful in real FFT
const int HISTORY_SIZE = 43;  // about 1 second at 43 fps
const double FLUX_THRESHOLD = 100000.0;  // tweak as needed

std::vector<double> prevMagnitudes(NUM_BINS, 0.0);
std::deque<double> fluxHistory;

int processArguments(int argc, char *argv[], double *frequency, double *maxamplitude, uint8_t *maxbrightness) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <frequency> <maxamplitude> <brightness>" << std::endl;
        return -1;
    }

    *frequency = std::atof(argv[1]); 
    *maxamplitude = std::atof(argv[2]); 
    *maxbrightness = std::stoi(argv[3]);

    std::cout << "Frequency: " << *frequency << std::endl;
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

double computeFFT(std::vector<short>& buffer) {
    int N = BUFFER_SIZE;
    fftw_complex *in, *out;
    fftw_plan plan;

    in = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * N);
    out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * N);
    
    plan = fftw_plan_dft_1d(N, in, out, FFTW_FORWARD, FFTW_ESTIMATE);

    for (int i = 0; i < N; i++) {
        in[i][0] = buffer[i];  
        in[i][1] = 0.0;        
    }

    fftw_execute(plan);

    std::vector<double> magnitudes(N / 2);
    for (int i = 0; i < N / 2; i++) {
        magnitudes[i] = sqrt(out[i][0] * out[i][0] + out[i][1] * out[i][1]);
    }

    fftw_destroy_plan(plan);
    fftw_free(in);
    fftw_free(out);

    
    return magnitudes;
}

bool detectBeat(std::vector<double>& magnitudes) {
    double flux = 0.0;

    for (int i = 0; i < NUM_BINS; ++i) {
        double diff = magnitudes[i] - prevMagnitudes[i];
        if (diff > 0) {
            flux += diff;
        }
        prevMagnitudes[i] = magnitudes[i];  // update for next frame
    }

    // Maintain rolling history
    if (fluxHistory.size() >= HISTORY_SIZE)
        fluxHistory.pop_front();

    fluxHistory.push_back(flux);

    double avg = 0.0;
    for (auto f : fluxHistory) avg += f;
    avg /= fluxHistory.size();

    return flux > avg + FLUX_THRESHOLD;
}

int main(int argc, char *argv[]){
    //************ INPUT VARS ************/
    double frequency = 0.0;
    double maxamplitudedb = 0;
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

    
    std::vector<short> buffer(buffer_size);
    std::vector<double> magnitudes;
    while (true) {
        snd_pcm_readi(pcm_handle, buffer.data(), buffer_size);
        magnitudes = computeFFT(buffer);

        if(detectBeat(magnitudes)){
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