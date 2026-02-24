#pragma once

#include <miniaudio.hpp>
#include <constants.hpp>
#include <atomic>

/* Channel routing mode for each output device */
enum class ChannelMode : int
{
	Both = 0,
	LeftOnly = 1,
	RightOnly = 2
};

struct SyncWaveContext
{
	/* Primary ring buffer (used for secondary/output device) */
	ma_pcm_rb buffer;
	/* Secondary ring buffer (used for default device in DelayDefault mode) */
	ma_pcm_rb defaultBuffer;
	bool dualBufferMode = false;

	ma_uint32 frameSizeInBytes;
	std::atomic<ma_uint32> targetDelayFrames{0};
	std::atomic<ma_uint32> defaultTargetDelayFrames{0};

	/* Per-device volume (0.0 – 1.0, applied in playback callbacks) */
	std::atomic<float> outputVolume{1.0f};
	std::atomic<float> defaultDeviceVolume{1.0f};

	/* Channel routing per device */
	std::atomic<int> outputChannelMode{0};  /* ChannelMode cast to int */
	std::atomic<int> defaultChannelMode{0};

	/* Low-pass filter cutoff Hz per device (0 = disabled) */
	std::atomic<int> outputLowPassHz{0};
	std::atomic<int> defaultLowPassHz{0};

	/* High-pass filter cutoff Hz per device (0 = disabled) */
	std::atomic<int> outputHighPassHz{0};
	std::atomic<int> defaultHighPassHz{0};

	/* Runtime filter state (managed by audio thread) */
	ma_lpf2 outputLPF{};
	ma_hpf2 outputHPF{};
	ma_lpf2 defaultLPF{};
	ma_hpf2 defaultHPF{};
	int activeOutputLPFHz = 0;
	int activeOutputHPFHz = 0;
	int activeDefaultLPFHz = 0;
	int activeDefaultHPFHz = 0;
};

void loopback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount);
void playback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount);
void playbackDefault(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount);

/* Pre-fill a ring buffer with silence to introduce a fixed delay */
bool applyDelayOffset(SyncWaveContext *ctx, ma_uint32 delayFrames);
bool applyDelayOffsetToBuffer(ma_pcm_rb *buffer, ma_uint32 frameSizeInBytes, ma_uint32 delayFrames);
