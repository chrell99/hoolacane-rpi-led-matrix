#include <iostream>
#include <alsa/asoundlib.h>
#include <cmath>
#include <vector>
#include <fftw3.h>

#define PCM_DEVICE "hw:1,0"  // USB Dongle audio input
#define SAMPLE_RATE 44100    // 44.1 kHz sample rate
#define BUFFER_SIZE 1024     // FFT buffer size (must be power of 2)

// g++ fft_audio.cpp -o fft_audio -lfftw3 -lasound -lm

// Function to compute FFT and print frequency bins and amplitudes
void computeFFT(std::vector<short>& buffer) {
    int N = BUFFER_SIZE;
    fftw_complex *in, *out;
    fftw_plan plan;

    // Allocate FFT input and output arrays
    in = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * N);
    out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * N);
    
    // Create FFTW plan
    plan = fftw_plan_dft_1d(N, in, out, FFTW_FORWARD, FFTW_ESTIMATE);

    // Convert input audio data to FFTW input format
    for (int i = 0; i < N; i++) {
        in[i][0] = buffer[i];  // Real part (audio sample)
        in[i][1] = 0.0;        // Imaginary part (set to zero)
    }

    // Execute FFT
    fftw_execute(plan);

    // Print frequency bins and amplitudes
    std::cout << "\nFFT Results:\n";
    for (int i = 0; i < N / 2; i++) {  // Only first half due to Nyquist theorem
        double frequency = (double)i * SAMPLE_RATE / N;
        double magnitude = sqrt(out[i][0] * out[i][0] + out[i][1] * out[i][1]);

        std::cout << "Bin " << i << " | Frequency: " << frequency << " Hz | Amplitude: " << magnitude << std::endl;
    }

    // Cleanup
    fftw_destroy_plan(plan);
    fftw_free(in);
    fftw_free(out);
}

int main() {
    snd_pcm_t *pcm_handle;
    snd_pcm_hw_params_t *params;
    snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;
    unsigned int rate = SAMPLE_RATE;
    int channels = 1;
    int buffer_size = BUFFER_SIZE;

    // Open ALSA PCM device for recording
    if (snd_pcm_open(&pcm_handle, PCM_DEVICE, SND_PCM_STREAM_CAPTURE, 0) < 0) {
        std::cerr << "Failed to open PCM device" << std::endl;
        return -1;
    }

    snd_pcm_hw_params_alloca(&params);
    if (snd_pcm_hw_params_any(pcm_handle, params) < 0 ||
        snd_pcm_hw_params_set_access(pcm_handle, params, SND_PCM_ACCESS_RW_INTERLEAVED) < 0 ||
        snd_pcm_hw_params_set_format(pcm_handle, params, format) < 0 ||
        snd_pcm_hw_params_set_rate_near(pcm_handle, params, &rate, 0) < 0 ||
        snd_pcm_hw_params_set_channels(pcm_handle, params, channels) < 0 ||
        snd_pcm_hw_params(pcm_handle, params) < 0) {
        std::cerr << "Failed to configure PCM device" << std::endl;
        return -1;
    }

    if (snd_pcm_prepare(pcm_handle) < 0) {
        std::cerr << "Failed to prepare PCM device" << std::endl;
        return -1;
    }

    std::vector<short> buffer(buffer_size);

    while (true) {
        int frames = snd_pcm_readi(pcm_handle, buffer.data(), buffer_size);
        if (frames < 0) {
            std::cerr << "Error reading audio data" << std::endl;
            break;
        }
        computeFFT(buffer);
    }

    snd_pcm_close(pcm_handle);
    return 0;
}
