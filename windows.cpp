#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING 1

#include <windows.h>
#include <shellapi.h>
#include <ShObjIdl.h>
#include <Shlobj.h>

#include "resource.h"
#include "gb_emu.h"

//#include <stdio.h>
#include <cassert>
#include <filesystem>
#include <functional>
#include <optional>

template<uint32_t num_palette_entries, uint8_t bit_depth>
struct bitmap_info
{
	BITMAPINFOHEADER header;
	RGBQUAD palette[num_palette_entries];

	bitmap_info(LONG width, LONG height, const RGBQUAD* palette)
	{
		header.biSize = sizeof(header);
		header.biWidth = width;
		header.biHeight = -height;
		header.biPlanes = 1;
		// bit_depth >= num_palette_entries == 0 ? 16 : (WORD)std::ceil(std::log2(num_palette_entries))
		header.biBitCount = bit_depth;
		header.biCompression = BI_RGB;
		header.biSizeImage = (width * height * bit_depth) / 8;
		header.biXPelsPerMeter = 0;
		header.biYPelsPerMeter = 0;
		header.biClrUsed = num_palette_entries;
		header.biClrImportant = 0;

		std::copy_n(palette, num_palette_entries, this->palette);
	}
};

// Global Variables:
HWND main_window = nullptr;
std::optional<coro_gb::cart> cart_instance;
std::optional<coro_gb::emu> emu_instance;
std::filesystem::path boot_rom_path = "dmg_rom.bin";
std::filesystem::path current_rom = "";
std::filesystem::path current_ram = "";

std::chrono::high_resolution_clock::time_point sync_time;
uint32_t sync_cycles;
bool speedup_active = false;

const TCHAR* appid = L"Thief.CoroGB.001";

