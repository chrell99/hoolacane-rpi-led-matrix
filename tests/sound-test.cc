#include <iostream>
#include <alsa/asoundlib.h>
#include <cmath>
#include <cstdlib>
#include <csignal>  // For signal handling

using namespace std;

#define PCM_DEVICE "default"
#define BUFFER_SIZE 1024
#define SAMPLE_RATE 44100
#define CHANNELS 1
#define FORMAT SND_PCM_FORMAT_S16_LE

snd_pcm_t *pcm_handle = nullptr;  // Global variable for PCM handle
short *buffer = nullptr;          // Global buffer

// Signal handler for SIGINT (Ctrl+C)
void handle_sigint(int sig) {
    cout << "\nCaught signal " << sig << ", cleaning up and exiting..." << endl;
    if (buffer) {
        free(buffer);
    }
    if (pcm_handle) {
        snd_pcm_close(pcm_handle);
    }
    exit(0);  // Exit gracefully
}

int main() {
    // Set up signal handling for SIGINT (Ctrl+C)
    signal(SIGINT, handle_sigint);

    snd_pcm_hw_params_t *params;
    int err;

    // Open the PCM device for capture
    err = snd_pcm_open(&pcm_handle, PCM_DEVICE, SND_PCM_STREAM_CAPTURE, 0);
    if (err < 0) {
        cerr << "ERROR: Unable to open PCM device: " << snd_strerror(err) << endl;
        return -1;
    }

    snd_pcm_hw_params_alloca(&params);

    err = snd_pcm_hw_params_any(pcm_handle, params);
    if (err < 0) {
        cerr << "ERROR: Unable to set parameters: " << snd_strerror(err) << endl;
        return -1;
    }

    err = snd_pcm_hw_params_set_format(pcm_handle, params, FORMAT);
    if (err < 0) {
        cerr << "ERROR: Unable to set format: " << snd_strerror(err) << endl;
        return -1;
    }

    err = snd_pcm_hw_params_set_rate_near(pcm_handle, params, &SAMPLE_RATE, 0);
    if (err < 0) {
        cerr << "ERROR: Unable to set rate: " << snd_strerror(err) << endl;
        return -1;
    }

    err = snd_pcm_hw_params_set_channels(pcm_handle, params, CHANNELS);
    if (err < 0) {
        cerr << "ERROR: Unable to set channels: " << snd_strerror(err) << endl;
        return -1;
    }

    err = snd_pcm_hw_params(pcm_handle, params);
    if (err < 0) {
        cerr << "ERROR: Unable to set parameters: " << snd_strerror(err) << endl;
        return -1;
    }

    buffer = (short *)malloc(BUFFER_SIZE * sizeof(short));

    while (true) {
        err = snd_pcm_readi(pcm_handle, buffer, BUFFER_SIZE);
        if (err == -EPIPE) {
            cerr << "Buffer Underrun!" << endl;
            snd_pcm_prepare(pcm_handle);
        } else if (err < 0) {
            cerr << "ERROR: Read failed: " << snd_strerror(err) << endl;
            break;
        }

        // Calculate the amplitude
        double amplitude = 0;
        for (int i = 0; i < BUFFER_SIZE; i++) {
            amplitude += std::abs(buffer[i]);
        }

        amplitude /= BUFFER_SIZE;  // Get average amplitude

        cout << "Amplitude: " << amplitude << endl;
    }

    free(buffer);
    snd_pcm_close(pcm_handle);
    return 0;
}
