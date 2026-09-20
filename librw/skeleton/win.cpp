#ifdef _WIN32
#include <windows.h>
#include <rw.h>
#include "skeleton.h"
#include <shellapi.h>

using namespace sk;
using namespace rw;

#ifdef RW_D3D9

#ifndef VK_OEM_NEC_EQUAL
#define VK_OEM_NEC_EQUAL 0x92
#endif

static int keymap[256];
static void
initkeymap(void)
{
	int i;
	for(i = 0; i < 256; i++)
		keymap[i] = KEY_NULL;
	keymap[VK_SPACE] = ' ';
	keymap[VK_OEM_7] = '\'';
	keymap[VK_OEM_COMMA] = ',';
	keymap[VK_OEM_MINUS] = '-';
	keymap[VK_OEM_PERIOD] = '.';
	keymap[VK_OEM_2] = '/';
	for(i = '0'; i <= '9'; i++)
		keymap[i] = i;
	keymap[VK_OEM_1] = ';';
	keymap[VK_OEM_NEC_EQUAL] = '=';
	for(i = 'A'; i <= 'Z'; i++)
		keymap[i] = i;
	keymap[VK_OEM_4] = '[';
	keymap[VK_OEM_5] = '\\';
	keymap[VK_OEM_6] = ']';
	keymap[VK_OEM_3] = '`';
	keymap[VK_ESCAPE] = KEY_ESC;
	keymap[VK_RETURN] = KEY_ENTER;
	keymap[VK_TAB] = KEY_TAB;
	keymap[VK_BACK] = KEY_BACKSP;
	keymap[VK_INSERT] = KEY_INS;
	keymap[VK_DELETE] = KEY_DEL;
	keymap[VK_RIGHT] = KEY_RIGHT;
	keymap[VK_LEFT] = KEY_LEFT;
	keymap[VK_DOWN] = KEY_DOWN;
	keymap[VK_UP] = KEY_UP;
	keymap[VK_PRIOR] = KEY_PGUP;
	keymap[VK_NEXT] = KEY_PGDN;
	keymap[VK_HOME] = KEY_HOME;
	keymap[VK_END] = KEY_END;
	keymap[VK_MODECHANGE] = KEY_CAPSLK;
	for(i = VK_F1; i <= VK_F24; i++)
		keymap[i] = i-VK_F1+KEY_F1;
	keymap[VK_LSHIFT] = KEY_LSHIFT;
	keymap[VK_LCONTROL] = KEY_LCTRL;
	keymap[VK_LMENU] = KEY_LALT;
	keymap[VK_RSHIFT] = KEY_RSHIFT;
	keymap[VK_RCONTROL] = KEY_RCTRL;
	keymap[VK_RMENU] = KEY_RALT;
	keymap[VK_LWIN] = KEY_LSUPER;
	keymap[VK_RWIN] = KEY_RSUPER;
}
bool running;
static bool inSizeMove, sizePending;	// gtacheck: a border drag in progress / a WM_SIZE not yet applied (see WM_SIZE)
static rw::Rect pendingRect;

// gtacheck: a frameless window whose top strip is the caption. The application sets customFrame and
// the strip height (client pixels) plus rectangles inside the strip that stay ordinary client area
// (its own buttons / fields); the resize borders are 8 px along the edges. Toggled with
// SetWindowPos(SWP_FRAMECHANGED) so WM_NCCALCSIZE is asked again.
namespace sk { int customFrame = 0, captionH = 0, captionExclN = 0; long captionExcl[8][4]; }

static void KeyUp(int key) { EventHandler(KEYUP, &key); }
static void KeyDown(int key) { EventHandler(KEYDOWN, &key); }