ATOM register_window_class(HINSTANCE hInstance);
bool init(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
std::optional<std::filesystem::path> show_open_dialog(HWND hwnd_owner);
void display_callback();

int APIENTRY wWinMain(
	_In_     HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_     LPWSTR    lpCmdLine,
	_In_     int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	if (!init(hInstance, nCmdShow))
	{
		return FALSE;
	}

	try
	{
		int nArgs;
		LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &nArgs);

		if (nArgs >= 2)
		{
			if (std::filesystem::exists(argv[1]))
			{
				current_rom = argv[1];

				{
					IShellItem* shell_item;
					HRESULT hr = SHCreateItemFromParsingName(current_rom.c_str(), NULL, IID_PPV_ARGS(&shell_item));
					if (SUCCEEDED(hr))
					{
						SHARDAPPIDINFO info;
						info.psi = shell_item;
						info.pszAppID = appid;

						SHAddToRecentDocs(SHARD_APPIDINFO, &info);
						shell_item->Release();
					}
				}

				std::filesystem::path default_ram = current_rom;
				default_ram.replace_extension(".sav");
				current_ram = default_ram;

				InvalidateRect(main_window, nullptr, FALSE);

				emu_instance.emplace();
				emu_instance->set_display_callback(display_callback);
				emu_instance->load_boot_rom(boot_rom_path);
				cart_instance.emplace(current_rom, current_ram);
				emu_instance->load_cart(*cart_instance);

				emu_instance->start();

				sync_time = std::chrono::high_resolution_clock::now();
				sync_cycles = emu_instance->get_cycle_counter();

				SetWindowTextW(main_window, (L"CoroGB - " + current_rom.filename().generic_wstring()).c_str());
			}
		}

		// Main Message loop:
		MSG msg;
		while (GetMessage(&msg, nullptr, 0, 0))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);

			if (emu_instance)
			{
				while (!PeekMessage(&msg, nullptr, 0, 0, PM_NOREMOVE))
				{
					std::chrono::high_resolution_clock::time_point now_time = std::chrono::high_resolution_clock::now();
					uint32_t now_cycles = emu_instance->get_cycle_counter();

					coro_gb::cycles elapsed_time_in_cycles = std::chrono::duration_cast<coro_gb::cycles>(now_time - sync_time);

					if (speedup_active)
					{
						elapsed_time_in_cycles *= 4;
					}

					uint32_t elapsed_cycles = now_cycles - sync_cycles;
					elapsed_time_in_cycles -= coro_gb::cycles(elapsed_cycles);

					// don't allow falling more than one frame behind - if we do, resync the timers
					if (elapsed_time_in_cycles >= coro_gb::cycles(70'224))
					{
						sync_cycles = now_cycles;
						sync_time = now_time - std::chrono::duration_cast<std::chrono::high_resolution_clock::duration>(coro_gb::cycles(70'224));
					}

					if (elapsed_time_in_cycles >= coro_gb::cycles(5 * 456))
					{
						// tick at most 10 lines (4560 cycles) or we might miss the vsync
						emu_instance->tick(10 * 456);
					}
					else
					{
						DWORD wait_result = MsgWaitForMultipleObjectsEx(0, NULL, 1 /*ms*/, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
						if (wait_result != WAIT_TIMEOUT)
						{
							break;
						}
					}
				}
			}
		}

		return (int)msg.wParam;
	}
	catch (std::exception ex)
	{
		std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
		MessageBoxW(NULL, converter.from_bytes(ex.what()).c_str(), L"ERROR", MB_ICONERROR | MB_OK);
		throw;
	}
}

ATOM register_window_class(HINSTANCE hInstance)
{
	WNDCLASSEXW wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_GBEMU));
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = MAKEINTRESOURCE(IDR_MAINMENU);
	wcex.lpszClassName = L"CoroGB";
	wcex.hIconSm = nullptr;

	return RegisterClassExW(&wcex);
}

bool init(HINSTANCE hInstance, int nCmdShow)
{
	//// show console
	//AllocConsole();
	//freopen("CONIN$", "r", stdin);
	//freopen("CONOUT$", "w", stdout);
	//freopen("CONOUT$", "w", stderr);

	SetCurrentProcessExplicitAppUserModelID(appid);

	register_window_class(hInstance);

	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	main_window = CreateWindowExW(0, L"CoroGB", L"CoroGB", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, 0, 0, 0, nullptr, nullptr, hInstance, nullptr);

	if (!main_window)
	{
		return false;
	}

	HMONITOR window_monitor = MonitorFromWindow(main_window, MONITOR_DEFAULTTOPRIMARY);
	MONITORINFO monitor_info{sizeof(MONITORINFO)};
	GetMonitorInfoW(window_monitor, &monitor_info);
	UINT dpi = GetDpiForWindow(main_window);

	POINT monitor_center
	{
		(monitor_info.rcMonitor.left + monitor_info.rcMonitor.right) / 2,
		(monitor_info.rcMonitor.top + monitor_info.rcMonitor.bottom) / 2
	};
	SIZE window_size // 160 x 144 Pixels, x4 zoom, scaled by DPI
	{
		MulDiv(160 * 4, dpi, USER_DEFAULT_SCREEN_DPI),
		MulDiv(144 * 4, dpi, USER_DEFAULT_SCREEN_DPI)
	};

	RECT window_rect{};
	window_rect.left   = monitor_center.x - window_size.cx / 2;
	window_rect.top    = monitor_center.y - window_size.cy / 2;
	window_rect.right  = window_rect.left + window_size.cx;
	window_rect.bottom = window_rect.top  + window_size.cy;
	AdjustWindowRectExForDpi(&window_rect, WS_OVERLAPPEDWINDOW, true, 0, dpi);
	SetWindowPos(main_window,
		nullptr,
		window_rect.left,
		window_rect.top,
		window_rect.right  - window_rect.left,
		window_rect.bottom - window_rect.top,
		SWP_NOZORDER | SWP_NOACTIVATE);

	ShowWindow(main_window, nCmdShow);
	UpdateWindow(main_window);

	return true;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	switch (Message)
	{
		case WM_NCCREATE:
		{
			EnableNonClientDpiScaling(hWnd);
			return TRUE;
		}
		case WM_GETDPISCALEDSIZE:
		{
			UINT dpi = (UINT)wParam;
			SIZE* scaled_size = (SIZE*)lParam;

			RECT window_rect{};
			window_rect.right = MulDiv(160 * 4, dpi, USER_DEFAULT_SCREEN_DPI);
			window_rect.bottom = MulDiv(144 * 4, dpi, USER_DEFAULT_SCREEN_DPI);
			AdjustWindowRectExForDpi(&window_rect, WS_OVERLAPPEDWINDOW, true, 0, dpi);

			scaled_size->cx = window_rect.right - window_rect.left;
			scaled_size->cy = window_rect.bottom - window_rect.top;
			return TRUE;
		}
		case WM_DPICHANGED:
		{
			//g_dpi = HIWORD(wParam);

			RECT* const prcNewWindow = (RECT*)lParam;
			SetWindowPos(hWnd,
				NULL,
				prcNewWindow->left,
				prcNewWindow->top,
				prcNewWindow->right - prcNewWindow->left,
				prcNewWindow->bottom - prcNewWindow->top,
				SWP_NOZORDER | SWP_NOACTIVATE);
			return 0;
		}
		case WM_COMMAND:
		{
			int wmId = LOWORD(wParam);
			// Parse the menu selections:
			switch (wmId)
			{
				case ID_FILE_LOAD:
				{
					std::optional<std::filesystem::path> open_result = show_open_dialog(hWnd);
					if (open_result)
					{
						current_rom = *open_result;

						{
							IShellItem* shell_item;
							HRESULT hr = SHCreateItemFromParsingName(current_rom.c_str(), NULL, IID_PPV_ARGS(&shell_item));
							if (SUCCEEDED(hr))
							{
								SHARDAPPIDINFO info;
								info.psi = shell_item;
								info.pszAppID = appid;

								SHAddToRecentDocs(SHARD_APPIDINFO, &info);
								shell_item->Release();
							}
						}

						std::filesystem::path default_ram = current_rom;
						default_ram.replace_extension(".sav");
						current_ram = default_ram;

						InvalidateRect(main_window, nullptr, FALSE);

						emu_instance.emplace();
						emu_instance->set_display_callback(display_callback);
						emu_instance->load_boot_rom(boot_rom_path);
						cart_instance.emplace(current_rom, current_ram);
						emu_instance->load_cart(*cart_instance);

						emu_instance->start();

						sync_time = std::chrono::high_resolution_clock::now();
						sync_cycles = emu_instance->get_cycle_counter();

						SetWindowTextW(hWnd, (L"CoroGB - " + current_rom.filename().generic_wstring()).c_str());
					}
					return 0;
				}
				case ID_FILE_RESET:
				{
					InvalidateRect(main_window, nullptr, FALSE);

					emu_instance.emplace();
					emu_instance->set_display_callback(display_callback);
					emu_instance->load_boot_rom(boot_rom_path);
					//cart_instance.emplace(current_rom, current_ram); // don't need to reload cart
					emu_instance->load_cart(*cart_instance);

					emu_instance->start();

					sync_time = std::chrono::high_resolution_clock::now();
					sync_cycles = emu_instance->get_cycle_counter();

					SetWindowTextW(hWnd, (L"CoroGB - " + current_rom.filename().generic_wstring()).c_str());
					return 0;
				}
				case IDM_EXIT:
					DestroyWindow(hWnd);
					return 0;
			}
		}
		case WM_ERASEBKGND:
			return 0;
		case WM_KEYDOWN:
		{
			if (emu_instance)
			{
				bool repeating = (lParam & (1 << 30));
				if (!repeating)
				{
					if (wParam == VK_RIGHT)
					{
						emu_instance->input(coro_gb::button_id::right, coro_gb::button_state::down);
						return 0;
					}
					if (wParam == VK_LEFT)
					{
						emu_instance->input(coro_gb::button_id::left, coro_gb::button_state::down);
						return 0;
					}
					if (wParam == VK_UP)
					{
						emu_instance->input(coro_gb::button_id::up, coro_gb::button_state::down);
						return 0;
					}
					if (wParam == VK_DOWN)
					{
						emu_instance->input(coro_gb::button_id::down, coro_gb::button_state::down);
						return 0;
					}
					if (wParam == 'Z')
					{
						emu_instance->input(coro_gb::button_id::a, coro_gb::button_state::down);
						return 0;
					}
					if (wParam == 'X')
					{
						emu_instance->input(coro_gb::button_id::b, coro_gb::button_state::down);
						return 0;
					}
					if (wParam == VK_SHIFT)
					{
						emu_instance->input(coro_gb::button_id::select, coro_gb::button_state::down);
						return 0;
					}
					if (wParam == VK_RETURN)
					{
						emu_instance->input(coro_gb::button_id::start, coro_gb::button_state::down);
						return 0;
					}
				}
			}
			if (wParam == VK_ADD)
			{
				speedup_active = true;
				sync_time = std::chrono::high_resolution_clock::now();
				sync_cycles = emu_instance->get_cycle_counter();
				return 0;
			}
		}
		break;
		case WM_KEYUP:
		{
			if (emu_instance)
			{
				if (wParam == VK_RIGHT)
				{
					emu_instance->input(coro_gb::button_id::right, coro_gb::button_state::up);
					return 0;
				}
				if (wParam == VK_LEFT)
				{
					emu_instance->input(coro_gb::button_id::left, coro_gb::button_state::up);
					return 0;
				}
				if (wParam == VK_UP)
				{
					emu_instance->input(coro_gb::button_id::up, coro_gb::button_state::up);
					return 0;
				}
				if (wParam == VK_DOWN)
				{
					emu_instance->input(coro_gb::button_id::down, coro_gb::button_state::up);
					return 0;
				}
				if (wParam == 'Z')
				{
					emu_instance->input(coro_gb::button_id::a, coro_gb::button_state::up);
					return 0;
				}
				if (wParam == 'X')
				{
					emu_instance->input(coro_gb::button_id::b, coro_gb::button_state::up);
					return 0;
				}
				if (wParam == VK_SHIFT)
				{
					emu_instance->input(coro_gb::button_id::select, coro_gb::button_state::up);
					return 0;
				}
				if (wParam == VK_RETURN)
				{
					emu_instance->input(coro_gb::button_id::start, coro_gb::button_state::up);
					return 0;
				}
			}
			if (wParam == VK_ADD)
			{
				speedup_active = false;
				sync_time = std::chrono::high_resolution_clock::now();
				sync_cycles = emu_instance->get_cycle_counter();
				return 0;
			}
		}
		break;

		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hdc;
			hdc = BeginPaint(hWnd, &ps);

			int Width = ps.rcPaint.right - ps.rcPaint.left;
			int Height = ps.rcPaint.bottom - ps.rcPaint.top;

			if (emu_instance && emu_instance->is_screen_enabled())
			{
				bitmap_info<4, 8> bmi(160, 144, (RGBQUAD*)emu_instance->get_palette());

				int result = StretchDIBits(hdc,
					ps.rcPaint.left, ps.rcPaint.top,
					Width, Height,
					0, 0, 160, 144,
					emu_instance->get_screen_buffer(), (BITMAPINFO*)&bmi, DIB_RGB_COLORS, SRCCOPY);
			}
			else
			{
				HBRUSH hbr = CreateSolidBrush(RGB(240, 247, 179));
				int result = FillRect(hdc, &ps.rcPaint, hbr);
				DeleteObject(hbr);
			}

			EndPaint(hWnd, &ps);

			return 0;
		}
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
	}
	return DefWindowProc(hWnd, Message, wParam, lParam);
}

