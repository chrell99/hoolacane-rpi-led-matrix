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

int processArguments(int argc, char *argv[], double *freqFrom, double *freqTo, 
                     uint8_t *maxBrightness, size_t *targetDNR, float *dropRate, 
                     float *riseSmooth, float *lerpFactor, float *historySecs) {
    if (argc < 9) {
        std::cerr << "Usage: " << argv[0] << " <frequency from> <frequency to> <brightness> <Dynamic range> <droprate> <rise smoothness> <lerp> <hist_secs>" << std::endl;
        return -1;
    }

    *freqFrom = std::atof(argv[1]); 
    *freqTo = std::atof(argv[2]); 
    *maxBrightness = static_cast<uint8_t>(std::stoi(argv[3]));
    *targetDNR = static_cast<size_t>(std::stoul(argv[4]));
    *dropRate = std::atof(argv[5]); 
    *riseSmooth = std::atof(argv[6]); 
    *lerpFactor = std::atof(argv[7]);
    *historySecs = std::atof(argv[8]);

    std::cout << "--- Configuration ---" << std::endl;
    std::cout << "Range: " << *freqFrom << "-" << *freqTo << "Hz | DNR: " << *targetDNR << "dB" << std::endl;
    std::cout << "Droprate: " << *dropRate << " | rise smoothness: " << *riseSmooth << std::endl;
    std::cout << "AGC Lerp: " << *lerpFactor << " | History: " << *historySecs << "s" << std::endl;

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
    const std::vector<double>& magnitudes, int* barHeights,
    float dBMin = 80.0f, float dBMax = 110.0f, int height = 100) {

    std::vector<float> melFreqEdges(numBars + 1);

    float melMin = 2595.0f * log10f(1.0f + minFreq / 700.0f);
    float melMax = 2595.0f * log10f(1.0f + maxFreq / 700.0f);

    for (int i = 0; i <= numBars; i++) {
        float t = static_cast<float>(i) / numBars;
        float mel = melMin + t * (melMax - melMin);
        melFreqEdges[i] = 700.0f * (powf(10.0f, mel / 2595.0f) - 1.0f);
    }

    auto freqToBin = [&](float freq) -> int {
        int bin = static_cast<int>((freq / sampleRate) * fftSize);
        return clamp(bin, 0, fftSize / 2 - 1);
    };

    std::vector<int> binEdges(numBars + 1);
    for (int i = 0; i <= numBars; i++) {
        binEdges[i] = freqToBin(melFreqEdges[i]);
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
    double freqFrom, freqTo;
    uint8_t maxbrightness;
    size_t targetDNR;
    float dropRate, riseSmooth, lerpFactor, historySecs;

    if(processArguments(argc, argv, &freqFrom, &freqTo, &maxbrightness, 
                        &targetDNR, &dropRate, &riseSmooth, &lerpFactor, &historySecs) < 0){
        return -1;
    }

    // CALCULATE HISTORY LENGTH based on audio timing
    // frames = seconds * (samples_per_sec / samples_per_frame)
    size_t maxHistoryLen = static_cast<size_t>(historySecs * (static_cast<float>(SAMPLE_RATE) / BUFFER_SIZE));
    if (maxHistoryLen < 1) maxHistoryLen = 1;

    // AGC State
    double autoDBMin = 60;
    double autoDBMax = 100;
    std::deque<double> maxHistory;

    // Standard audio setup
    snd_pcm_t *pcm_handle;
    snd_pcm_hw_params_t *params;
    if (configure_pcm_device(pcm_handle, params, SND_PCM_FORMAT_S16_LE, SAMPLE_RATE, 1, BUFFER_SIZE) < 0) return -1;


    //************ RGB MATRIX VARS ************/
    RGBMatrix::Options options;
    options.hardware_mapping = "regular";
    options.rows = 32; options.cols = 32; options.chain_length = 3; options.parallel = 3;
    options.show_refresh_rate = false;
    options.multiplexing = 1;
    options.pixel_mapper_config = "Rotate:270";
    rgb_matrix::RuntimeOptions rOptions;
    rOptions.gpio_slowdown = 2;

    RGBMatrix *matrix = RGBMatrix::CreateFromOptions(options, rOptions);
    matrix->SetBrightness(maxbrightness);
    
    std::vector<short> buffer(BUFFER_SIZE);
    std::vector<double> magnitudesDB;

    int numBars_ = 24; 
    const int width = matrix->width();
    int height_ = matrix->height();
    int barWidth_ = width/numBars_;
    int* barHeights_ = new int[numBars_];
    std::vector<float> smoothHeights(numBars_, 0.0f);
    int heightGreen_  = height_*4/12;
    int heightYellow_ = height_*8/12;
    int heightOrange_ = height_*10/12;
    int heightRed_    = height_*12/12;

    // Variables for FPS calculation
    int frameCount = 0;
    auto lastFpsTimestamp = std::chrono::steady_clock::now();
    double currentFps = 0.0;

    while (true) {
        // Sound capture
        snd_pcm_readi(pcm_handle, buffer.data(), BUFFER_SIZE);
        magnitudesDB = computeFFT(buffer);

        // 2. AGC CALCULATION
        double frameMax = -200.0;
        // Find the highest magnitude in the current FFT frame
        for (double val : magnitudesDB) {
            if (val > frameMax) frameMax = val;
        }

        // Update history of peaks
        maxHistory.push_back(frameMax);
        if (maxHistory.size() > maxHistoryLen) maxHistory.pop_front();

        // Calculate the target Max (average of recent peaks)
        double targetMax = 0;
        for (double m : maxHistory) targetMax += m;
        targetMax /= maxHistory.size();

        // Set a 40dB dynamic range window relative to the peak
        double targetMin = targetMax - targetDNR;

        // Smoothly "float" the current range toward the targets
        autoDBMax = autoDBMax + (targetMax - autoDBMax) * lerpFactor;
        autoDBMin = autoDBMin + (targetMin - autoDBMin) * lerpFactor;

        // Safety clamps: prevent the UI from "hunting" in total silence
        if (autoDBMax < 60) autoDBMax = 60; 
        if (autoDBMin < 40) autoDBMin = 40;

        calcBarHeights(magnitudesDB.size(), SAMPLE_RATE, freqFrom, freqTo, numBars_, magnitudesDB, barHeights_, autoDBMin, autoDBMax, height_);

        for (int i = 0; i < numBars_; ++i) {

            // Smoothing the chnages in the bars height
            float targetHeight = static_cast<float>(barHeights_[i]);

            if (targetHeight > smoothHeights[i]) {
                smoothHeights[i] = targetHeight * riseSmooth + smoothHeights[i] * (1.0f - riseSmooth);
            } else {
                smoothHeights[i] -= dropRate;
            }

            if (smoothHeights[i] < 0) smoothHeights[i] = 0;
            
            int visualHeight = static_cast<int>(smoothHeights[i]);

            // Drawing to the panel
            int y;
            for (y = 0; y < visualHeight; ++y) {
                if (y < heightGreen_) {
                    for (int x = i * barWidth_; x < (i + 1) * barWidth_; ++x) 
                        matrix->SetPixel(x, height_ - 1 - y, 0, 200, 0);
                } else if (y < heightYellow_) {
                    for (int x = i * barWidth_; x < (i + 1) * barWidth_; ++x) 
                        matrix->SetPixel(x, height_ - 1 - y, 150, 150, 0);
                } else if (y < heightOrange_) {
                    for (int x = i * barWidth_; x < (i + 1) * barWidth_; ++x) 
                        matrix->SetPixel(x, height_ - 1 - y, 250, 100, 0);
                } else {
                    for (int x = i * barWidth_; x < (i + 1) * barWidth_; ++x) 
                        matrix->SetPixel(x, height_ - 1 - y, 200, 0, 0);
                }
            }
            for (; y < height_; ++y) {
                for (int x = i * barWidth_; x < (i + 1) * barWidth_; ++x) 
                    matrix->SetPixel(x, height_ - 1 - y, 0, 0, 0);
            }
        }

        frameCount++;
        auto currentTime = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastFpsTimestamp).count();

        if (elapsed >= 1000) {
            currentFps = frameCount / (elapsed / 1000.0);
            
            // Print to console (using \r to overwrite the same line)
            std::cout << "\rFPS: " << std::fixed << std::setprecision(1) << currentFps << "   " << std::flush;
            
            frameCount = 0;
            lastFpsTimestamp = currentTime;
        }

        // usleep(20000); // Leave commented to get maximum possible FPS
    }

    if (matrix == NULL)
        return 1;

    matrix->Clear();
    delete matrix;
    return 0;
}