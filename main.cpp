#include <atomic>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

#include <cstdio>
#include <cwchar>
#include <cassert>

#include <windows.h>

using namespace std::literals;

namespace {

// Global variables are necessary, because Windows hooks have a terrible API.
HHOOK mouseHook;
HHOOK keyboardHook;

constexpr unsigned spamDownBit = 0x1;
constexpr unsigned spamUpBit = 0x2;
std::atomic<unsigned> spamFlags(0);

DWORD downKeyCode = 0;
DWORD upKeyCode = 0;

bool isGameInFocus() {
  auto window = GetForegroundWindow();

  constexpr int bufferSize = 35;
  wchar_t buffer[bufferSize];

  std::wstring_view str = {buffer, static_cast<std::size_t>(GetClassNameW(
                                       window, buffer, bufferSize))};
									   
  if (str != L"Ghost_CLASS") {
    return false;
  }

  POINT point;

  [[maybe_unused]] auto r = GetCursorPos(&point);
  assert(r);

  if (window != WindowFromPoint(point)) {
    return false;
  }

  return true;
}

bool handleKey(DWORD keyCode, bool isDown) {
  unsigned mask = 0;

  if (keyCode == downKeyCode) {
    mask = spamDownBit;
  } else if (keyCode == upKeyCode) {
    mask = spamUpBit;
  }

  if (mask == 0 || !isGameInFocus()) {
    return false;
  }

  if (isDown) {
    spamFlags |= mask;
  } else {
    spamFlags &= ~mask;
  }

  return true;
}

LRESULT CALLBACK LowLevelMouseProc(int code, WPARAM wParam, LPARAM lParam) {
  if (code < HC_ACTION) {
    return CallNextHookEx(mouseHook, code, wParam, lParam);
  }

  assert(code == HC_ACTION);

  DWORD keyCode = 0;
  bool isDown = false;
  switch (wParam) {
    case WM_LBUTTONDOWN: {
      isDown = true;

      [[fallthrough]];
    }
    case WM_LBUTTONUP: {
      keyCode = VK_LBUTTON;
      break;
    }
    case WM_RBUTTONDOWN: {
      isDown = true;

      [[fallthrough]];
    }
    case WM_RBUTTONUP: {
      keyCode = VK_RBUTTON;
      break;
    }
    case WM_MBUTTONDOWN: {
      isDown = true;

      [[fallthrough]];
    }
    case WM_MBUTTONUP: {
      keyCode = VK_MBUTTON;
      break;
    }
    case WM_XBUTTONDOWN: {
      isDown = true;

      [[fallthrough]];
    }
    case WM_XBUTTONUP: {
      auto& info = *reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
      keyCode = VK_XBUTTON1 + HIWORD(info.mouseData) - XBUTTON1;

      break;
    }
  }

  handleKey(keyCode, isDown);

  return CallNextHookEx(mouseHook, code, wParam, lParam);
}

LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam) {
  if (code < HC_ACTION) {
    return CallNextHookEx(keyboardHook, code, wParam, lParam);
  }

  assert(code == HC_ACTION);

  auto& info = *reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

  auto isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);

  handleKey(info.vkCode, isDown);

  return CallNextHookEx(keyboardHook, code, wParam, lParam);
}

DWORD getWheelDelta(unsigned flags) {
  DWORD r = 0;

  if ((flags & spamDownBit) != 0) {
    r -= WHEEL_DELTA;
  }

  if ((flags & spamUpBit) != 0) {
    r += WHEEL_DELTA;
  }

  return r;
}

void CALLBACK TimerProc(void*, BOOLEAN) {
  if (isGameInFocus()) {
    auto wheelDelta = getWheelDelta(spamFlags);

    if (wheelDelta != 0) {
      mouse_event(MOUSEEVENTF_WHEEL, 0, 0, wheelDelta, 0);
    }
  } else {
    spamFlags = 0;
  }
}

struct FileDeleter {
  void operator()(std::FILE* file) const noexcept { std::fclose(file); }
};

