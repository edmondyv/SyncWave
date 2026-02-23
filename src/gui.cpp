#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")

#include "common.hpp"
#include "gui.hpp"

/* ── Control IDs ── */
#define IDC_DEVICE_COMBO  101
#define IDC_OFFSET_SLIDER 102
#define IDC_OFFSET_LABEL  103
#define IDC_START_BTN     104
#define IDC_STOP_BTN      105
#define IDC_STATUS_LABEL  106

/* ── Window dimensions ── */
static constexpr int WIN_W = 420;
static constexpr int WIN_H = 320;

/* ── Shared state ── */
struct GUIState
{
	CPrefs *prefs;
	SyncWaveContext ctxt{};
	ma_device pdevice{};
	ma_device cdevice{};
	bool running = false;
	int selectedDevice = -1;
	int offsetMs = DEFAULT_OFFSET_MS;

	/* Win32 handles */
	HWND hCombo = NULL;
	HWND hSlider = NULL;
	HWND hOffsetLabel = NULL;
	HWND hStartBtn = NULL;
	HWND hStopBtn = NULL;
	HWND hStatusLabel = NULL;
};

static GUIState g_state;

/* ── Helper: update offset label text ── */
static void updateOffsetLabel(HWND hwnd, int ms)
{
	char buf[64];
	snprintf(buf, sizeof(buf), "Delay offset: %d ms", ms);
	SetWindowTextA(g_state.hOffsetLabel, buf);
}

/* ── Helper: set status text ── */
static void setStatus(const char *text)
{
	SetWindowTextA(g_state.hStatusLabel, text);
}

/* ── Start audio ── */
static void startAudio(HWND hwnd)
{
	if (g_state.running)
		return;

	int devIdx = (int)SendMessageA(g_state.hCombo, CB_GETCURSEL, 0, 0);
	if (devIdx == CB_ERR || devIdx < 0)
	{
		setStatus("Please select an output device.");
		return;
	}

	g_state.selectedDevice = devIdx;
	g_state.offsetMs = (int)SendMessageA(g_state.hSlider, TBM_GETPOS, 0, 0);

	/* Validate device selection via CPrefs */
	int initResult = g_state.prefs->initDevice(devIdx);
	if (initResult != ALL_OK)
	{
		setStatus("Invalid device. Choose a non-default device.");
		return;
	}

	/* Initialize ring buffer */
	ma_result result = ma_pcm_rb_init(ma_format_f32, DEFAULT_CHANNELS,
									  DEFAULT_BUFFER_FRAMES, NULL, NULL,
									  &g_state.ctxt.buffer);
	if (result != MA_SUCCESS)
	{
		setStatus("Failed to initialize audio buffer.");
		return;
	}
	g_state.ctxt.frameSizeInBytes = ma_get_bytes_per_frame(ma_format_f32, DEFAULT_CHANNELS);

	/* Apply delay offset */
	if (g_state.offsetMs > 0)
	{
		ma_uint32 delayFrames = static_cast<ma_uint32>(
			(static_cast<ma_uint64>(g_state.offsetMs) * DEFAULT_SAMPLE_RATE) / 1000);
		applyDelayOffset(&g_state.ctxt, delayFrames);
	}

	/* Configure devices */
	ma_device_config pCfg = g_state.prefs->pDeviceConfig;
	ma_device_config cCfg = g_state.prefs->cDeviceConfig;
	pCfg.pUserData = &g_state.ctxt;
	cCfg.pUserData = &g_state.ctxt;

	result = ma_device_init(NULL, &pCfg, &g_state.pdevice);
	if (result != MA_SUCCESS)
	{
		setStatus("Failed to init playback device.");
		ma_pcm_rb_uninit(&g_state.ctxt.buffer);
		return;
	}

	ma_backend backends[] = {ma_backend_wasapi};
	result = ma_device_init_ex(backends, 1, NULL, &cCfg, &g_state.cdevice);
	if (result != MA_SUCCESS)
	{
		setStatus("Failed to init capture device.");
		ma_device_uninit(&g_state.pdevice);
		ma_pcm_rb_uninit(&g_state.ctxt.buffer);
		return;
	}

	result = ma_device_start(&g_state.cdevice);
	if (result != MA_SUCCESS)
	{
		setStatus("Failed to start capture.");
		ma_device_uninit(&g_state.cdevice);
		ma_device_uninit(&g_state.pdevice);
		ma_pcm_rb_uninit(&g_state.ctxt.buffer);
		return;
	}

	result = ma_device_start(&g_state.pdevice);
	if (result != MA_SUCCESS)
	{
		setStatus("Failed to start playback.");
		ma_device_stop(&g_state.cdevice);
		ma_device_uninit(&g_state.cdevice);
		ma_device_uninit(&g_state.pdevice);
		ma_pcm_rb_uninit(&g_state.ctxt.buffer);
		return;
	}

	g_state.running = true;
	EnableWindow(g_state.hStartBtn, FALSE);
	EnableWindow(g_state.hStopBtn, TRUE);
	EnableWindow(g_state.hCombo, FALSE);

	char statusBuf[128];
	snprintf(statusBuf, sizeof(statusBuf), "Playing (offset: %d ms)", g_state.offsetMs);
	setStatus(statusBuf);
}

