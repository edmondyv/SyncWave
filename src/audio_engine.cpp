#include "common.hpp"
#include <cmath>

/* ── Helpers for in-place audio processing ── */

static void applyVolume(float *frames, ma_uint32 frameCount, ma_uint32 channels, float volume)
{
	if (volume >= 1.0f)
		return;
	ma_uint32 sampleCount = frameCount * channels;
	for (ma_uint32 i = 0; i < sampleCount; ++i)
		frames[i] *= volume;
}

static void applyChannelRouting(float *frames, ma_uint32 frameCount, int mode)
{
	if (mode == static_cast<int>(ChannelMode::Both))
		return;
	for (ma_uint32 i = 0; i < frameCount; ++i)
	{
		float *L = &frames[i * 2];
		float *R = &frames[i * 2 + 1];
		if (mode == static_cast<int>(ChannelMode::LeftOnly))
			*R = *L;
		else if (mode == static_cast<int>(ChannelMode::RightOnly))
			*L = *R;
	}
}

/* Reinitialise a second-order LPF if the target cutoff changed.
   Returns true when the filter is active and should be applied. */
static bool ensureLPF(ma_lpf2 *lpf, int *activeCutoff, int targetHz)
{
	if (targetHz < MIN_FILTER_HZ)
	{
		*activeCutoff = 0;
		return false;
	}
	if (*activeCutoff != targetHz)
	{
		ma_lpf2_config cfg = ma_lpf2_config_init(ma_format_f32, DEFAULT_CHANNELS,
												  DEFAULT_SAMPLE_RATE,
												  static_cast<double>(targetHz), 0.707);
		if (*activeCutoff == 0)
			ma_lpf2_init(&cfg, NULL, lpf);
		else
			ma_lpf2_reinit(&cfg, lpf);
		*activeCutoff = targetHz;
	}
	return true;
}

/* Same for HPF */
static bool ensureHPF(ma_hpf2 *hpf, int *activeCutoff, int targetHz)
{
	if (targetHz < MIN_FILTER_HZ)
	{
		*activeCutoff = 0;
		return false;
	}
	if (*activeCutoff != targetHz)
	{
		ma_hpf2_config cfg = ma_hpf2_config_init(ma_format_f32, DEFAULT_CHANNELS,
												  DEFAULT_SAMPLE_RATE,
												  static_cast<double>(targetHz), 0.707);
		if (*activeCutoff == 0)
			ma_hpf2_init(&cfg, NULL, hpf);
		else
			ma_hpf2_reinit(&cfg, hpf);
		*activeCutoff = targetHz;
	}
	return true;
}

/* ── Generic read-from-ring-buffer + process helper ── */

static void readAndProcess(ma_pcm_rb *rb, SyncWaveContext *ctx, void *pOutput,
						   ma_uint32 frameCount,
						   std::atomic<ma_uint32> &targetDelay,
						   std::atomic<float> &volume,
						   std::atomic<int> &channelMode,
						   std::atomic<int> &lpfHz, std::atomic<int> &hpfHz,
						   ma_lpf2 *lpf, ma_hpf2 *hpf,
						   int *activeLPF, int *activeHPF)
{
	ma_uint32 availableFrames = ma_pcm_rb_available_read(rb);
	ma_uint32 delay = targetDelay.load(std::memory_order_relaxed);

	/* Adaptive delay management: skip excess frames when ahead of target */
	if (delay > 0 && availableFrames > delay + frameCount * 2)
	{
		ma_uint32 skipFrames = availableFrames - delay - frameCount;
		void *skipBuffer;
		if (ma_pcm_rb_acquire_read(rb, &skipFrames, &skipBuffer) == MA_SUCCESS)
		{
			ma_pcm_rb_commit_read(rb, skipFrames);
			trace("Skipped {} frames to maintain target delay.", skipFrames);
		}
	}

	void *bufferIn;
	ma_uint32 framesToRead = frameCount;
	if (ma_pcm_rb_acquire_read(rb, &framesToRead, &bufferIn) != MA_SUCCESS)
	{
		memset(pOutput, 0, static_cast<size_t>(frameCount) * ctx->frameSizeInBytes);
		return;
	}

	memcpy(pOutput, bufferIn, static_cast<size_t>(framesToRead) * ctx->frameSizeInBytes);
	ma_pcm_rb_commit_read(rb, framesToRead);

	/* Silence-fill any underrun remainder */
	if (framesToRead < frameCount)
	{
		memset(static_cast<char *>(pOutput) + static_cast<size_t>(framesToRead) * ctx->frameSizeInBytes,
			   0, static_cast<size_t>(frameCount - framesToRead) * ctx->frameSizeInBytes);
	}

	float *out = static_cast<float *>(pOutput);

	/* Volume */
	applyVolume(out, frameCount, DEFAULT_CHANNELS, volume.load(std::memory_order_relaxed));

	/* Channel routing */
	applyChannelRouting(out, frameCount, channelMode.load(std::memory_order_relaxed));

	/* Low-pass filter */
	int targetLPF = lpfHz.load(std::memory_order_relaxed);
	if (ensureLPF(lpf, activeLPF, targetLPF))
		ma_lpf2_process_pcm_frames(lpf, out, out, frameCount);

	/* High-pass filter */
	int targetHPF = hpfHz.load(std::memory_order_relaxed);
	if (ensureHPF(hpf, activeHPF, targetHPF))
		ma_hpf2_process_pcm_frames(hpf, out, out, frameCount);
}

