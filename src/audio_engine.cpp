#include "common.hpp"

void loopback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount)
{
	SyncWaveContext *ctx = (SyncWaveContext *)pDevice->pUserData;
	void *bufferOut;
	ma_uint32 framesToWrite = frameCount;
	if (ma_pcm_rb_acquire_write(&ctx->buffer, &framesToWrite, &bufferOut) != MA_SUCCESS)
	{
		warn("Failed to acquire write on buffer");
		return;
	}
	if (framesToWrite < frameCount)
	{
		trace("Buffer full, dropped {} frames.", (frameCount - framesToWrite));
	}

	memcpy(bufferOut, pInput, static_cast<size_t>(framesToWrite) * ctx->frameSizeInBytes);
	ma_pcm_rb_commit_write(&ctx->buffer, framesToWrite);
}

void playback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount)
{
	SyncWaveContext *ctx = (SyncWaveContext *)pDevice->pUserData;
	ma_uint32 availableFrames = ma_pcm_rb_available_read(&ctx->buffer);
	ma_uint32 targetDelay = ctx->targetDelayFrames.load(std::memory_order_relaxed);

	/*
	 * Adaptive delay management:
	 * If significantly more data is buffered than the target delay,
	 * skip excess frames to reduce latency. This keeps the playback
	 * device in sync when Bluetooth or other variable-latency devices
	 * cause drift.
	 */
	if (targetDelay > 0 && availableFrames > targetDelay + frameCount * 2)
	{
		ma_uint32 skipFrames = availableFrames - targetDelay - frameCount;
		void *skipBuffer;
		if (ma_pcm_rb_acquire_read(&ctx->buffer, &skipFrames, &skipBuffer) == MA_SUCCESS)
		{
			ma_pcm_rb_commit_read(&ctx->buffer, skipFrames);
			trace("Skipped {} frames to maintain target delay.", skipFrames);
		}
	}

	void *bufferIn;
	ma_uint32 framesToRead = frameCount;
	if (ma_pcm_rb_acquire_read(&ctx->buffer, &framesToRead, &bufferIn) != MA_SUCCESS)
	{
		warn("Failed to acquire read on buffer");
		memset(pOutput, 0, static_cast<size_t>(frameCount) * ctx->frameSizeInBytes);
		return;
	}

	memcpy(pOutput, bufferIn, static_cast<size_t>(framesToRead) * ctx->frameSizeInBytes);
	ma_pcm_rb_commit_read(&ctx->buffer, framesToRead);

	/* Fill remainder with silence if buffer underrun */
	if (framesToRead < frameCount)
	{
		memset(static_cast<char *>(pOutput) + static_cast<size_t>(framesToRead) * ctx->frameSizeInBytes,
			   0,
			   static_cast<size_t>(frameCount - framesToRead) * ctx->frameSizeInBytes);
		trace("Buffer underrun, padded {} frames with silence.", (frameCount - framesToRead));
	}

	(void)pInput;
}

bool applyDelayOffset(SyncWaveContext *ctx, ma_uint32 delayFrames)
{
	if (delayFrames == 0)
		return true;

	/* Pre-fill the ring buffer with silence to create the initial delay */
	void *pBuffer;
	ma_uint32 framesToWrite = delayFrames;
	if (ma_pcm_rb_acquire_write(&ctx->buffer, &framesToWrite, &pBuffer) != MA_SUCCESS)
	{
		warn("Failed to apply delay offset: could not acquire write on buffer");
		return false;
	}

	memset(pBuffer, 0, static_cast<size_t>(framesToWrite) * ctx->frameSizeInBytes);
	ma_pcm_rb_commit_write(&ctx->buffer, framesToWrite);

	if (framesToWrite < delayFrames)
	{
		warn("Could only apply {} of {} delay frames.", framesToWrite, delayFrames);
	}

	ctx->targetDelayFrames.store(delayFrames, std::memory_order_relaxed);
	info("Applied delay offset: {} frames ({} ms at {} Hz).",
		 framesToWrite,
		 (framesToWrite * 1000) / DEFAULT_SAMPLE_RATE,
		 DEFAULT_SAMPLE_RATE);

	return true;
}
