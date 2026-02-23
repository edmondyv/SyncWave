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

	/* Apply delay offset for Bluetooth sync compensation */
	if (prefs.delayOffsetMs > 0)
	{
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

	result = ma_device_start(&cdevice);
	if (result != MA_SUCCESS)
	{
		crit("Error: %s", ma_result_description(result));
		return -1;
	}

	result = ma_device_start(&pdevice);
	if (result != MA_SUCCESS)
	{
		crit("Error: %s", ma_result_description(result));
		return -1;
	}

	std::cout << "Press Enter to stop..." << std::endl;
	if (prefs.delayOffsetMs > 0)
		std::cout << "Delay offset: " << prefs.delayOffsetMs << " ms" << std::endl;
	getchar();
	return 0;
}