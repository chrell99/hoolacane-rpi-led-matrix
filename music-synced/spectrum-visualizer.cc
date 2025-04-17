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

const int NUM_BINS = BUFFER_SIZE / 2;  // only half is useful in real FFT
const int HISTORY_SIZE = 43;  // about 1 second at 43 fps

template <typename T>
T clamp(T value, T minVal, T maxVal) {
    return std::max(minVal, std::min(value, maxVal));
}

int processArguments(int argc, char *argv[], double *freqFrom, double *freqTo, uint8_t *maxBrightness, double *dBMax) {
    if (argc < 5) {
        std::cerr << "Usage: " << argv[0] << " <frequency_from> <frequency_to> <brightness> <dBMax>" << std::endl;
        return -1;
    }

    *freqFrom = std::atof(argv[1]); 
    *freqTo = std::atof(argv[2]); 
    *maxBrightness = static_cast<uint8_t>(std::stoi(argv[3]));
    *dBMax = std::atof(argv[4]);

    std::cout << "Frequency Range: " << *freqFrom << " Hz to " << *freqTo << " Hz" << std::endl;
    std::cout << "Max Brightness: " << static_cast<int>(*maxBrightness) << std::endl;
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

void calcBarHeights(int fftSize, int sampleRate, int minFreq, int maxFreq, int numBars,
    const std::vector<float>& magnitudes, std::vector<int>& barHeights,
    float dBMin = -100.0f, float dBMax = 0.0f, int height = 100) {
    std::vector<float> logFreqEdges(numBars + 1);
    float logMin = std::log10(minFreq);
    float logMax = std::log10(maxFreq);

    for (int i = 0; i <= numBars; i++) {
        float t = static_cast<float>(i) / numBars;
        float logFreq = logMin + t * (logMax - logMin);
        logFreqEdges[i] = std::pow(10.0f, logFreq);
    }

    auto freqToBin = [&](float freq) -> int {
        int bin = static_cast<int>((freq / sampleRate) * fftSize);
        return clamp(bin, 0, fftSize / 2 - 1);
    };

    std::vector<int> binEdges(numBars + 1);
    for (int i = 0; i <= numBars; i++) {
        binEdges[i] = freqToBin(logFreqEdges[i]);
    }

    for (int i = 0; i < numBars; i++) {
        float sum = 0.0f;
        for (int b = binEdges[i]; b < binEdges[i + 1]; b++) {
            sum += magnitudes[b];
        }
        int binCount = binEdges[i + 1] - binEdges[i];
        float avg = (binCount > 0) ? sum / binCount : dBMin;

        avg = clamp(avg, dBMin, dBMax);
        float normalized = (avg - dBMin) / (dBMax - dBMin);

        barHeights[i] = static_cast<int>(normalized * height);
    }
}

int main(int argc, char *argv[]){
    //************ INPUT VARS ************/
    double freqFrom = 0.0;
    double freqTo = 20000.0;
    uint8_t maxbrightness = 80; 
    double dBMax = 130;
    double dBMin = 80;

    if(processArguments(argc, argv, &freqFrom, &freqTo, &maxbrightness, &dBMax) < 0){
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

    int numBars_ = 24; 
    const int width = matrix->width();
    int height_ = matrix->height();
    int barWidth_ = width/numBars_;
    int* barHeights_ = new int[numBars_];
    int heightGreen_  = height_*4/12;
    int heightYellow_ = height_*8/12;
    int heightOrange_ = height_*10/12;
    int heightRed_    = height_*12/12;

    while (true) {
        snd_pcm_readi(pcm_handle, buffer.data(), buffer_size);
        magnitudesDB = computeFFT(buffer);

        int fftSize = magnitudesDB.size();

        calcBarHeights(fftSize, SAMPLE_RATE, freqFrom, freqTo, numBars_, magnitudesDB, barHeights_);

        for (int i = 0; i < numBars; ++i) {
            cout << "Bar " << i << ": " << barHeights[i] << endl;
        }

        for (int i=0; i<numBars_; ++i) {
            int y;
            for (y=0; y<barHeights_[i]; ++y) {
                if (y<heightGreen_) {
                    for (int x=i*barWidth_; x<(i+1)*barWidth_; ++x) {
                        matrix->SetPixel(x, height_-1-y, 0, 200, 0);
                    }
                }
                else if (y<heightYellow_) {
                    for (int x=i*barWidth_; x<(i+1)*barWidth_; ++x) {
                        matrix->SetPixel(x, height_-1-y, 150, 150, 0);
                    }
                }
                else if (y<heightOrange_) {
                    for (int x=i*barWidth_; x<(i+1)*barWidth_; ++x) {
                        matrix->SetPixel(x, height_-1-y, 250, 100, 0);
                    }
                }
                else {
                    for (int x=i*barWidth_; x<(i+1)*barWidth_; ++x) {
                        matrix->SetPixel(x, height_-1-y, 200, 0, 0);
                    }
                }
            }
            // Anything above the bar should be black
            for (; y<height_; ++y) {
                for (int x=i*barWidth_; x<(i+1)*barWidth_; ++x) {
                    matrix->SetPixel(x, height_-1-y, 0, 0, 0);
                }
            }
        }

    }

    if (matrix == NULL)
        return 1;

    matrix->Clear();
    delete matrix;
    return 0;
}