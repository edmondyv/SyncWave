#pragma once

#include <miniaudio.hpp>
#include <constants.hpp>
#include <atomic>

struct SyncWaveContext
{
	ma_pcm_rb buffer;
	ma_uint32 frameSizeInBytes;
	std::atomic<ma_uint32> targetDelayFrames{0};
};

void loopback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount);
void playback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount);

// Pre-fill the ring buffer with silence to introduce a fixed delay
bool applyDelayOffset(SyncWaveContext *ctx, ma_uint32 delayFrames);