LRESULT CALLBACK
WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int resizing = 0;
	static int buttons = 0;
	POINTS p;

	MouseState ms;
	switch(msg){
	case WM_DESTROY:
		PostQuitMessage(0);
		break;

	case WM_SYSKEYDOWN:
	case WM_KEYDOWN:
		if(wParam == VK_MENU){
			if(GetKeyState(VK_LMENU) & 0x8000) KeyDown(keymap[VK_LMENU]);
			if(GetKeyState(VK_RMENU) & 0x8000) KeyDown(keymap[VK_RMENU]);
		}else if(wParam == VK_CONTROL){
			if(GetKeyState(VK_LCONTROL) & 0x8000) KeyDown(keymap[VK_LCONTROL]);
			if(GetKeyState(VK_RCONTROL) & 0x8000) KeyDown(keymap[VK_RCONTROL]);
		}else if(wParam == VK_SHIFT){
			if(GetKeyState(VK_LSHIFT) & 0x8000) KeyDown(keymap[VK_LSHIFT]);
			if(GetKeyState(VK_RSHIFT) & 0x8000) KeyDown(keymap[VK_RSHIFT]);
		}else
			KeyDown(keymap[wParam]);
		break;

	case WM_SYSKEYUP:
	case WM_KEYUP:
		if(wParam == VK_MENU){
			if((GetKeyState(VK_LMENU) & 0x8000) == 0) KeyUp(keymap[VK_LMENU]);
			if((GetKeyState(VK_RMENU) & 0x8000) == 0) KeyUp(keymap[VK_RMENU]);
		}else if(wParam == VK_CONTROL){
			if((GetKeyState(VK_LCONTROL) & 0x8000) == 0) KeyUp(keymap[VK_LCONTROL]);
			if((GetKeyState(VK_RCONTROL) & 0x8000) == 0) KeyUp(keymap[VK_RCONTROL]);
		}else if(wParam == VK_SHIFT){
			if((GetKeyState(VK_LSHIFT) & 0x8000) == 0) KeyUp(keymap[VK_LSHIFT]);
			if((GetKeyState(VK_RSHIFT) & 0x8000) == 0) KeyUp(keymap[VK_RSHIFT]);
		}else
			KeyUp(keymap[wParam]);
		break;

	case WM_CHAR:
		if(wParam > 0 && wParam < 0x10000)
			EventHandler(CHARINPUT, (void*)wParam);
		break;

	case WM_MOUSEMOVE:
		p = MAKEPOINTS(lParam);
		ms.posx = p.x;
		ms.posy = p.y;
		EventHandler(MOUSEMOVE, &ms);
		break;

	case WM_LBUTTONDOWN:
		buttons |= 1; goto mbtn;
	case WM_LBUTTONUP:
		buttons &= ~1; goto mbtn;
	case WM_MBUTTONDOWN:
		buttons |= 2; goto mbtn;
	case WM_MBUTTONUP:
		buttons &= ~2; goto mbtn;
	case WM_RBUTTONDOWN:
		buttons |= 4; goto mbtn;
	case WM_RBUTTONUP:
		buttons &= ~4;
	mbtn:
		ms.buttons = buttons;
		EventHandler(MOUSEBTN, &ms);
		break;

	case WM_MOUSEWHEEL:
		ms.scrollx = 0.0f;
		ms.scrolly = (float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA;
		EventHandler(MOUSESCROLL, &ms);
		break;

	case WM_MOUSEHWHEEL:
		ms.scrollx = -(float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA;
		ms.scrolly = 0.0f;
		EventHandler(MOUSESCROLL, &ms);
		break;

	case WM_NCCALCSIZE:
		if(customFrame && wParam){
			NCCALCSIZE_PARAMS *ncp = (NCCALCSIZE_PARAMS*)lParam;
			if(IsZoomed(hwnd)){	// maximised: pull the client in by the frame, or it hangs off the screen
				int fx = GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
				int fy = GetSystemMetrics(SM_CYSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
				ncp->rgrc[0].left += fx; ncp->rgrc[0].right -= fx; ncp->rgrc[0].top += fy; ncp->rgrc[0].bottom -= fy;
			}
			return 0;	// the whole window is client area: no caption, no visible frame
		}
		break;

	case WM_NCHITTEST:
		if(customFrame){
			POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
			ScreenToClient(hwnd, &pt);
			RECT rc; GetClientRect(hwnd, &rc);
			if(!IsZoomed(hwnd)){
				const int b = 8;
				bool l = pt.x < b, r = pt.x >= rc.right - b, t = pt.y < b, bo = pt.y >= rc.bottom - b;
				if(t && l) return HTTOPLEFT; if(t && r) return HTTOPRIGHT; if(bo && l) return HTBOTTOMLEFT; if(bo && r) return HTBOTTOMRIGHT;
				if(l) return HTLEFT; if(r) return HTRIGHT; if(t) return HTTOP; if(bo) return HTBOTTOM;
			}
			if(pt.y < captionH){
				for(int i = 0; i < captionExclN; i++) if(pt.x >= captionExcl[i][0] && pt.y >= captionExcl[i][1] && pt.x < captionExcl[i][2] && pt.y < captionExcl[i][3]) return HTCLIENT;
				return HTCAPTION;
			}
			return HTCLIENT;
		}
		break;

	case WM_GETMINMAXINFO: {	// the window never gets narrower than the header row (gtacheck: 1100×700 for now)
		MINMAXINFO *mmi = (MINMAXINFO*)lParam;
		mmi->ptMinTrackSize.x = 1100; mmi->ptMinTrackSize.y = 700;
		return 0;
	}

	case WM_SIZE:
		rw::Rect r;
		r.x = 0;
		r.y = 0;
		r.w = LOWORD(lParam);
		r.h = HIWORD(lParam);
		// gtacheck: while the user drags a border every WM_SIZE would recreate the camera raster = a D3D9
		// device Reset (all DEFAULT-pool textures rebuilt) per mouse move — flicker and lag with the 3D map
		// open. The size is kept pending: applied at most every 400 ms by the timer (the back buffer is
		// stretched to the client area meanwhile) and for sure when the drag ends.
		if(inSizeMove){ pendingRect = r; sizePending = true; if(running){ float dt = 0.016f; EventHandler(IDLE, &dt); } break; }	// a stretched frame right away: the new strip of window is never left unpainted
		EventHandler(RESIZE, &r);
		break;

	// gtacheck: dragging / resizing runs inside DefWindowProc's modal loop, where WinMain's frame loop does
	// not turn — the parts of the window coming back from off-screen showed the class brush and the check's
	// progress froze until the mouse was released. A 16 ms timer keeps drawing frames meanwhile.
	case WM_ERASEBKGND:
		if(running) return 1;	// gtacheck: nothing to erase — every frame covers the client area (the brush erase was the black flicker on resize)
		break;
	case WM_ENTERSIZEMOVE:
		inSizeMove = true;
		rw::d3d::setSizeLock(true);	// the back buffer keeps its size during the drag, Present stretches it; the real resize comes from the timer / at the end
		SetTimer(hwnd, 1, 16, nil);
		break;
	case WM_EXITSIZEMOVE:
		KillTimer(hwnd, 1);
		inSizeMove = false;
		rw::d3d::setSizeLock(false);
		if(sizePending){ sizePending = false; EventHandler(RESIZE, &pendingRect); }
		break;
	case WM_TIMER:
		if(wParam == 1 && running){
			static INT64 last, lastResize; INT64 now, freq;
			QueryPerformanceCounter((LARGE_INTEGER*)&now); QueryPerformanceFrequency((LARGE_INTEGER*)&freq);
			float dt = last ? (float)(now - last) / freq : 0.016f; if(dt > 0.1f) dt = 0.1f;
			last = now;
			if(sizePending && (double)(now - lastResize) / freq >= 0.4){ sizePending = false; lastResize = now; rw::d3d::setSizeLock(false); EventHandler(RESIZE, &pendingRect); rw::d3d::setSizeLock(true); return 0; }	// RESIZE draws a frame itself (with the Reset)
			EventHandler(IDLE, &dt);
			return 0;
		}
		break;

	case WM_CLOSE:
		DestroyWindow(hwnd);
		break;

	case WM_DROPFILES: {
		HDROP drop = (HDROP)wParam;
		UINT numFiles = DragQueryFileW(drop, 0xFFFFFFFF, nil, 0);
		wchar_t wpath[MAX_PATH];
		char path[MAX_PATH * 3];
		for(UINT i = 0; i < numFiles; i++){
			// UTF-8, as every other path in the application (the ANSI variant mangled non-Latin names)
			if(DragQueryFileW(drop, i, wpath, MAX_PATH) > 0 && WideCharToMultiByte(CP_UTF8, 0, wpath, -1, path, sizeof(path), nil, nil) > 0)
				EventHandler(FILEDROP, path);
		}
		DragFinish(drop);
		break;
	}

	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
			return 0;
		break;

	case WM_QUIT:
		running = false;
		break;
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

HWND
MakeWindow(HINSTANCE instance, int width, int height, const char *title)
{
	WNDCLASS wc;
	wc.style         = 0;	// gtacheck: CS_HREDRAW|CS_VREDRAW invalidated the whole window on every size change → the class brush flashed through before the next Present
	wc.lpfnWndProc   = WndProc;
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = 0;
	wc.hInstance     = instance;
	wc.hIcon         = LoadIcon(instance, MAKEINTRESOURCE(1));	// gtacheck: the exe's own icon (resource 1 in gtacheck.rc)
	if(wc.hIcon == nil) wc.hIcon = LoadIcon(0, IDI_APPLICATION);
	wc.hCursor       = LoadCursor(0, IDC_ARROW);
	wc.hbrBackground = CreateSolidBrush(RGB(17, 17, 17));	// gtacheck: exposed areas before the first frame are dark, not white (every theme is dark)
	wc.lpszMenuName  = 0;
	wc.lpszClassName = "librwD3D9";
	if(!RegisterClass(&wc)){
		MessageBox(0, "RegisterClass() - FAILED", 0, 0);
		return 0;
	}

	int offx = globals.posx >= 0 ? globals.posx : 100;	// gtacheck: remembered position
	int offy = globals.posy >= 0 ? globals.posy : 100;
	RECT rect;
	rect.left = 0;
	rect.top = 0;
	rect.right = width;
	rect.bottom = height;
	DWORD style = WS_OVERLAPPEDWINDOW;
	AdjustWindowRect(&rect, style, FALSE);
	rect.right += -rect.left;
	rect.bottom += -rect.top;
	HWND win;
	win = CreateWindow("librwD3D9", title, style,
		offx, offy, rect.right, rect.bottom, 0, 0, instance, 0);
	if(!win){
		MessageBox(0, "CreateWindow() - FAILED", 0, 0);
		return 0;
	}
	DragAcceptFiles(win, TRUE);
	// GTACHECK_HIDDEN=1: headless self-test — the D3D device still needs a window, but it never shows
	if(!GetEnvironmentVariableA("GTACHECK_HIDDEN", nil, 0)){
		ShowWindow(win, globals.maximized ? SW_MAXIMIZE : SW_SHOW);
		UpdateWindow(win);
	}
	return win;
}

void
pollEvents(void)
{
	MSG msg;
	while(PeekMessage(&msg, 0, 0, 0, PM_REMOVE)){
		if(msg.message == WM_QUIT){
			running = false;
			break;
		}else{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
}

int WINAPI
WinMain(HINSTANCE instance, HINSTANCE,
        PSTR cmdLine, int showCmd)
{
/*
	AllocConsole();
	freopen("CONIN$", "r", stdin);
	freopen("CONOUT$", "w", stdout);
	freopen("CONOUT$", "w", stderr);
*/

	INT64 ticks;
	INT64 ticksPerSecond;
	if(!QueryPerformanceFrequency((LARGE_INTEGER*)&ticksPerSecond))
		return 0;
	if(!QueryPerformanceCounter((LARGE_INTEGER*)&ticks))
		return 0;

#if defined(__MINGW32__) && !defined(__MINGW64_VERSION_MAJOR)
	args.argc = _argc;
	args.argv = _argv;
#else
	args.argc = __argc;
	args.argv = __argv;
#endif

	if(EventHandler(INITIALIZE, nil) == EVENTERROR)
		return 0;

	HWND win = MakeWindow(instance,
		sk::globals.width, sk::globals.height,
		sk::globals.windowtitle);
	if(win == 0){
		MessageBox(0, "MakeWindow() - FAILED", 0, 0);
		return 0;
	}
	engineOpenParams.window = win;
	initkeymap();

	if(EventHandler(RWINITIALIZE, nil) == EVENTERROR)
		return 0;

	INT64 lastTicks;
	QueryPerformanceCounter((LARGE_INTEGER *)&lastTicks);
	running = true;
	while((pollEvents(), running) && !globals.quit){
		QueryPerformanceCounter((LARGE_INTEGER *)&ticks);
		float timeDelta = (float)(ticks - lastTicks)/ticksPerSecond;

		EventHandler(IDLE, &timeDelta);

		lastTicks = ticks;
	}

	EventHandler(RWTERMINATE, nil);

	return 0;
}

namespace sk {

void
SetMousePosition(int x, int y)
{
	POINT pos = { x, y };
	ClientToScreen(engineOpenParams.window, &pos);
	SetCursorPos(pos.x, pos.y);
}

}

#endif

#ifdef RW_OPENGL
int main(int argc, char *argv[]);

int WINAPI
WinMain(HINSTANCE instance, HINSTANCE,
        PSTR cmdLine, int showCmd)
{
/*
	AllocConsole();
	freopen("CONIN$", "r", stdin);
	freopen("CONOUT$", "w", stdout);
	freopen("CONOUT$", "w", stderr);
*/

#if defined(__MINGW32__) && !defined(__MINGW64_VERSION_MAJOR)
	return main(_argc, _argv);
#else
	return main(__argc, __argv);
#endif
}
#endif
#endif
