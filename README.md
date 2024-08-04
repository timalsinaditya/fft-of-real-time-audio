# Frequency analysis of real time audio singal

## Concepts Used

```bash 
1. Fast Fourier Transform
2. Low Pass Filter
3. High Pass Filter
4. Hanning Window
``` 
## Installation
Please install [raylib](https://github.com/raysan5/raylib) and run the command below.

```bash
clang main.c -o main -lpthread -lraylib -lm && ./main
```

## Usage

```bash 
1. R - Start recording
2. S - Stop recording
3. P - Pause recording 
4. L - Implement Low Pass Filter
5. H - Implement High Pass Filter
6. N - No filter
7. Up and Down Arrow - To adjust the cutoff frequency 