/* ── Stop audio ── */
static void stopAudio()
{
	if (!g_state.running)
		return;

	ma_device_stop(&g_state.pdevice);
	ma_device_stop(&g_state.cdevice);
	ma_device_uninit(&g_state.pdevice);
	ma_device_uninit(&g_state.cdevice);
	ma_pcm_rb_uninit(&g_state.ctxt.buffer);

	g_state.running = false;
	g_state.ctxt.targetDelayFrames.store(0, std::memory_order_relaxed);
	EnableWindow(g_state.hStartBtn, TRUE);
	EnableWindow(g_state.hStopBtn, FALSE);
	EnableWindow(g_state.hCombo, TRUE);
	setStatus("Stopped.");
}

/* ── Window procedure ── */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_CREATE:
	{
		int y = 15;

		/* Title label */
		CreateWindowA("STATIC", "SyncWave - Audio Sync Tool",
					  WS_VISIBLE | WS_CHILD | SS_CENTER,
					  10, y, WIN_W - 40, 24, hwnd, NULL, NULL, NULL);
		y += 35;

		/* Device selection label */
		CreateWindowA("STATIC", "Output Device:",
					  WS_VISIBLE | WS_CHILD,
					  15, y, 120, 20, hwnd, NULL, NULL, NULL);
		y += 22;

		/* Device combo box */
		g_state.hCombo = CreateWindowA("COMBOBOX", "",
									   WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
									   15, y, WIN_W - 50, 200, hwnd,
									   (HMENU)IDC_DEVICE_COMBO, NULL, NULL);
		y += 35;

		/* Populate combo box with playback devices */
		ma_uint32 devCount = g_state.prefs->getPlaybackDeviceCount();
		ma_device_info *devInfos = g_state.prefs->getPlaybackDevices();
		for (ma_uint32 i = 0; i < devCount; ++i)
		{
			char entry[MA_MAX_DEVICE_NAME_LENGTH + 32];
			if (devInfos[i].isDefault)
				snprintf(entry, sizeof(entry), "%u: %s (default)", i, devInfos[i].name);
			else
				snprintf(entry, sizeof(entry), "%u: %s", i, devInfos[i].name);
			SendMessageA(g_state.hCombo, CB_ADDSTRING, 0, (LPARAM)entry);
		}

		/* Delay offset label */
		CreateWindowA("STATIC", "Delay Offset (for Bluetooth sync):",
					  WS_VISIBLE | WS_CHILD,
					  15, y, 280, 20, hwnd, NULL, NULL, NULL);
		y += 22;

		/* Delay offset slider (trackbar) */
		g_state.hSlider = CreateWindowA(TRACKBAR_CLASSA, "",
										WS_VISIBLE | WS_CHILD | TBS_AUTOTICKS | TBS_TOOLTIPS,
										15, y, WIN_W - 130, 30, hwnd,
										(HMENU)IDC_OFFSET_SLIDER, NULL, NULL);
		SendMessageA(g_state.hSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, MAX_OFFSET_MS));
		SendMessageA(g_state.hSlider, TBM_SETTICFREQ, 50, 0);
		SendMessageA(g_state.hSlider, TBM_SETPOS, TRUE, g_state.prefs->delayOffsetMs);

		/* Offset value label */
		g_state.hOffsetLabel = CreateWindowA("STATIC", "",
											  WS_VISIBLE | WS_CHILD | SS_LEFT,
											  WIN_W - 110, y + 4, 100, 20, hwnd,
											  (HMENU)IDC_OFFSET_LABEL, NULL, NULL);
		updateOffsetLabel(hwnd, g_state.prefs->delayOffsetMs);
		y += 42;

		/* Buttons */
		g_state.hStartBtn = CreateWindowA("BUTTON", "Start",
										   WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
										   15, y, 100, 32, hwnd,
										   (HMENU)IDC_START_BTN, NULL, NULL);

		g_state.hStopBtn = CreateWindowA("BUTTON", "Stop",
										  WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
										  125, y, 100, 32, hwnd,
										  (HMENU)IDC_STOP_BTN, NULL, NULL);
		EnableWindow(g_state.hStopBtn, FALSE);
		y += 48;

		/* Status label */
		g_state.hStatusLabel = CreateWindowA("STATIC", "Ready. Select a device and press Start.",
											  WS_VISIBLE | WS_CHILD | SS_LEFT,
											  15, y, WIN_W - 50, 20, hwnd,
											  (HMENU)IDC_STATUS_LABEL, NULL, NULL);
		return 0;
	}

	case WM_HSCROLL:
	{
		if ((HWND)lParam == g_state.hSlider)
		{
			int pos = (int)SendMessageA(g_state.hSlider, TBM_GETPOS, 0, 0);
			updateOffsetLabel(hwnd, pos);

			/* Update delay at runtime if audio is running */
			if (g_state.running)
			{
				ma_uint32 newDelay = static_cast<ma_uint32>(
					(static_cast<ma_uint64>(pos) * DEFAULT_SAMPLE_RATE) / 1000);
				g_state.ctxt.targetDelayFrames.store(newDelay, std::memory_order_relaxed);
			}
		}
		return 0;
	}

	case WM_COMMAND:
	{
		switch (LOWORD(wParam))
		{
		case IDC_START_BTN:
			startAudio(hwnd);
			break;
		case IDC_STOP_BTN:
			stopAudio();
			break;
		}
		return 0;
	}

	case WM_DESTROY:
		stopAudio();
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int runGUI(CPrefs &prefs)
{
	g_state.prefs = &prefs;

	/* Init common controls for trackbar */
	INITCOMMONCONTROLSEX icex;
	icex.dwSize = sizeof(icex);
	icex.dwICC = ICC_BAR_CLASSES;
	InitCommonControlsEx(&icex);

	/* Register window class */
	WNDCLASSA wc{};
	wc.lpfnWndProc = WndProc;
	wc.hInstance = GetModuleHandle(NULL);
	wc.lpszClassName = "SyncWaveGUI";
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);

	if (!RegisterClassA(&wc))
	{
		crit("Failed to register window class.");
		return -1;
	}

	/* Create window */
	HWND hwnd = CreateWindowExA(
		0, "SyncWaveGUI", "SyncWave",
		WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
		CW_USEDEFAULT, CW_USEDEFAULT, WIN_W, WIN_H,
		NULL, NULL, wc.hInstance, NULL);

	if (!hwnd)
	{
		crit("Failed to create window.");
		return -1;
	}

	ShowWindow(hwnd, SW_SHOW);
	UpdateWindow(hwnd);

	/* Message loop */
	MSG msg;
	while (GetMessageA(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessageA(&msg);
	}

	return (int)msg.wParam;
}

#endif /* _WIN32 */
