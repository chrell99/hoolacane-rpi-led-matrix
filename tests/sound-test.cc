#include <iostream>
#include <alsa/asoundlib.h>
#include <cmath>
#include <vector>

#define PCM_DEVICE "hw:0,0"  // Default device (change if necessary)

int main() {
    snd_pcm_t *pcm_handle;
    snd_pcm_hw_params_t *params;
    snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;  // 16-bit little-endian signed samples
    unsigned int rate = 44100;  // 44.1 kHz sample rate (CD quality)
    int channels = 1;  // Mono channel
    int buffer_size = 1024;  // Buffer size (number of frames)

    // Open PCM device for recording
    if (snd_pcm_open(&pcm_handle, PCM_DEVICE, SND_PCM_STREAM_CAPTURE, 0) < 0) {
        std::cerr << "Failed to open PCM device" << std::endl;
        return -1;
    }

    // Allocate hardware parameters object
    snd_pcm_hw_params_alloca(&params);

    // Initialize hardware parameters
    if (snd_pcm_hw_params_any(pcm_handle, params) < 0) {
        std::cerr << "Failed to initialize hardware parameters" << std::endl;
        return -1;
    }

    // Set the desired hardware parameters
    if (snd_pcm_hw_params_set_access(pcm_handle, params, SND_PCM_ACCESS_RW_INTERLEAVED) < 0) {
        std::cerr << "Failed to set access type" << std::endl;
        return -1;
    }

    if (snd_pcm_hw_params_set_format(pcm_handle, params, format) < 0) {
        std::cerr << "Failed to set format" << std::endl;
        return -1;
    }

    if (snd_pcm_hw_params_set_rate_near(pcm_handle, params, &rate, 0) < 0) {
        std::cerr << "Failed to set sample rate" << std::endl;
        return -1;
    }

    if (snd_pcm_hw_params_set_channels(pcm_handle, params, channels) < 0) {
        std::cerr << "Failed to set channels" << std::endl;
        return -1;
    }

    // Apply the hardware parameters
    if (snd_pcm_hw_params(pcm_handle, params) < 0) {
        std::cerr << "Failed to apply hardware parameters" << std::endl;
        return -1;
    }

    // Prepare PCM device for capturing
    if (snd_pcm_prepare(pcm_handle) < 0) {
        std::cerr << "Failed to prepare PCM device" << std::endl;
        return -1;
    }

    // Create a buffer to hold the audio samples
    std::vector<short> buffer(buffer_size);

    while (true) {
        // Read samples from the sound card
        int frames = snd_pcm_readi(pcm_handle, buffer.data(), buffer_size);
        if (frames < 0) {
            std::cerr << "Error reading audio data" << std::endl;
            break;
        }

        // Calculate the RMS (Root Mean Square) amplitude of the audio buffer
        double sum = 0.0;
        for (int i = 0; i < frames; ++i) {
            sum += buffer[i] * buffer[i];
        }

        double rms_amplitude = std::sqrt(sum / frames);

        // Output the RMS amplitude
        std::cout << "Amplitude (RMS): " << rms_amplitude << std::flush;
    }

    // Close the PCM device
    snd_pcm_close(pcm_handle);
    return 0;
}
