#pragma once
constexpr int ALL_OK = 1;
constexpr int NOK = -1;
constexpr int END = 0;

constexpr char GREEN[] = "\033[32m";

constexpr int DEFAULT_SAMPLE_RATE = 44100;
constexpr int DEFAULT_CHANNELS = 2;
constexpr int DEFAULT_OFFSET_MS = 0;
constexpr int MAX_OFFSET_MS = 1000;
constexpr int DEFAULT_BUFFER_FRAMES = 16 * 48000;