using unique_file = std::unique_ptr<std::FILE, FileDeleter>;

// There are a few valid possible configurations:
// <num>: <num> is the scroll down key.
// <num> <c>: <num> is the key for either scroll up or down depending on whether
// or not <c> is `u` or not.
// <num1> <num2>: <num1> is the scroll down key. <num2> is the scroll up key.
void readConfiguration(const char* fileName) {
  unique_file file(std::fopen(fileName, "rb"));
  if (!file) {
    throw std::runtime_error("Unable to open configuration file \""s +
                             fileName + "\".");
  }

  auto keyCodesRead =
      std::fscanf(file.get(), "%li %li", &downKeyCode, &upKeyCode);

  if (keyCodesRead < 1) {
    throw std::runtime_error(
        "Unable to read key code from configuration file \""s + fileName +
        "\".");
  }

  if (keyCodesRead < 2) {
    char wheelDirection = 'd';

    if (std::fscanf(file.get(), " %c", &wheelDirection) == 1) {
      switch (wheelDirection | 32) { // `|32` converts ASCII letters to lower case.
        case 'd':
          break;
        case 'u': {
          upKeyCode = downKeyCode;
          downKeyCode = 0;

          break;
        }
        default: {
          throw std::runtime_error(
              "Invalid wheel direction '+"s + wheelDirection +
              "+' in configuration file \"" + fileName + "\".");
        }
      }
    }
  }

  if (downKeyCode > VK_OEM_CLEAR) {
    throw std::runtime_error("Invalid key code " + std::to_string(downKeyCode) +
                             " in configuration file \""s + fileName + "\".");
  }

  if (upKeyCode > VK_OEM_CLEAR) {
    throw std::runtime_error("Invalid key code " + std::to_string(upKeyCode) +
                             " in configuration file \""s + fileName + "\".");
  }

  if (downKeyCode == upKeyCode) {
    if (downKeyCode == 0) {
      throw std::runtime_error(
          "No key was bound to either scroll direction in configuration file \""s +
          fileName + "\".");
    } else {
      throw std::runtime_error(
          "Scroll up and scroll down both have the same binding in configuration file \""s +
          fileName + "\".");
    }
  }
}

bool isMouseButton(DWORD keyCode) {
  return (VK_LBUTTON <= keyCode && keyCode <= VK_RBUTTON) ||
         (VK_MBUTTON <= keyCode && keyCode <= VK_XBUTTON2);
}

bool isKeyboardKey(DWORD keyCode) {
  return keyCode != 0 && !isMouseButton(keyCode);
}
}

int main(int argc, char** argv) {
  try {
    LPCTSTR consoleTitle = "DOOM Eternal Freescroll Macro";
    SetConsoleTitle(consoleTitle);

    auto configFilename = "bindings.txt";

    if (argc >= 2) {
      configFilename = argv[1];
    }

    readConfiguration(configFilename);

    if (isMouseButton(downKeyCode) || isMouseButton(upKeyCode)) {
      mouseHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, nullptr, 0);

      if (!mouseHook) {
        throw std::system_error(GetLastError(), std::system_category());
      }
    }

    if (isKeyboardKey(downKeyCode) || isKeyboardKey(upKeyCode)) {
      keyboardHook =
          SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, nullptr, 0);

      if (!keyboardHook) {
        throw std::system_error(GetLastError(), std::system_category());
      }
    }

    std::cout << "Macro is running...";

    // Execute every 10 milliseconds, 100 times a second.
    DWORD period = 10;

    HANDLE timer;
    if (!CreateTimerQueueTimer(&timer, nullptr, TimerProc, nullptr, 0, period,
                               WT_EXECUTEDEFAULT)) {
      throw std::system_error(GetLastError(), std::system_category());
    }

    for (;;) {
      MSG message;
      while (GetMessageW(&message, 0, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
    }
  } catch (std::exception& e) {
    std::fprintf(stderr, "Error: %s\n", e.what());
    std::getchar();

    return 1;
  }
}
