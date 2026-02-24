#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

/* Enable visual styles via manifest (modern look on Vista+) */
#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include "common.hpp"
#include "gui.hpp"

/* ── Control IDs ── */
#define IDC_DEVICE_COMBO      101
#define IDC_OFFSET_SLIDER     102
#define IDC_OFFSET_LABEL      103
#define IDC_START_BTN         104
#define IDC_STOP_BTN          105
#define IDC_STATUS_LABEL      106
#define IDC_DELAY_DIR_OUTPUT  107
#define IDC_DELAY_DIR_DEFAULT 108
#define IDC_OUT_VOL_SLIDER    109
#define IDC_OUT_VOL_LABEL     110
#define IDC_DEF_VOL_SLIDER    111
#define IDC_DEF_VOL_LABEL     112
#define IDC_OUT_CHAN_COMBO     113
#define IDC_DEF_CHAN_COMBO     114
#define IDC_OUT_LPF_SLIDER    115
#define IDC_OUT_LPF_LABEL     116
#define IDC_OUT_HPF_SLIDER    117
#define IDC_OUT_HPF_LABEL     118
#define IDC_DEF_LPF_SLIDER    119
#define IDC_DEF_LPF_LABEL     120
#define IDC_DEF_HPF_SLIDER    121
#define IDC_DEF_HPF_LABEL     122

/* ── Window dimensions ── */
static constexpr int WIN_W = 520;
static constexpr int WIN_H = 660;
static constexpr int PAD  = 15;
static constexpr int COL2 = 260; /* x-start of the right column */

/* ── Shared state ── */
struct GUIState
{
	CPrefs *prefs;
	SyncWaveContext ctxt{};
	ma_device pdevice{};
	ma_device cdevice{};
	ma_device ddevice{}; /* default-device playback (dual-buffer mode) */
	bool running = false;
	int selectedDevice = -1;
	int offsetMs = DEFAULT_OFFSET_MS;
	bool delayDefault = false;

	/* Win32 handles */
	HWND hCombo = NULL;
	HWND hSlider = NULL;
	HWND hOffsetLabel = NULL;
	HWND hStartBtn = NULL;
	HWND hStopBtn = NULL;
	HWND hStatusLabel = NULL;
	HWND hDelayDirOutput = NULL;
	HWND hDelayDirDefault = NULL;

	/* Output device controls */
	HWND hOutVolSlider = NULL;
	HWND hOutVolLabel = NULL;
	HWND hOutChanCombo = NULL;
	HWND hOutLPFSlider = NULL;
	HWND hOutLPFLabel = NULL;
	HWND hOutHPFSlider = NULL;
	HWND hOutHPFLabel = NULL;

	/* Default device controls */
	HWND hDefVolSlider = NULL;
	HWND hDefVolLabel = NULL;
	HWND hDefChanCombo = NULL;
	HWND hDefLPFSlider = NULL;
	HWND hDefLPFLabel = NULL;
	HWND hDefHPFSlider = NULL;
	HWND hDefHPFLabel = NULL;

	HFONT hFont = NULL;
	HFONT hTitleFont = NULL;
};

static GUIState g;

/* ── Helpers ── */

static HFONT createFont(int size, int weight)
{
	return CreateFontA(size, 0, 0, 0, weight,
					   FALSE, FALSE, FALSE,
					   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
					   CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
					   DEFAULT_PITCH | FF_SWISS, "Segoe UI");
}

static void setFont(HWND hwnd)
{
	SendMessageA(hwnd, WM_SETFONT, (WPARAM)g.hFont, TRUE);
}

static void updateOffsetLabel(int ms)
{
	char buf[64];
	snprintf(buf, sizeof(buf), "%d ms", ms);
	SetWindowTextA(g.hOffsetLabel, buf);
}

static void setStatus(const char *text)
{
	SetWindowTextA(g.hStatusLabel, text);
}

static void updateFilterLabel(HWND label, const char *prefix, int hz)
{
	char buf[64];
	if (hz < MIN_FILTER_HZ)
		snprintf(buf, sizeof(buf), "%s: OFF", prefix);
	else
		snprintf(buf, sizeof(buf), "%s: %d Hz", prefix, hz);
	SetWindowTextA(label, buf);
}

static void updateVolumeLabel(HWND label, int pct)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "%d%%", pct);
	SetWindowTextA(label, buf);
}

