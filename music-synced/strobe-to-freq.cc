#include <iostream>
#include <alsa/asoundlib.h>
#include <cmath>
#include <vector>
#include <fftw3.h>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <string>

#include "led-matrix.h"
#include <unistd.h>
#include <math.h>
#include <stdio.h>
#include <signal.h>

#define PCM_DEVICE "hw:0,0" 
#define SAMPLE_RATE 44100   
#define BUFFER_SIZE 1024    

using rgb_matrix::RGBMatrix;
using rgb_matrix::FrameCanvas;

// --- Help & Arguments ---
void printHelp(const char* progName) {
    std::cout << "Usage: " << progName << " [OPTIONS]\n"
              << "Options:\n"
              << "  -h, --help                Show this help menu\n"
              << "  -d, --debug               Console output for levels\n"
              << "  -b, --brightness [0-100]  Set brightness (default: 80)\n"
              << "  -s, --sensitivity [val]   dB trigger offset (default: 10.0)\n"
              << "  -w, --window [1-100]      AGC smoothing (default: 50, higher=slower)\n"
              << std::endl;
}

int processArguments(int argc, char *argv[], int *brightness, bool *debug, double *sensitivity, double *emaAlpha) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") { printHelp(argv[0]); return 1; }
        else if (arg == "-d" || arg == "--debug") { *debug = true; }
        else if (arg == "-b" || arg == "--brightness") { 
            if (i + 1 < argc) *brightness = std::stoi(argv[++i]); 
        }
        else if (arg == "-s" || arg == "--sensitivity") { 
            if (i + 1 < argc) *sensitivity = std::stod(argv[++i]); 
        }
        else if (arg == "-w" || arg == "--window") {
            if (i + 1 < argc) {
                int win = std::max(1, std::min(100, std::stoi(argv[++i])));
                *emaAlpha = 1.0 / (win * 10.0);
            }
        }
    }
    return 0;
}

// --- Audio Logic ---
int configure_pcm_device(snd_pcm_t *&pcm_handle, snd_pcm_hw_params_t *&params) {
    if (snd_pcm_open(&pcm_handle, PCM_DEVICE, SND_PCM_STREAM_CAPTURE, 0) < 0) return -1;
    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(pcm_handle, params);
    snd_pcm_hw_params_set_access(pcm_handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm_handle, params, SND_PCM_FORMAT_S16_LE);
    unsigned int rate = SAMPLE_RATE;
    snd_pcm_hw_params_set_rate_near(pcm_handle, params, &rate, 0);
    snd_pcm_hw_params_set_channels(pcm_handle, params, 1);
    if (snd_pcm_hw_params(pcm_handle, params) < 0) return -1;
    return (snd_pcm_prepare(pcm_handle) < 0) ? -1 : 0;
}

std::vector<double> computeFFT(std::vector<short>& buffer) {
    int N = BUFFER_SIZE;
    fftw_complex *in = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * N);
    fftw_complex *out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * N);
    fftw_plan plan = fftw_plan_dft_1d(N, in, out, FFTW_FORWARD, FFTW_ESTIMATE);
    for (int i = 0; i < N; i++) {
        double window = 0.5 * (1 - cos((2 * M_PI * i) / (N - 1)));
        in[i][0] = buffer[i] * window; in[i][1] = 0.0;
    }
    fftw_execute(plan);
    std::vector<double> magnitudesDB(N / 2);
    for (int i = 0; i < N / 2; i++) {
        magnitudesDB[i] = 20.0 * log10(sqrt(out[i][0]*out[i][0] + out[i][1]*out[i][1]) + 1e-10);
    }
    fftw_destroy_plan(plan); fftw_free(in); fftw_free(out);
    return magnitudesDB;
}

int main(int argc, char *argv[]) {
    int brightness = 80;
    bool debug = false;
    double sensitivity = 10.0;
    double emaAlpha = 0.02; 

    if (processArguments(argc, argv, &brightness, &debug, &sensitivity, &emaAlpha) != 0) return 0;

    snd_pcm_t *pcm_handle;
    snd_pcm_hw_params_t *params;
    if (configure_pcm_device(pcm_handle, params) < 0) return -1;

    RGBMatrix::Options options;
    options.hardware_mapping = "regular";
    options.rows = 32; options.cols = 32; options.chain_length = 3;
    options.parallel = 3; options.pixel_mapper_config = "Rotate:270";
    options.multiplexing = 1;
    rgb_matrix::RuntimeOptions rOptions;
    rOptions.gpio_slowdown = 2;

    RGBMatrix *matrix = RGBMatrix::CreateFromOptions(options, rOptions);
    if (!matrix) return 1;
    matrix->SetBrightness(brightness);

    FrameCanvas *offscreen_canvas = matrix->CreateFrameCanvas();
    
    std::vector<short> buffer(BUFFER_SIZE);
    double rollingAverageBassDB = 0.0;
    bool firstRun = true;

    while (true) {
        if (snd_pcm_readi(pcm_handle, buffer.data(), BUFFER_SIZE) < 0) {
            snd_pcm_prepare(pcm_handle);
            continue;
        }

        std::vector<double> magnitudesDB = computeFFT(buffer); 
        double currentBassLevel = 0.0;
        for (int i = 1; i <= 5; ++i) currentBassLevel += magnitudesDB[i];
        currentBassLevel /= 5.0;

        if (firstRun) { 
            rollingAverageBassDB = currentBassLevel; 
            firstRun = false; 
        } else {
            rollingAverageBassDB = (emaAlpha * currentBassLevel) + ((1.0 - emaAlpha) * rollingAverageBassDB);
        }

        double dynamicThreshold = rollingAverageBassDB + sensitivity;

        if (debug) {
            std::cout << "\rAvg: " << std::fixed << std::setprecision(1) << rollingAverageBassDB 
                      << " | Cur: " << currentBassLevel << " | Thr: " << dynamicThreshold << "    " << std::flush;
        }

        // --- Render to the offscreen canvas to avoid tearing/flicker ---
        if(currentBassLevel > dynamicThreshold) {
            offscreen_canvas->Fill(255, 255, 255);
        } else {
            offscreen_canvas->Fill(0, 0, 0);
        }
        
        // Swap buffers on VSync
        offscreen_canvas = matrix->SwapOnVSync(offscreen_canvas);
    }

    delete matrix;
    return 0;
}