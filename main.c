#include "ray-lib/raylib.h"
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include <stdio.h>
#include <limits.h>
#include <math.h>
#include <complex.h>

#define SAMPLE_RATE 44000
#define CHANNELS 1
#define BUFFER_SIZE 1024
#define WINDOW_WIDTH 1200
#define WINDOW_HEIGHT 800
#define CELL_WIDTH 2
#define FFT_SIZE BUFFER_SIZE

typedef enum
{
    STATE_STOPPED,
    STATE_RECORDING,
    STATE_PAUSED
} RecorderState;

typedef enum
{
    FILTER_NONE,
    FILTER_LOW_PASS,
    FILTER_HIGH_PASS
} FilterType;

typedef struct
{
    FILE *file;
    ma_encoder encoder;
    RecorderState state;
    float sampleBuffer[BUFFER_SIZE * CHANNELS];
    int bufferIndex;
    complex double fftBuffer[FFT_SIZE * CHANNELS];
    bool beatDetected;
    FilterType currentFilter;
    float cutoffFrequency;
} RecordingContext;

// Hann window function
void apply_window(float *buffer, int size)
{
    for (int i = 0; i < size; i++)
    {
        float t = (float)i / (size - 1);
        float hann = 0.5f * (1 - cosf(2 * M_PI * t));
        buffer[i] *= hann;
    }
}

void fft(complex double *input, complex double *output, int n)
{
    if (n <= 1)
    {
        output[0] = input[0];
        return;
    }

    complex double even[n / 2];
    complex double odd[n / 2];

    for (int i = 0; i < n / 2; ++i)
    {
        even[i] = input[i * 2];
        odd[i] = input[i * 2 + 1];
    }

    complex double even_output[n / 2];
    complex double odd_output[n / 2];

    fft(even, even_output, n / 2);
    fft(odd, odd_output, n / 2);

    for (int i = 0; i < n / 2; ++i)
    {
        complex double t = cexp(-2.0 * I * M_PI * i / n) * odd_output[i];
        output[i] = even_output[i] + t;
        output[i + n / 2] = even_output[i] - t;
    }
}

void fft_buffer(RecordingContext *context)
{
    if (context == NULL)
        return;

    complex double fftInput[FFT_SIZE];
    float windowedBuffer[FFT_SIZE];

    for (int i = 0; i < FFT_SIZE; ++i)
    {
        windowedBuffer[i] = context->sampleBuffer[i];
    }

    apply_window(windowedBuffer, FFT_SIZE);

    for (int i = 0; i < FFT_SIZE; ++i)
    {
        fftInput[i] = (complex double)windowedBuffer[i];
    }

    fft(fftInput, context->fftBuffer, FFT_SIZE);

    // Apply filter effect to FFT buffer
    if (context->currentFilter != FILTER_NONE)
    {
        int cutoffIndex = (int)((context->cutoffFrequency / SAMPLE_RATE) * FFT_SIZE);
        for (int i = 0; i < FFT_SIZE / 2; ++i)
        {
            if ((context->currentFilter == FILTER_LOW_PASS && i > cutoffIndex) ||
                (context->currentFilter == FILTER_HIGH_PASS && i < cutoffIndex))
            {
                context->fftBuffer[i] *= 0.1;
            }
        }
    }
}

void apply_low_pass_filter(float *buffer, int buffer_size, float cutoff_frequency)
{
    float rc = 1.0f / (cutoff_frequency * 2 * M_PI);
    float dt = 1.0f / SAMPLE_RATE;
    float alpha = dt / (rc + dt);

    float filtered_value = buffer[0];
    for (int i = 1; i < buffer_size; i++)
    {
        filtered_value = filtered_value + alpha * (buffer[i] - filtered_value);
        buffer[i] = filtered_value;
    }
}

