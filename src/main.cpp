#include "common.hpp"

int runCLI(CPrefs &prefs);

#ifdef _WIN32
#include "gui.hpp"
#endif

int main(int argc, char *argv[])
{
	int ret = 0;
	CPrefs prefs(argc, argv, &ret);
	if (ret != ALL_OK)
		return ret;

#ifdef _WIN32
	if (prefs.guiMode)
		return runGUI(prefs);
#endif

	return runCLI(prefs);
}

int runCLI(CPrefs &prefs)
{
	ma_result result;

	SyncWaveContext ctxt{};
	result = ma_pcm_rb_init(ma_format_f32, DEFAULT_CHANNELS, DEFAULT_BUFFER_FRAMES, NULL, NULL, &(ctxt.buffer));
	if (result != MA_SUCCESS)
	{
		crit("Error: %s", ma_result_description(result));
		return -1;
	}
	ctxt.frameSizeInBytes = ma_get_bytes_per_frame(ma_format_f32, DEFAULT_CHANNELS);

	/* In "delay default" mode, set up dual-buffer so the default device
	   gets the delay and the secondary device plays immediately. */
	if (prefs.delayDefault && prefs.delayOffsetMs > 0)
	{
		result = ma_pcm_rb_init(ma_format_f32, DEFAULT_CHANNELS,
								DEFAULT_BUFFER_FRAMES, NULL, NULL,
								&ctxt.defaultBuffer);
		if (result != MA_SUCCESS)
		{
			crit("Error initializing default buffer: %s", ma_result_description(result));
			return -1;
		}
		ctxt.dualBufferMode = true;

		ma_uint32 delayFrames = static_cast<ma_uint32>(
			(static_cast<ma_uint64>(prefs.delayOffsetMs) * DEFAULT_SAMPLE_RATE) / 1000);
		if (!applyDelayOffsetToBuffer(&ctxt.defaultBuffer,
									  ctxt.frameSizeInBytes, delayFrames))
		{
			crit("Failed to apply delay offset to default buffer.");
			return -1;
		}
		ctxt.defaultTargetDelayFrames.store(delayFrames, std::memory_order_relaxed);
	}
	else if (prefs.delayOffsetMs > 0)
	{
		/* Original behaviour: delay the output (secondary) device */
		ma_uint32 delayFrames = static_cast<ma_uint32>(
			(static_cast<ma_uint64>(prefs.delayOffsetMs) * DEFAULT_SAMPLE_RATE) / 1000);
		if (!applyDelayOffset(&ctxt, delayFrames))
		{
			crit("Failed to apply delay offset.");
			return -1;
		}
	}

	ma_device pdevice;
	ma_device cdevice;
	ma_device_config pDeviceConfig = prefs.pDeviceConfig, cDeviceConfig = prefs.cDeviceConfig;
	pDeviceConfig.pUserData = &ctxt;
	cDeviceConfig.pUserData = &ctxt;

	result = ma_device_init(NULL, &pDeviceConfig, &pdevice);
	if (result != MA_SUCCESS)
	{
		crit("Error: %s", ma_result_description(result));
		return -1;
	}
	UninitDeviceOnExit playbackCleanup(&pdevice);

	ma_backend backends[] = {
			ma_backend_wasapi};
	result = ma_device_init_ex(backends, sizeof(backends) / sizeof(backends[0]), NULL, &cDeviceConfig, &cdevice);
	if (result != MA_SUCCESS)
	{
		crit("Error: %s", ma_result_description(result));
		return -1;
	}
	UninitDeviceOnExit loopbackCleanup(&cdevice);

	/* Optional: init default-device playback in dual-buffer mode */
	ma_device ddevice;
	UninitDeviceOnExit *defaultCleanup = nullptr;
	if (ctxt.dualBufferMode)
	{
		ma_device_config dCfg = prefs.pDefaultDeviceConfig;
		dCfg.pUserData = &ctxt;
		result = ma_device_init(NULL, &dCfg, &ddevice);
		if (result != MA_SUCCESS)
		{
			crit("Error init default device: %s", ma_result_description(result));
			return -1;
		}
		defaultCleanup = new UninitDeviceOnExit(&ddevice);
	}

	result = ma_device_start(&cdevice);
	if (result != MA_SUCCESS)
	{
		crit("Error: %s", ma_result_description(result));
		delete defaultCleanup;
		return -1;
	}

	result = ma_device_start(&pdevice);
	if (result != MA_SUCCESS)
	{
		crit("Error: %s", ma_result_description(result));
		delete defaultCleanup;
		return -1;
	}

	if (ctxt.dualBufferMode)
	{
		result = ma_device_start(&ddevice);
		if (result != MA_SUCCESS)
		{
			crit("Error starting default device: %s", ma_result_description(result));
			delete defaultCleanup;
			return -1;
		}
	}

	std::cout << "Press Enter to stop..." << std::endl;
	if (prefs.delayOffsetMs > 0)
	{
		std::cout << "Delay offset: " << prefs.delayOffsetMs << " ms"
				  << " (applied to " << (prefs.delayDefault ? "default" : "output") << " device)"
				  << std::endl;
	}
	getchar();

	delete defaultCleanup;
	if (ctxt.dualBufferMode)
		ma_pcm_rb_uninit(&ctxt.defaultBuffer);
	return 0;
}