#include "raylib.h"
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include <stdio.h>
#include <limits.h>
#include <math.h>
#include <complex.h>

#define SAMPLE_RATE 48000
#define CHANNELS 1
#define BUFFER_SIZE 600
#define WINDOW_WIDTH 1200
#define WINDOW_HEIGHT 800
#define CELL_WIDTH 2
#define FFT_SIZE BUFFER_SIZE

typedef enum {
    STATE_STOPPED,
    STATE_RECORDING,
    STATE_PAUSED
} RecorderState;

typedef struct {
    FILE *file;
    ma_encoder encoder;
    RecorderState state;
    int16_t sampleBuffer[BUFFER_SIZE * CHANNELS];
    int bufferIndex;
    complex double fftBuffer[FFT_SIZE * CHANNELS];
} RecordingContext;

void fft(complex double *input, complex double *output, int n) {
    if (n <= 1) {
        output[0] = input[0];
        return;
    }

    complex double even[n / 2];
    complex double odd[n / 2];

    for (int i = 0; i < n / 2; ++i) {
        even[i] = input[i * 2];
        odd[i] = input[i * 2 + 1];
    }

    complex double even_output[n / 2];
    complex double odd_output[n / 2];

    fft(even, even_output, n / 2);
    fft(odd, odd_output, n / 2);

    for (int i = 0; i < n / 2; ++i) {
        complex double t = cexp(-2.0 * I * M_PI * i / n) * odd_output[i];
        output[i] = even_output[i] + t;
        output[i + n / 2] = even_output[i] - t;
        }
}

void fft_buffer(RecordingContext *context) {
    if (context == NULL) return;

    complex double fftInput[FFT_SIZE];
    for (int i = 0; i < FFT_SIZE; ++i) {
        fftInput[i] = (complex double)context->sampleBuffer[i];
    }

    for (int i = 0; i < FFT_SIZE; ++i) {
            fftInput[i] /= INT16_MAX;
        }

    fft(fftInput, context->fftBuffer, FFT_SIZE);
}

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    RecordingContext *context = (RecordingContext*)pDevice->pUserData;
    if (context == NULL || context->file == NULL || context->state != STATE_RECORDING) {
        return;
    }
    ma_uint64 framesWritten;
    ma_encoder_write_pcm_frames(&context->encoder, pInput, frameCount, &framesWritten);
    const int16_t *inputSamples = (const int16_t *)pInput;
    for (ma_uint32 i = 0; i < frameCount * CHANNELS; ++i) {
        context->sampleBuffer[context->bufferIndex] = inputSamples[i];
        context->bufferIndex = (context->bufferIndex + 1) % (BUFFER_SIZE * CHANNELS);
    }
    fft_buffer(context);
}

int main(void) {
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Simple Audio Recorder");
    SetTargetFPS(60);

    RecordingContext context = {0};
    context.state = STATE_STOPPED;

    ma_device_config deviceConfig;
    ma_device device;
    ma_result result;

    deviceConfig = ma_device_config_init(ma_device_type_capture);
    deviceConfig.capture.format = ma_format_s16;
    deviceConfig.capture.channels = CHANNELS;
    deviceConfig.sampleRate = SAMPLE_RATE;
    deviceConfig.dataCallback = data_callback;
    deviceConfig.pUserData = &context;

    result = ma_device_init(NULL, &deviceConfig, &device);
    if (result != MA_SUCCESS) {
        fprintf(stderr, "Failed to initialize capture device.\n");
        return -1;
    }

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_R)) {
            if (context.state == STATE_STOPPED) {
                context.file = fopen("recording.raw", "wb");
                if (context.file == NULL) {
                    fprintf(stderr, "Failed to open file for writing.\n");
                    continue;
                }

                ma_encoder_config encoderConfig = ma_encoder_config_init(ma_encoding_format_wav, ma_format_s16, CHANNELS, SAMPLE_RATE);
                result = ma_encoder_init_file("recording.raw", &encoderConfig, &context.encoder);
                if (result != MA_SUCCESS) {
                    fprintf(stderr, "Failed to initialize encoder.\n");
                    fclose(context.file);
                    continue;
                }

                context.state = STATE_RECORDING;
                ma_device_start(&device);
            } else if (context.state == STATE_PAUSED) {
                context.state = STATE_RECORDING;
            }
        } else if (IsKeyPressed(KEY_P)) {
            if (context.state == STATE_RECORDING) {
                context.state = STATE_PAUSED;
            }
        } else if (IsKeyPressed(KEY_S)) {
            if (context.state == STATE_RECORDING || context.state == STATE_PAUSED) {
                context.state = STATE_STOPPED;
                ma_device_stop(&device);
                ma_encoder_uninit(&context.encoder);
                fclose(context.file);
                context.file = NULL;
            }
        }
        BeginDrawing();
        ClearBackground(RAYWHITE);

        if (context.state == STATE_RECORDING) {
            for (ma_uint32 i = 0; i < BUFFER_SIZE; ++i) {
                if (context.sampleBuffer[i] >= 0) {
                    float normalized_value = context.sampleBuffer[i] / (float)INT16_MAX;
                    DrawRectangle(CELL_WIDTH * i , WINDOW_HEIGHT/4 - normalized_value * WINDOW_HEIGHT/4, CELL_WIDTH, normalized_value * WINDOW_HEIGHT/4, RED);
                }
                else {
                    float normalized_value = context.sampleBuffer[i] / (float)INT16_MIN;
                    DrawRectangle(CELL_WIDTH * i , WINDOW_HEIGHT/4, CELL_WIDTH, normalized_value * WINDOW_HEIGHT/4, RED);
                }
            }

            float maxMagnitude = 0;
            float freq1, freq2;
            for(ma_uint32 i = 0; i < FFT_SIZE; ++i) {
                float magnitude = cabs(context.fftBuffer[i]);
                if (magnitude > maxMagnitude) {
                    maxMagnitude = magnitude;
                    freq1 = (i * SAMPLE_RATE) / FFT_SIZE;
                    freq2 = SAMPLE_RATE - freq1;
                }
            }

            if(freq1 > freq2) {
                    float temp = freq1;
                    freq1 = freq2;
                    freq2 = temp;
                }

            for(ma_uint32 i = 0; i < FFT_SIZE ; ++i) {
                float magnitude = cabs(context.fftBuffer[i]);
                float normalizedMagnitude = magnitude / maxMagnitude;
                DrawRectangle((CELL_WIDTH ) * i, 3* WINDOW_HEIGHT / 4 - normalizedMagnitude * WINDOW_HEIGHT / 4, CELL_WIDTH , normalizedMagnitude * WINDOW_HEIGHT / 4, BLUE);
                char freqText[16];
                snprintf(freqText, sizeof(freqText), "%.2f Hz", freq1);
                DrawText(freqText, 10 , 3* WINDOW_HEIGHT / 4 + 10, 20, BLACK);
               // snprintf(freqText, sizeof(freqText), "%.2f Hz", freq2);
               // DrawText(freqText, 950 , 3* WINDOW_HEIGHT / 4 + 10, 20, BLACK);
            }
        }
        EndDrawing();
    }

    if (context.state != STATE_STOPPED) {
        ma_device_stop(&device);
        ma_encoder_uninit(&context.encoder);
        if (context.file) {
            fclose(context.file);
        }
    }

    ma_device_uninit(&device);
    CloseWindow();
    return 0;
}