/* ── Stop audio ── */
static void stopAudio()
{
	if (!g.running)
		return;

	ma_device_stop(&g.pdevice);
	ma_device_stop(&g.cdevice);
	ma_device_uninit(&g.pdevice);
	ma_device_uninit(&g.cdevice);
	ma_pcm_rb_uninit(&g.ctxt.buffer);

	if (g.ctxt.dualBufferMode)
	{
		ma_device_stop(&g.ddevice);
		ma_device_uninit(&g.ddevice);
		ma_pcm_rb_uninit(&g.ctxt.defaultBuffer);
		g.ctxt.dualBufferMode = false;
	}

	g.running = false;
	g.ctxt.targetDelayFrames.store(0, std::memory_order_relaxed);
	g.ctxt.defaultTargetDelayFrames.store(0, std::memory_order_relaxed);
	g.ctxt.activeOutputLPFHz = 0;
	g.ctxt.activeOutputHPFHz = 0;
	g.ctxt.activeDefaultLPFHz = 0;
	g.ctxt.activeDefaultHPFHz = 0;

	EnableWindow(g.hStartBtn, TRUE);
	EnableWindow(g.hStopBtn, FALSE);
	EnableWindow(g.hCombo, TRUE);
	EnableWindow(g.hDelayDirOutput, TRUE);
	EnableWindow(g.hDelayDirDefault, TRUE);
	setStatus("Stopped.");
}