void apply_high_pass_filter(float *buffer, int buffer_size, float cutoff_frequency)
{
    float rc = 1.0f / (cutoff_frequency * 2 * M_PI);
    float dt = 1.0f / SAMPLE_RATE;
    float alpha = rc / (rc + dt);

    float prev_value = buffer[0];
    for (int i = 1; i < buffer_size; i++)
    {
        float filtered_value = alpha * (buffer[i - 1] + buffer[i] - prev_value);
        prev_value = buffer[i];
        buffer[i] = filtered_value;
    }
}

void data_callback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount)
{
    RecordingContext *context = (RecordingContext *)pDevice->pUserData;
    if (context == NULL || context->file == NULL || context->state != STATE_RECORDING)
    {
        return;
    }

    ma_uint64 framesWritten;
    ma_encoder_write_pcm_frames(&context->encoder, pInput, frameCount, &framesWritten);

    const float *inputSamples = (const float *)pInput;
    for (ma_uint32 i = 0; i < frameCount * CHANNELS; ++i)
    {
        context->sampleBuffer[context->bufferIndex] = inputSamples[i];
        context->bufferIndex = (context->bufferIndex + 1) % (BUFFER_SIZE * CHANNELS);
    }

    switch (context->currentFilter)
    {
    case FILTER_LOW_PASS:
        apply_low_pass_filter(context->sampleBuffer, BUFFER_SIZE, context->cutoffFrequency);
        break;
    case FILTER_HIGH_PASS:
        apply_high_pass_filter(context->sampleBuffer, BUFFER_SIZE, context->cutoffFrequency);
        break;
    default:
        break;
    }

    fft_buffer(context);
}

