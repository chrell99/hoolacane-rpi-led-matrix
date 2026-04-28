#include <iostream>
#include <alsa/asoundlib.h>
#include <cmath>
#include <vector>
#include <deque>
#include <fftw3.h>
#include <chrono>
#include <iomanip>
#include "led-matrix.h"
#include <unistd.h>
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

// Global for cleanup
bool keep_running = true;
void intHandler(int dummy) { keep_running = false; }

int configure_pcm_device(snd_pcm_t *&pcm_handle, snd_pcm_format_t format, unsigned int rate, int channels) {
    if (snd_pcm_open(&pcm_handle, PCM_DEVICE, SND_PCM_STREAM_CAPTURE, 0) < 0) return -1;
    snd_pcm_hw_params_t *params;
    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(pcm_handle, params);
    snd_pcm_hw_params_set_access(pcm_handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm_handle, params, format);
    snd_pcm_hw_params_set_rate_near(pcm_handle, params, &rate, 0);
    snd_pcm_hw_params_set_channels(pcm_handle, params, channels);
    snd_pcm_hw_params(pcm_handle, params);
    return 0;
}

int main(int argc, char *argv[]) {
    signal(SIGINT, intHandler);

    // 1. Settings
    int brightness = 80;
    double sensitivity = 1.3; // Multiplier: Trigger if current bass is 1.3x higher than average
    if (argc > 1) brightness = std::stoi(argv[1]);
    if (argc > 2) sensitivity = std::stod(argv[2]);

    // 2. Setup Sound
    snd_pcm_t *pcm_handle;
    if (configure_pcm_device(pcm_handle, SND_PCM_FORMAT_S16_LE, SAMPLE_RATE, 1) < 0) return -1;

    // 3. Setup FFTW (Done ONCE outside the loop)
    fftw_complex *in = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * BUFFER_SIZE);
    fftw_complex *out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * BUFFER_SIZE);
    fftw_plan plan = fftw_plan_dft_1d(BUFFER_SIZE, in, out, FFTW_FORWARD, FFTW_MEASURE);

    // 4. Setup Matrix
    RGBMatrix::Options options;
    options.rows = 32;
    options.cols = 32;
    options.chain_length = 3;
    options.parallel = 3;
    options.hardware_mapping = "regular";
    rgb_matrix::RuntimeOptions rOptions;
    rOptions.gpio_slowdown = 2;
    RGBMatrix *matrix = RGBMatrix::CreateFromOptions(options, rOptions);
    matrix->SetBrightness(brightness);

    // 5. Detection Variables
    std::vector<short> audio_buffer(BUFFER_SIZE);
    std::deque<double> history;
    const int history_limit = 43; // ~1 second of audio history
    
    std::cout << "Running... Press Ctrl+C to stop." << std::endl;

    while (keep_running) {
        if (snd_pcm_readi(pcm_handle, audio_buffer.data(), BUFFER_SIZE) < 0) {
            snd_pcm_prepare(pcm_handle);
            continue;
        }

        // Apply Hann Window and load FFT
        for (int i = 0; i < BUFFER_SIZE; i++) {
            double window = 0.5 * (1 - cos((2 * M_PI * i) / (BUFFER_SIZE - 1)));
            in[i][0] = audio_buffer[i] * window;
            in[i][1] = 0.0;
        }

        fftw_execute(plan);

        // Analyze Bass Range (Bins 1-4 cover approx 40Hz - 170Hz)
        double current_bass_energy = 0;
        for (int i = 1; i <= 4; i++) {
            double mag = sqrt(out[i][0] * out[i][0] + out[i][1] * out[i][1]);
            current_bass_energy += mag;
        }
        current_bass_energy /= 4.0;

        // Calculate Moving Average
        double avg_bass_energy = 0;
        if (!history.empty()) {
            for (double val : history) avg_bass_energy += val;
            avg_bass_energy /= history.size();
        }

        // Trigger Logic: Spike must be > average * sensitivity
        if (current_bass_energy > (avg_bass_energy * sensitivity) && current_bass_energy > 1000) {
            matrix->Fill(255, 255, 255); 
        } else {
            matrix->Fill(0, 0, 0);
        }

        // Update History
        history.push_back(current_bass_energy);
        if (history.size() > history_limit) history.pop_front();
    }

    // Cleanup
    matrix->Clear();
    delete matrix;
    fftw_destroy_plan(plan);
    fftw_free(in);
    fftw_free(out);
    snd_pcm_close(pcm_handle);
    return 0;
}