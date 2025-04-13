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

int LOW_BIN;
int HIGH_BIN;

int processArguments(int argc, char *argv[], double *freqFrom, double *freqTo, uint8_t *maxBrightness) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <frequency_from> <frequency_to> <brightness>" << std::endl;
        return -1;
    }

    *freqFrom = std::atof(argv[1]); 
    *freqTo = std::atof(argv[2]); 
    *maxBrightness = static_cast<uint8_t>(std::stoi(argv[3]));

    std::cout << "Frequency Range: " << *freqFrom << " Hz to " << *freqTo << " Hz" << std::endl;
    std::cout << "Max Brightness: " << static_cast<int>(*maxBrightness) << std::endl;

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

void drawBarRow(int bar, int y, uint8_t r, uint8_t g, uint8_t b) {
    for (int x=bar*barWidth_; x<(bar+1)*barWidth_; ++x) {
        canvas()->SetPixel(x, height_-1-y, r, g, b);
    }
}

int main(int argc, char *argv[]){
    //************ INPUT VARS ************/
    double freqFrom = 0.0;
    double freqTo = 20000.0;
    uint8_t maxbrightness = 80; 

    if(processArguments(argc, argv, &freqFrom, &freqTo, &maxbrightness) < 0){
        return -1;
    }

    LOW_BIN = static_cast<int>(round(freqFrom / (SAMPLE_RATE / BUFFER_SIZE)));
    HIGH_BIN = static_cast<int>(round(freqTo / (SAMPLE_RATE / BUFFER_SIZE)));

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
    matrix->SetBrightness(maxbrightness);
    
    std::vector<short> buffer(buffer_size);
    std::vector<double> magnitudes;

    int numBars_ = 24; 
    const int width = canvas()->width();
    int height_ = canvas()->height();
    int barWidth_ = width/numBars_;
    int* barHeights_ = new int[numBars_];
    int* barMeans_ = new int[numBars_];
    int* barFreqs_ = new int[numBars_];
    int heightGreen_  = height_*4/12;
    int heightYellow_ = height_*8/12;
    int heightOrange_ = height_*10/12;
    int heightRed_    = height_*12/12;

    while (true) {
        snd_pcm_readi(pcm_handle, buffer.data(), buffer_size);
        magnitudes = computeFFT(buffer);
        int fftSize = magnitudes.size();
        for (int i = 0; i < 24; i++){

            double barFreqStart = freqFrom * pow(freqTo / freqFrom, static_cast<double>(i) / numBars_);
            double barFreqEnd   = freqFrom * pow(freqTo / freqFrom, static_cast<double>(i + 1) / numBars_);

            int binStart = static_cast<int>((barFreqStart / SAMPLE_RATE) * fftSize)
            int binEnd = static_cast<int>((barFreqEnd / SAMPLE_RATE) * fftSize)

            binStart = std::clamp(binStart, 0, fftSize - 1);
            binEnd   = std::clamp(binEnd, binStart + 1, fftSize);

            double sum = 0;
            for (int b = binStart; b < binEnd; ++b) {
                sum += magnitudes[b]; 
            }
            double avg = sum / (binEnd - binStart);

            double dBMin = 80;
            double dBMax = 130;
            avg = std::clamp(avg, dBMin, dBMax);
            double normalized = (avg - dBMin) / (dBMax - dBMin);

            barHeights_[i] = static_cast<int>(normalized * height_);
        }

        for (int i=0; i<numBars_; ++i) {
            int y;
            for (y=0; y<barHeights_[i]; ++y) {
                if (y<heightGreen_) {
                drawBarRow(i, y, 0, 200, 0);
                }
                else if (y<heightYellow_) {
                drawBarRow(i, y, 150, 150, 0);
                }
                else if (y<heightOrange_) {
                drawBarRow(i, y, 250, 100, 0);
                }
                else {
                drawBarRow(i, y, 200, 0, 0);
                }
            }
            // Anything above the bar should be black
            for (; y<height_; ++y) {
                drawBarRow(i, y, 0, 0, 0);
            }
        }

    }

    if (matrix == NULL)
        return 1;

    matrix->Clear();
    delete matrix;
    return 0;
}


class VolumeBars : public DemoRunner {
    public:
      VolumeBars(Canvas *m, int delay_ms=50, int numBars=8)
        : DemoRunner(m), delay_ms_(delay_ms),
          numBars_(numBars), t_(0) {
      }
    
      ~VolumeBars() {
        delete [] barHeights_;
        delete [] barFreqs_;
        delete [] barMeans_;
      }
    
      void Run() override {
        const int width = canvas()->width();
        height_ = canvas()->height();
        barWidth_ = width/numBars_;
        barHeights_ = new int[numBars_];
        barMeans_ = new int[numBars_];
        barFreqs_ = new int[numBars_];
        heightGreen_  = height_*4/12;
        heightYellow_ = height_*8/12;
        heightOrange_ = height_*10/12;
        heightRed_    = height_*12/12;
    
        // Array of possible bar means
        int numMeans = 10;
        int means[10] = {1,2,3,4,5,6,7,8,16,32};
        for (int i=0; i<numMeans; ++i) {
          means[i] = height_ - means[i]*height_/8;
        }
        // Initialize bar means randomly
        srand(time(NULL));
        for (int i=0; i<numBars_; ++i) {
          barMeans_[i] = rand()%numMeans;
          barFreqs_[i] = 1<<(rand()%3);
        }
    
        // Start the loop
        while (!interrupt_received) {
          if (t_ % 8 == 0) {
            // Change the means
            for (int i=0; i<numBars_; ++i) {
              barMeans_[i] += rand()%3 - 1;
              if (barMeans_[i] >= numMeans)
                barMeans_[i] = numMeans-1;
              if (barMeans_[i] < 0)
                barMeans_[i] = 0;
            }
          }
    
          // Update bar heights
          t_++;
          for (int i=0; i<numBars_; ++i) {
            barHeights_[i] = (height_ - means[barMeans_[i]])
              * sin(0.1*t_*barFreqs_[i]) + means[barMeans_[i]];
            if (barHeights_[i] < height_/8)
              barHeights_[i] = rand() % (height_/8) + 1;
          }
    
          for (int i=0; i<numBars_; ++i) {
            int y;
            for (y=0; y<barHeights_[i]; ++y) {
              if (y<heightGreen_) {
                drawBarRow(i, y, 0, 200, 0);
              }
              else if (y<heightYellow_) {
                drawBarRow(i, y, 150, 150, 0);
              }
              else if (y<heightOrange_) {
                drawBarRow(i, y, 250, 100, 0);
              }
              else {
                drawBarRow(i, y, 200, 0, 0);
              }
            }
            // Anything above the bar should be black
            for (; y<height_; ++y) {
              drawBarRow(i, y, 0, 0, 0);
            }
          }
        }
      }
    
    private:
      
    
      int delay_ms_;
      int numBars_;
      int* barHeights_;
      int barWidth_;
      int height_;
      int heightGreen_;
      int heightYellow_;
      int heightOrange_;
      int heightRed_;
      int* barFreqs_;
      int* barMeans_;
      int t_;
    };