/* ── Start audio ── */
static void startAudio(HWND hwnd)
{
	if (g.running)
		return;

	int devIdx = (int)SendMessageA(g.hCombo, CB_GETCURSEL, 0, 0);
	if (devIdx == CB_ERR || devIdx < 0)
	{
		setStatus("Please select an output device.");
		return;
	}

	g.selectedDevice = devIdx;
	g.offsetMs = (int)SendMessageA(g.hSlider, TBM_GETPOS, 0, 0);
	g.delayDefault = (SendMessageA(g.hDelayDirDefault, BM_GETCHECK, 0, 0) == BST_CHECKED);

	/* Validate device selection via CPrefs */
	int initResult = g.prefs->initDevice(devIdx);
	if (initResult != ALL_OK)
	{
		setStatus("Invalid device. Choose a non-default device.");
		return;
	}

	/* Initialize primary ring buffer */
	ma_result result = ma_pcm_rb_init(ma_format_f32, DEFAULT_CHANNELS,
									  DEFAULT_BUFFER_FRAMES, NULL, NULL,
									  &g.ctxt.buffer);
	if (result != MA_SUCCESS)
	{
		setStatus("Failed to initialize audio buffer.");
		return;
	}
	g.ctxt.frameSizeInBytes = ma_get_bytes_per_frame(ma_format_f32, DEFAULT_CHANNELS);

	/* Apply initial control values */
	g.ctxt.outputVolume.store(
		(float)SendMessageA(g.hOutVolSlider, TBM_GETPOS, 0, 0) / 100.0f,
		std::memory_order_relaxed);
	g.ctxt.defaultDeviceVolume.store(
		(float)SendMessageA(g.hDefVolSlider, TBM_GETPOS, 0, 0) / 100.0f,
		std::memory_order_relaxed);
	g.ctxt.outputChannelMode.store(
		(int)SendMessageA(g.hOutChanCombo, CB_GETCURSEL, 0, 0),
		std::memory_order_relaxed);
	g.ctxt.defaultChannelMode.store(
		(int)SendMessageA(g.hDefChanCombo, CB_GETCURSEL, 0, 0),
		std::memory_order_relaxed);
	g.ctxt.outputLowPassHz.store(
		(int)SendMessageA(g.hOutLPFSlider, TBM_GETPOS, 0, 0),
		std::memory_order_relaxed);
	g.ctxt.outputHighPassHz.store(
		(int)SendMessageA(g.hOutHPFSlider, TBM_GETPOS, 0, 0),
		std::memory_order_relaxed);
	g.ctxt.defaultLowPassHz.store(
		(int)SendMessageA(g.hDefLPFSlider, TBM_GETPOS, 0, 0),
		std::memory_order_relaxed);
	g.ctxt.defaultHighPassHz.store(
		(int)SendMessageA(g.hDefHPFSlider, TBM_GETPOS, 0, 0),
		std::memory_order_relaxed);

	/* Delay direction handling */
	if (g.delayDefault)
	{
		/* "Delay Default" mode: secondary (output) plays immediately,
		   default device gets the delay via a second ring buffer. */
		result = ma_pcm_rb_init(ma_format_f32, DEFAULT_CHANNELS,
								DEFAULT_BUFFER_FRAMES, NULL, NULL,
								&g.ctxt.defaultBuffer);
		if (result != MA_SUCCESS)
		{
			setStatus("Failed to initialize default device buffer.");
			ma_pcm_rb_uninit(&g.ctxt.buffer);
			return;
		}
		g.ctxt.dualBufferMode = true;

		/* Pre-fill silence into the DEFAULT device buffer */
		if (g.offsetMs > 0)
		{
			ma_uint32 delayFrames = static_cast<ma_uint32>(
				(static_cast<ma_uint64>(g.offsetMs) * DEFAULT_SAMPLE_RATE) / 1000);
			applyDelayOffsetToBuffer(&g.ctxt.defaultBuffer,
									g.ctxt.frameSizeInBytes, delayFrames);
			g.ctxt.defaultTargetDelayFrames.store(delayFrames, std::memory_order_relaxed);
		}
	}
	else
	{
		/* "Delay Output" mode (original behaviour): delay the secondary device */
		g.ctxt.dualBufferMode = false;
		if (g.offsetMs > 0)
		{
			ma_uint32 delayFrames = static_cast<ma_uint32>(
				(static_cast<ma_uint64>(g.offsetMs) * DEFAULT_SAMPLE_RATE) / 1000);
			applyDelayOffset(&g.ctxt, delayFrames);
		}
	}

	/* Configure devices */
	ma_device_config pCfg = g.prefs->pDeviceConfig;
	ma_device_config cCfg = g.prefs->cDeviceConfig;
	pCfg.pUserData = &g.ctxt;
	cCfg.pUserData = &g.ctxt;

	result = ma_device_init(NULL, &pCfg, &g.pdevice);
	if (result != MA_SUCCESS)
	{
		setStatus("Failed to init playback device.");
		ma_pcm_rb_uninit(&g.ctxt.buffer);
		if (g.ctxt.dualBufferMode) ma_pcm_rb_uninit(&g.ctxt.defaultBuffer);
		g.ctxt.dualBufferMode = false;
		return;
	}

	ma_backend backends[] = {ma_backend_wasapi};
	result = ma_device_init_ex(backends, 1, NULL, &cCfg, &g.cdevice);
	if (result != MA_SUCCESS)
	{
		setStatus("Failed to init capture device.");
		ma_device_uninit(&g.pdevice);
		ma_pcm_rb_uninit(&g.ctxt.buffer);
		if (g.ctxt.dualBufferMode) ma_pcm_rb_uninit(&g.ctxt.defaultBuffer);
		g.ctxt.dualBufferMode = false;
		return;
	}

	/* In dual-buffer mode, init a playback device for the default device */
	if (g.ctxt.dualBufferMode)
	{
		ma_device_config dCfg = g.prefs->pDefaultDeviceConfig;
		dCfg.pUserData = &g.ctxt;
		result = ma_device_init(NULL, &dCfg, &g.ddevice);
		if (result != MA_SUCCESS)
		{
			setStatus("Failed to init default playback device.");
			ma_device_uninit(&g.cdevice);
			ma_device_uninit(&g.pdevice);
			ma_pcm_rb_uninit(&g.ctxt.buffer);
			ma_pcm_rb_uninit(&g.ctxt.defaultBuffer);
			g.ctxt.dualBufferMode = false;
			return;
		}
	}

	/* Start devices */
	result = ma_device_start(&g.cdevice);
	if (result != MA_SUCCESS)
	{
		setStatus("Failed to start capture.");
		if (g.ctxt.dualBufferMode) { ma_device_uninit(&g.ddevice); ma_pcm_rb_uninit(&g.ctxt.defaultBuffer); }
		ma_device_uninit(&g.cdevice);
		ma_device_uninit(&g.pdevice);
		ma_pcm_rb_uninit(&g.ctxt.buffer);
		g.ctxt.dualBufferMode = false;
		return;
	}

	result = ma_device_start(&g.pdevice);
	if (result != MA_SUCCESS)
	{
		setStatus("Failed to start playback.");
		ma_device_stop(&g.cdevice);
		if (g.ctxt.dualBufferMode) { ma_device_uninit(&g.ddevice); ma_pcm_rb_uninit(&g.ctxt.defaultBuffer); }
		ma_device_uninit(&g.cdevice);
		ma_device_uninit(&g.pdevice);
		ma_pcm_rb_uninit(&g.ctxt.buffer);
		g.ctxt.dualBufferMode = false;
		return;
	}

	if (g.ctxt.dualBufferMode)
	{
		result = ma_device_start(&g.ddevice);
		if (result != MA_SUCCESS)
		{
			setStatus("Failed to start default device playback.");
			ma_device_stop(&g.pdevice);
			ma_device_stop(&g.cdevice);
			ma_device_uninit(&g.ddevice);
			ma_device_uninit(&g.cdevice);
			ma_device_uninit(&g.pdevice);
			ma_pcm_rb_uninit(&g.ctxt.buffer);
			ma_pcm_rb_uninit(&g.ctxt.defaultBuffer);
			g.ctxt.dualBufferMode = false;
			return;
		}
	}

	g.running = true;
	EnableWindow(g.hStartBtn, FALSE);
	EnableWindow(g.hStopBtn, TRUE);
	EnableWindow(g.hCombo, FALSE);
	EnableWindow(g.hDelayDirOutput, FALSE);
	EnableWindow(g.hDelayDirDefault, FALSE);

	char statusBuf[128];
	snprintf(statusBuf, sizeof(statusBuf), "Playing  (offset %d ms, delay %s)",
			 g.offsetMs, g.delayDefault ? "default device" : "output device");
	setStatus(statusBuf);
}

