#pragma once
class CPrefs {
	ma_context context;
	ma_device_info* pPlaybackDeviceInfos;
	ma_uint32 playbackDeviceCount;
	ma_device_info* pCaptureDeviceInfos;
	ma_uint32 captureDeviceCount;
	bool contextInit = false;


public:
	ma_device_config pDeviceConfig, cDeviceConfig;
	void* input = NULL;
	int delayOffsetMs = DEFAULT_OFFSET_MS;
	bool guiMode = false;

	CPrefs(int argc, char* argv[], int* result);

	~CPrefs() {
		if(contextInit)
			ma_context_uninit(&context);
		if(input != NULL)
			free(input);
	}

	void listDevices();
	int initDevice(int);

	ma_device_info* getPlaybackDevices() const { return pPlaybackDeviceInfos; }
	ma_uint32 getPlaybackDeviceCount() const { return playbackDeviceCount; }
	ma_context* getContext() { return &context; }
	bool isContextInit() const { return contextInit; }
};