/* ── Callbacks ── */

void loopback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount)
{
	SyncWaveContext *ctx = (SyncWaveContext *)pDevice->pUserData;

	/* Write to primary buffer (secondary / output device) */
	{
		void *bufferOut;
		ma_uint32 framesToWrite = frameCount;
		if (ma_pcm_rb_acquire_write(&ctx->buffer, &framesToWrite, &bufferOut) == MA_SUCCESS)
		{
			if (framesToWrite < frameCount)
				trace("Primary buffer full, dropped {} frames.", (frameCount - framesToWrite));
			memcpy(bufferOut, pInput, static_cast<size_t>(framesToWrite) * ctx->frameSizeInBytes);
			ma_pcm_rb_commit_write(&ctx->buffer, framesToWrite);
		}
	}

	/* Write to default-device buffer when in dual-buffer mode */
	if (ctx->dualBufferMode)
	{
		void *bufferOut;
		ma_uint32 framesToWrite = frameCount;
		if (ma_pcm_rb_acquire_write(&ctx->defaultBuffer, &framesToWrite, &bufferOut) == MA_SUCCESS)
		{
			if (framesToWrite < frameCount)
				trace("Default buffer full, dropped {} frames.", (frameCount - framesToWrite));
			memcpy(bufferOut, pInput, static_cast<size_t>(framesToWrite) * ctx->frameSizeInBytes);
			ma_pcm_rb_commit_write(&ctx->defaultBuffer, framesToWrite);
		}
	}
}

void playback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount)
{
	SyncWaveContext *ctx = (SyncWaveContext *)pDevice->pUserData;
	readAndProcess(&ctx->buffer, ctx, pOutput, frameCount,
				   ctx->targetDelayFrames,
				   ctx->outputVolume,
				   ctx->outputChannelMode,
				   ctx->outputLowPassHz, ctx->outputHighPassHz,
				   &ctx->outputLPF, &ctx->outputHPF,
				   &ctx->activeOutputLPFHz, &ctx->activeOutputHPFHz);
	(void)pInput;
}

void playbackDefault(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount)
{
	SyncWaveContext *ctx = (SyncWaveContext *)pDevice->pUserData;
	readAndProcess(&ctx->defaultBuffer, ctx, pOutput, frameCount,
				   ctx->defaultTargetDelayFrames,
				   ctx->defaultDeviceVolume,
				   ctx->defaultChannelMode,
				   ctx->defaultLowPassHz, ctx->defaultHighPassHz,
				   &ctx->defaultLPF, &ctx->defaultHPF,
				   &ctx->activeDefaultLPFHz, &ctx->activeDefaultHPFHz);
	(void)pInput;
}

/* ── Delay offset helpers ── */

bool applyDelayOffsetToBuffer(ma_pcm_rb *buffer, ma_uint32 frameSizeInBytes, ma_uint32 delayFrames)
{
	if (delayFrames == 0)
		return true;

	void *pBuffer;
	ma_uint32 framesToWrite = delayFrames;
	if (ma_pcm_rb_acquire_write(buffer, &framesToWrite, &pBuffer) != MA_SUCCESS)
	{
		warn("Failed to apply delay offset: could not acquire write on buffer");
		return false;
	}

	memset(pBuffer, 0, static_cast<size_t>(framesToWrite) * frameSizeInBytes);
	ma_pcm_rb_commit_write(buffer, framesToWrite);

	if (framesToWrite < delayFrames)
		warn("Could only apply {} of {} delay frames.", framesToWrite, delayFrames);

	info("Applied delay offset: {} frames ({} ms at {} Hz).",
		 framesToWrite,
		 (framesToWrite * 1000) / DEFAULT_SAMPLE_RATE,
		 DEFAULT_SAMPLE_RATE);

	return true;
}

bool applyDelayOffset(SyncWaveContext *ctx, ma_uint32 delayFrames)
{
	bool ok = applyDelayOffsetToBuffer(&ctx->buffer, ctx->frameSizeInBytes, delayFrames);
	if (ok)
		ctx->targetDelayFrames.store(delayFrames, std::memory_order_relaxed);
	return ok;
}