/* ── Create a labelled group box ── */
static HWND createGroupBox(HWND parent, const char *title, int x, int y, int w, int h)
{
	HWND gb = CreateWindowA("BUTTON", title,
							WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
							x, y, w, h, parent, NULL, NULL, NULL);
	setFont(gb);
	return gb;
}

/* ── Create a static label ── */
static HWND createLabel(HWND parent, const char *text, int x, int y, int w, int h,
						DWORD style = SS_LEFT, HMENU id = NULL)
{
	HWND lbl = CreateWindowA("STATIC", text,
							 WS_VISIBLE | WS_CHILD | style,
							 x, y, w, h, parent, id, NULL, NULL);
	setFont(lbl);
	return lbl;
}

/* ── Create a trackbar (slider) ── */
static HWND createSlider(HWND parent, int x, int y, int w, int h,
						 int minVal, int maxVal, int pos, int tickFreq, HMENU id)
{
	HWND tb = CreateWindowA(TRACKBAR_CLASSA, "",
							WS_VISIBLE | WS_CHILD | TBS_AUTOTICKS | TBS_TOOLTIPS,
							x, y, w, h, parent, id, NULL, NULL);
	SendMessageA(tb, TBM_SETRANGE, TRUE, MAKELPARAM(minVal, maxVal));
	SendMessageA(tb, TBM_SETTICFREQ, tickFreq, 0);
	SendMessageA(tb, TBM_SETPOS, TRUE, pos);
	return tb;
}