int main(void)
{
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Enhanced Audio Recorder");
    SetTargetFPS(60);

    RecordingContext context = {0};
    context.state = STATE_STOPPED;
    context.currentFilter = FILTER_NONE;
    context.cutoffFrequency = 1000.0f;

    ma_device_config deviceConfig;
    ma_device device;
    ma_result result;

    deviceConfig = ma_device_config_init(ma_device_type_capture);
    deviceConfig.capture.format = ma_format_f32;
    deviceConfig.capture.channels = CHANNELS;
    deviceConfig.sampleRate = SAMPLE_RATE;
    deviceConfig.dataCallback = data_callback;
    deviceConfig.pUserData = &context;

    result = ma_device_init(NULL, &deviceConfig, &device);
    if (result != MA_SUCCESS)
    {
        fprintf(stderr, "Failed to initialize capture device.\n");
        return -1;
    }

    while (!WindowShouldClose())
    {
        if (IsKeyPressed(KEY_R))
        {
            if (context.state == STATE_STOPPED)
            {
                context.file = fopen("recording.raw", "wb");
                if (context.file == NULL)
                {
                    fprintf(stderr, "Failed to open file for writing.\n");
                    continue;
                }

                ma_encoder_config encoderConfig = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, CHANNELS, SAMPLE_RATE);
                result = ma_encoder_init_file("recording.wav", &encoderConfig, &context.encoder);
                if (result != MA_SUCCESS)
                {
                    fprintf(stderr, "Failed to initialize encoder.\n");
                    fclose(context.file);
                    continue;
                }

                context.state = STATE_RECORDING;
                ma_device_start(&device);
            }
            else if (context.state == STATE_PAUSED)
            {
                context.state = STATE_RECORDING;
            }
        }
        else if (IsKeyPressed(KEY_P))
        {
            if (context.state == STATE_RECORDING)
            {
                context.state = STATE_PAUSED;
            }
        }
        else if (IsKeyPressed(KEY_S))
        {
            if (context.state == STATE_RECORDING || context.state == STATE_PAUSED)
            {
                context.state = STATE_STOPPED;
                ma_device_stop(&device);
                ma_encoder_uninit(&context.encoder);
                fclose(context.file);
                context.file = NULL;
            }
        }
        else if (IsKeyPressed(KEY_L))
        {
            context.currentFilter = FILTER_LOW_PASS;
        }
        else if (IsKeyPressed(KEY_H))
        {
            context.currentFilter = FILTER_HIGH_PASS;
        }
        else if (IsKeyPressed(KEY_N))
        {
            context.currentFilter = FILTER_NONE;
        }

        // Adjust cutoff frequency
        if (IsKeyDown(KEY_UP))
        {
            context.cutoffFrequency += 10.0f;
        }
        else if (IsKeyDown(KEY_DOWN))
        {
            context.cutoffFrequency -= 10.0f;
            if (context.cutoffFrequency < 10.0f)
                context.cutoffFrequency = 10.0f;
        }

        BeginDrawing();
        ClearBackground(RAYWHITE);
        if (context.state == STATE_RECORDING)
        {
            // Draw waveform
            for (int i = 0; i < BUFFER_SIZE; ++i)
            {
                float normalized_value = context.sampleBuffer[i];
                if (normalized_value >= 0)
                {
                    DrawRectangle(CELL_WIDTH * i, WINDOW_HEIGHT / 4 - normalized_value * WINDOW_HEIGHT / 4, CELL_WIDTH, normalized_value * WINDOW_HEIGHT / 4, RED);
                }
                else
                {
                    DrawRectangle(CELL_WIDTH * i, WINDOW_HEIGHT / 4, CELL_WIDTH, -normalized_value * WINDOW_HEIGHT / 4, RED);
                }
            }

            float maxMagnitude = 0;
            float peakFreq = 0;
            for (int i = 0; i < FFT_SIZE; ++i)
            {
                float magnitude = cabs(context.fftBuffer[i]);
                float freq = (i * SAMPLE_RATE) / FFT_SIZE;

                if ((context.currentFilter == FILTER_NONE) ||
                    (context.currentFilter == FILTER_LOW_PASS && freq <= context.cutoffFrequency) ||
                    (context.currentFilter == FILTER_HIGH_PASS && freq >= context.cutoffFrequency))
                {
                    if (magnitude > maxMagnitude)
                    {
                        maxMagnitude = magnitude;
                        peakFreq = freq;
                    }
                }
            }

            for (int i = 0; i < FFT_SIZE; ++i)
            {
                float magnitude = cabs(context.fftBuffer[i]);
                float normalizedMagnitude = magnitude / maxMagnitude;
                float freq = (i * SAMPLE_RATE) / FFT_SIZE;

                if ((context.currentFilter == FILTER_NONE) ||
                    (context.currentFilter == FILTER_LOW_PASS && freq <= context.cutoffFrequency) ||
                    (context.currentFilter == FILTER_HIGH_PASS && freq >= context.cutoffFrequency))
                {
                    DrawRectangle(CELL_WIDTH * i, 3 * WINDOW_HEIGHT / 4 - normalizedMagnitude * WINDOW_HEIGHT / 4,
                                  CELL_WIDTH, normalizedMagnitude * WINDOW_HEIGHT / 4, BLUE);
                }
            }

            char freqText[32];
            snprintf(freqText, sizeof(freqText), "Peak Frequency: %.2f Hz", peakFreq);
            DrawText(freqText, 10, 3 * WINDOW_HEIGHT / 4 + 10, 20, BLACK);

            const char *filterName;
            switch (context.currentFilter)
            {
            case FILTER_LOW_PASS:
                filterName = "Low-Pass";
                break;
            case FILTER_HIGH_PASS:
                filterName = "High-Pass";
                break;
            default:
                filterName = "None";
            }
            char filterInfo[64];
            snprintf(filterInfo, sizeof(filterInfo), "Filter: %s, Cutoff: %.2f Hz", filterName, context.cutoffFrequency);
            DrawText(filterInfo, 10, WINDOW_HEIGHT - 30, 20, BLACK);
        }

        EndDrawing();
    }

    if (context.state != STATE_STOPPED)
    {
        ma_device_stop(&device);
        ma_encoder_uninit(&context.encoder);
        if (context.file)
        {
            fclose(context.file);
        }
    }

    ma_device_uninit(&device);
    CloseWindow();
    return 0;
}