std::optional<std::filesystem::path> show_open_dialog(HWND hwnd_owner)
{
	HRESULT hr;

	// Create the File Open Dialog object
	std::unique_ptr<IFileDialog, void(*)(IFileDialog*)> file_dialog = { nullptr, nullptr };
	{
		IFileDialog* pfd;
		hr = CoCreateInstance(CLSID_FileOpenDialog,
			NULL,
			CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&pfd));
		assert(SUCCEEDED(hr));
		file_dialog = { pfd, [](IFileDialog* del) { del->Release(); } };
	}

	DWORD dwFlags;
	hr = file_dialog->GetOptions(&dwFlags);
	assert(SUCCEEDED(hr));
	hr = file_dialog->SetOptions(dwFlags | FOS_FORCEFILESYSTEM);
	assert(SUCCEEDED(hr));

	// Set the file types to display
	COMDLG_FILTERSPEC file_types[2] = { {L".gb rom file", L"*.gb"}, { L"All Files", L"*.*" } };
	hr = file_dialog->SetFileTypes(2, file_types);
	assert(SUCCEEDED(hr));
	hr = file_dialog->SetFileTypeIndex(1);
	assert(SUCCEEDED(hr));
	hr = file_dialog->SetDefaultExtension(L"gb");
	assert(SUCCEEDED(hr));

	// Show the dialog
	hr = file_dialog->Show(hwnd_owner);
	if (!SUCCEEDED(hr))
	{
		return std::nullopt;
	}

	// Get the result
	std::unique_ptr<IShellItem, void(*)(IShellItem*)> dialog_result = { nullptr, nullptr };
	{
		IShellItem *psiResult;
		hr = file_dialog->GetResult(&psiResult);
		dialog_result = { psiResult, [](IShellItem* del) { del->Release(); } };
		assert(SUCCEEDED(hr));
	}
	PWSTR pszFilePath = NULL;
	hr = dialog_result->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
	if (SUCCEEDED(hr))
	{
		return { pszFilePath };
	}

	return std::nullopt;
}

void display_callback()
{
	InvalidateRect(main_window, nullptr, FALSE);
}