/* ── Window procedure ── */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_CREATE:
	{
		/* Fonts */
		g.hFont = createFont(-14, FW_NORMAL);
		g.hTitleFont = createFont(-20, FW_BOLD);

		int y = 10;
		int contentW = WIN_W - 2 * PAD - 16; /* account for window border */

		/* ── Title ── */
		HWND hTitle = createLabel(hwnd, "SyncWave", PAD, y, contentW, 28, SS_CENTER);
		SendMessageA(hTitle, WM_SETFONT, (WPARAM)g.hTitleFont, TRUE);
		y += 36;

		/* ═══════ Device & Delay Settings group ═══════ */
		int grpY = y;
		int grpH = 150;
		createGroupBox(hwnd, " Device && Delay ", PAD, grpY, contentW, grpH);
		int gy = grpY + 20;

		createLabel(hwnd, "Output device:", PAD + 10, gy, 100, 18);
		g.hCombo = CreateWindowA("COMBOBOX", "",
								 WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
								 PAD + 110, gy - 2, contentW - 120, 200, hwnd,
								 (HMENU)IDC_DEVICE_COMBO, NULL, NULL);
		setFont(g.hCombo);
		gy += 28;

		/* Populate combo */
		ma_uint32 devCount = g.prefs->getPlaybackDeviceCount();
		ma_device_info *devInfos = g.prefs->getPlaybackDevices();
		for (ma_uint32 i = 0; i < devCount; ++i)
		{
			char entry[MA_MAX_DEVICE_NAME_LENGTH + 32];
			if (devInfos[i].isDefault)
				snprintf(entry, sizeof(entry), "%u: %s (default)", i, devInfos[i].name);
			else
				snprintf(entry, sizeof(entry), "%u: %s", i, devInfos[i].name);
			SendMessageA(g.hCombo, CB_ADDSTRING, 0, (LPARAM)entry);
		}

		/* Delay direction */
		createLabel(hwnd, "Delay direction:", PAD + 10, gy, 100, 18);
		g.hDelayDirOutput = CreateWindowA("BUTTON", "Delay output device",
										  WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON | WS_GROUP,
										  PAD + 116, gy, 170, 18, hwnd,
										  (HMENU)IDC_DELAY_DIR_OUTPUT, NULL, NULL);
		setFont(g.hDelayDirOutput);
		g.hDelayDirDefault = CreateWindowA("BUTTON", "Delay default device",
										   WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON,
										   PAD + 290, gy, 170, 18, hwnd,
										   (HMENU)IDC_DELAY_DIR_DEFAULT, NULL, NULL);
		setFont(g.hDelayDirDefault);
		SendMessageA(g.hDelayDirOutput, BM_SETCHECK, BST_CHECKED, 0);
		gy += 26;

		/* Delay slider */
		createLabel(hwnd, "Delay offset:", PAD + 10, gy + 4, 90, 18);
		g.hSlider = createSlider(hwnd, PAD + 100, gy, contentW - 170, 28,
								 0, MAX_OFFSET_MS, g.prefs->delayOffsetMs, 50,
								 (HMENU)IDC_OFFSET_SLIDER);
		g.hOffsetLabel = createLabel(hwnd, "", contentW - 50, gy + 4, 60, 18,
									 SS_LEFT, (HMENU)IDC_OFFSET_LABEL);
		updateOffsetLabel(g.prefs->delayOffsetMs);
		gy += 36;

		y = grpY + grpH + 8;

		/* ═══════ Output Device Controls group ═══════ */
		grpY = y;
		grpH = 156;
		createGroupBox(hwnd, " Output Device Controls ", PAD, grpY, contentW, grpH);
		gy = grpY + 20;

		/* Volume */
		createLabel(hwnd, "Volume:", PAD + 10, gy + 4, 60, 18);
		g.hOutVolSlider = createSlider(hwnd, PAD + 70, gy, contentW - 140, 28,
									   0, 100, 100, 10, (HMENU)IDC_OUT_VOL_SLIDER);
		g.hOutVolLabel = createLabel(hwnd, "100%", contentW - 50, gy + 4, 50, 18,
									 SS_LEFT, (HMENU)IDC_OUT_VOL_LABEL);
		gy += 30;

		/* Channel */
		createLabel(hwnd, "Channel:", PAD + 10, gy + 2, 60, 18);
		g.hOutChanCombo = CreateWindowA("COMBOBOX", "",
										WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST,
										PAD + 70, gy, 120, 100, hwnd,
										(HMENU)IDC_OUT_CHAN_COMBO, NULL, NULL);
		setFont(g.hOutChanCombo);
		SendMessageA(g.hOutChanCombo, CB_ADDSTRING, 0, (LPARAM)"Both");
		SendMessageA(g.hOutChanCombo, CB_ADDSTRING, 0, (LPARAM)"Left only");
		SendMessageA(g.hOutChanCombo, CB_ADDSTRING, 0, (LPARAM)"Right only");
		SendMessageA(g.hOutChanCombo, CB_SETCURSEL, 0, 0);
		gy += 28;

		/* Low-pass */
		createLabel(hwnd, "Low-pass:", PAD + 10, gy + 4, 60, 18);
		g.hOutLPFSlider = createSlider(hwnd, PAD + 70, gy, contentW - 140, 28,
									   0, MAX_LPF_HZ, 0, 1000, (HMENU)IDC_OUT_LPF_SLIDER);
		g.hOutLPFLabel = createLabel(hwnd, "LP: OFF", contentW - 60, gy + 4, 70, 18,
									 SS_LEFT, (HMENU)IDC_OUT_LPF_LABEL);
		gy += 30;

		/* High-pass */
		createLabel(hwnd, "High-pass:", PAD + 10, gy + 4, 60, 18);
		g.hOutHPFSlider = createSlider(hwnd, PAD + 70, gy, contentW - 140, 28,
									   0, MAX_HPF_HZ, 0, 500, (HMENU)IDC_OUT_HPF_SLIDER);
		g.hOutHPFLabel = createLabel(hwnd, "HP: OFF", contentW - 60, gy + 4, 70, 18,
									 SS_LEFT, (HMENU)IDC_OUT_HPF_LABEL);
		gy += 30;

		y = grpY + grpH + 8;

		/* ═══════ Default Device Controls group ═══════ */
		grpY = y;
		grpH = 156;
		createGroupBox(hwnd, " Default Device Controls ", PAD, grpY, contentW, grpH);
		gy = grpY + 20;

		/* Volume */
		createLabel(hwnd, "Volume:", PAD + 10, gy + 4, 60, 18);
		g.hDefVolSlider = createSlider(hwnd, PAD + 70, gy, contentW - 140, 28,
									   0, 100, 100, 10, (HMENU)IDC_DEF_VOL_SLIDER);
		g.hDefVolLabel = createLabel(hwnd, "100%", contentW - 50, gy + 4, 50, 18,
									 SS_LEFT, (HMENU)IDC_DEF_VOL_LABEL);
		gy += 30;

		/* Channel */
		createLabel(hwnd, "Channel:", PAD + 10, gy + 2, 60, 18);
		g.hDefChanCombo = CreateWindowA("COMBOBOX", "",
										WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST,
										PAD + 70, gy, 120, 100, hwnd,
										(HMENU)IDC_DEF_CHAN_COMBO, NULL, NULL);
		setFont(g.hDefChanCombo);
		SendMessageA(g.hDefChanCombo, CB_ADDSTRING, 0, (LPARAM)"Both");
		SendMessageA(g.hDefChanCombo, CB_ADDSTRING, 0, (LPARAM)"Left only");
		SendMessageA(g.hDefChanCombo, CB_ADDSTRING, 0, (LPARAM)"Right only");
		SendMessageA(g.hDefChanCombo, CB_SETCURSEL, 0, 0);
		gy += 28;

		/* Low-pass */
		createLabel(hwnd, "Low-pass:", PAD + 10, gy + 4, 60, 18);
		g.hDefLPFSlider = createSlider(hwnd, PAD + 70, gy, contentW - 140, 28,
									   0, MAX_LPF_HZ, 0, 1000, (HMENU)IDC_DEF_LPF_SLIDER);
		g.hDefLPFLabel = createLabel(hwnd, "LP: OFF", contentW - 60, gy + 4, 70, 18,
									 SS_LEFT, (HMENU)IDC_DEF_LPF_LABEL);
		gy += 30;

		/* High-pass */
		createLabel(hwnd, "High-pass:", PAD + 10, gy + 4, 60, 18);
		g.hDefHPFSlider = createSlider(hwnd, PAD + 70, gy, contentW - 140, 28,
									   0, MAX_HPF_HZ, 0, 500, (HMENU)IDC_DEF_HPF_SLIDER);
		g.hDefHPFLabel = createLabel(hwnd, "HP: OFF", contentW - 60, gy + 4, 70, 18,
									 SS_LEFT, (HMENU)IDC_DEF_HPF_LABEL);
		gy += 30;

		y = grpY + grpH + 12;

		/* ═══════ Buttons ═══════ */
		g.hStartBtn = CreateWindowA("BUTTON", "  Start  ",
									WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
									PAD, y, 110, 34, hwnd,
									(HMENU)IDC_START_BTN, NULL, NULL);
		setFont(g.hStartBtn);

		g.hStopBtn = CreateWindowA("BUTTON", "  Stop  ",
								   WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
								   PAD + 120, y, 110, 34, hwnd,
								   (HMENU)IDC_STOP_BTN, NULL, NULL);
		setFont(g.hStopBtn);
		EnableWindow(g.hStopBtn, FALSE);
		y += 44;

		/* ── Status label ── */
		g.hStatusLabel = CreateWindowA("STATIC", "Ready. Select a device and press Start.",
									   WS_VISIBLE | WS_CHILD | SS_LEFT,
									   PAD, y, contentW, 20, hwnd,
									   (HMENU)IDC_STATUS_LABEL, NULL, NULL);
		setFont(g.hStatusLabel);
		return 0;
	}

	case WM_HSCROLL:
	{
		HWND ctl = (HWND)lParam;

		if (ctl == g.hSlider)
		{
			int pos = (int)SendMessageA(g.hSlider, TBM_GETPOS, 0, 0);
			updateOffsetLabel(pos);
			if (g.running)
			{
				ma_uint32 newDelay = static_cast<ma_uint32>(
					(static_cast<ma_uint64>(pos) * DEFAULT_SAMPLE_RATE) / 1000);
				if (g.delayDefault)
					g.ctxt.defaultTargetDelayFrames.store(newDelay, std::memory_order_relaxed);
				else
					g.ctxt.targetDelayFrames.store(newDelay, std::memory_order_relaxed);
			}
		}
		else if (ctl == g.hOutVolSlider)
		{
			int v = (int)SendMessageA(g.hOutVolSlider, TBM_GETPOS, 0, 0);
			updateVolumeLabel(g.hOutVolLabel, v);
			g.ctxt.outputVolume.store(v / 100.0f, std::memory_order_relaxed);
		}
		else if (ctl == g.hDefVolSlider)
		{
			int v = (int)SendMessageA(g.hDefVolSlider, TBM_GETPOS, 0, 0);
			updateVolumeLabel(g.hDefVolLabel, v);
			g.ctxt.defaultDeviceVolume.store(v / 100.0f, std::memory_order_relaxed);
		}
		else if (ctl == g.hOutLPFSlider)
		{
			int hz = (int)SendMessageA(g.hOutLPFSlider, TBM_GETPOS, 0, 0);
			updateFilterLabel(g.hOutLPFLabel, "LP", hz);
			g.ctxt.outputLowPassHz.store(hz, std::memory_order_relaxed);
		}
		else if (ctl == g.hOutHPFSlider)
		{
			int hz = (int)SendMessageA(g.hOutHPFSlider, TBM_GETPOS, 0, 0);
			updateFilterLabel(g.hOutHPFLabel, "HP", hz);
			g.ctxt.outputHighPassHz.store(hz, std::memory_order_relaxed);
		}
		else if (ctl == g.hDefLPFSlider)
		{
			int hz = (int)SendMessageA(g.hDefLPFSlider, TBM_GETPOS, 0, 0);
			updateFilterLabel(g.hDefLPFLabel, "LP", hz);
			g.ctxt.defaultLowPassHz.store(hz, std::memory_order_relaxed);
		}
		else if (ctl == g.hDefHPFSlider)
		{
			int hz = (int)SendMessageA(g.hDefHPFSlider, TBM_GETPOS, 0, 0);
			updateFilterLabel(g.hDefHPFLabel, "HP", hz);
			g.ctxt.defaultHighPassHz.store(hz, std::memory_order_relaxed);
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
		case IDC_OUT_CHAN_COMBO:
			if (HIWORD(wParam) == CBN_SELCHANGE)
				g.ctxt.outputChannelMode.store(
					(int)SendMessageA(g.hOutChanCombo, CB_GETCURSEL, 0, 0),
					std::memory_order_relaxed);
			break;
		case IDC_DEF_CHAN_COMBO:
			if (HIWORD(wParam) == CBN_SELCHANGE)
				g.ctxt.defaultChannelMode.store(
					(int)SendMessageA(g.hDefChanCombo, CB_GETCURSEL, 0, 0),
					std::memory_order_relaxed);
			break;
		}
		return 0;
	}

	case WM_CTLCOLORSTATIC:
	{
		HDC hdc = (HDC)wParam;
		SetBkMode(hdc, TRANSPARENT);
		return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
	}

	case WM_DESTROY:
		stopAudio();
		if (g.hFont) DeleteObject(g.hFont);
		if (g.hTitleFont) DeleteObject(g.hTitleFont);
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int runGUI(CPrefs &prefs)
{
	g.prefs = &prefs;

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
