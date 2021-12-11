#include <cassert>
#include <iostream>
#include <string>

#include <Windows.h>
#include <Psapi.h>
#include <TlHelp32.h>

#pragma comment(lib, "winmm.lib")

using namespace std::literals;

namespace {
  const std::string DOOM_ETERNAL_WINDOW_NAME = "DOOMEternal";

  HHOOK mouseHook;
  HHOOK keyboardHook;

  DWORD downKeyCode = 0;
  DWORD upKeyCode = 0;

  std::atomic<bool> spamUp = false;
  std::atomic<bool> spamDown = false;

  int upKeyRepeatCount = 0;
  int downKeyRepeatCount = 0;

  UINT timerId;

  void waitForUserToExit() {
    std::cout << "Press Enter to exit . . .";
    std::cin.get();
  }

  bool isGameInFocus() {
    HWND window = GetForegroundWindow();
    char buffer[256];
    GetWindowText(window, buffer, sizeof(buffer));

    // Match any window starting with "DOOMEternal".
    std::string foregroundWindowText(buffer);
    return foregroundWindowText.substr(0, DOOM_ETERNAL_WINDOW_NAME.length()) == DOOM_ETERNAL_WINDOW_NAME;
  }

  void handleKey(const DWORD keyCode, const bool isDown) {
    if (keyCode == downKeyCode) {
      if (isDown) {
        spamUp = false;
        spamDown = true;
      } else {
        spamDown = false;
      }
    }

    if (keyCode == upKeyCode) {
      if (isDown) {
        spamDown = false;
        spamUp = true;
      } else {
        spamUp = false;
      }
    }
  }

  LRESULT CALLBACK LowLevelMouseProc(const int code, const WPARAM wParam, const LPARAM lParam) {
    if (code < HC_ACTION) {
      return CallNextHookEx(mouseHook, code, wParam, lParam);
    }

    assert(code == HC_ACTION);

    DWORD keyCode = 0;
    bool isDown = false;
    switch (wParam) {
      case WM_LBUTTONDOWN:
        isDown = true;

        [[fallthrough]];
      case WM_LBUTTONUP:
        keyCode = VK_LBUTTON;
        break;
      case WM_RBUTTONDOWN:
        isDown = true;

        [[fallthrough]];
      case WM_RBUTTONUP:
        keyCode = VK_RBUTTON;
        break;
      case WM_MBUTTONDOWN:
        isDown = true;

        [[fallthrough]];
      case WM_MBUTTONUP:
        keyCode = VK_MBUTTON;
        break;
      case WM_XBUTTONDOWN:
        isDown = true;

        [[fallthrough]];
      case WM_XBUTTONUP: {
        auto &info = *reinterpret_cast<MSLLHOOKSTRUCT *>(lParam);
        keyCode = VK_XBUTTON1 + HIWORD(info.mouseData) - XBUTTON1;
        break;
      }
      case WM_MOUSEWHEEL:
        if (spamUp || spamDown) {
          auto *info = reinterpret_cast<MSLLHOOKSTRUCT *>(lParam);
          const bool scrollingDown = static_cast<std::make_signed_t<WORD>>(HIWORD(info->mouseData)) < 0;

          // If macro is active prevent manual scrolling of the opposite direction for proper freescroll emulation.
          if (spamDown == !scrollingDown || spamUp == scrollingDown) {
            return 1;
          }
        }
        break;
    }

    handleKey(keyCode, isDown);

    return CallNextHookEx(mouseHook, code, wParam, lParam);
  }

  LRESULT CALLBACK LowLevelKeyboardProc(const int code, const WPARAM wParam, const LPARAM lParam) {
    if (code < HC_ACTION) {
      return CallNextHookEx(keyboardHook, code, wParam, lParam);
    }

    assert(code == HC_ACTION);

    auto &info = *reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);

    const bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);

    switch (wParam) {
      case WM_KEYDOWN:
        [[fallthrough]];
      case WM_SYSKEYDOWN:
        if (info.vkCode == upKeyCode) {
          upKeyRepeatCount++;
        } else if (info.vkCode == downKeyCode) {
          downKeyRepeatCount++;
        }
        break;
      case WM_KEYUP:
        [[fallthrough]];
      case WM_SYSKEYUP:
        if (info.vkCode == upKeyCode) {
          upKeyRepeatCount = 0;
        } else if (info.vkCode == downKeyCode) {
          downKeyRepeatCount = 0;
        }
        break;
      default:
        break;
    }

    if (info.vkCode == upKeyCode && upKeyRepeatCount <= 1 || info.vkCode == downKeyCode && downKeyRepeatCount <= 1) {
      handleKey(info.vkCode, isDown);
    }

    return CallNextHookEx(keyboardHook, code, wParam, lParam);
  }

  DWORD getWheelDelta() {
    DWORD r = 0;

    if (spamDown) {
      r -= WHEEL_DELTA;
    } else if (spamUp) {
      r += WHEEL_DELTA;
    }

    return r;
  }

  void CALLBACK TimerProc(UINT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR) {
    if (isGameInFocus()) {
      const DWORD wheelDelta = getWheelDelta();
      if (wheelDelta != 0) {
        mouse_event(MOUSEEVENTF_WHEEL, 0, 0, wheelDelta, 0);
      }
    } else {
      spamUp = false;
      spamDown = false;
    }
  }

  struct FileDeleter {
    void operator()(std::FILE *file) const noexcept {
      std::fclose(file);
    }
  };

  using unique_file = std::unique_ptr<std::FILE, FileDeleter>;

  // There are a few valid possible configurations:
  // <num>: <num> is the scroll down key.
  // <num> <c>: <num> is the key for either scroll up or down depending on whether
  // or not <c> is `u` or not.
  // <num1> <num2>: <num1> is the scroll down key. <num2> is the scroll up key.
  void readConfiguration() {
    const char *fileName = "bindings.txt";
    const unique_file file(std::fopen(fileName, "rb"));
    if (!file) {
      throw std::runtime_error("Unable to open configuration file \""s + fileName + "\".");
    }

    const int keyCodesRead = std::fscanf(file.get(), "%li %li", &downKeyCode, &upKeyCode);

    if (keyCodesRead < 1) {
      throw std::runtime_error("Unable to read key code from configuration file \""s + fileName + "\".");
    }

    if (keyCodesRead < 2) {
      char wheelDirection = 'd';

      if (std::fscanf(file.get(), " %c", &wheelDirection) == 1) {
        switch (wheelDirection | 32) { // `|32` converts ASCII letters to lower case.
          case 'd':
            break;
          case 'u':
            upKeyCode = downKeyCode;
            downKeyCode = 0;
            break;
          default:
            throw std::runtime_error("Invalid wheel direction '+"s + wheelDirection + "+' in configuration file \"" + fileName + "\".");
        }
      }
    }

    if (downKeyCode > VK_OEM_CLEAR) {
      throw std::runtime_error("Invalid key code " + std::to_string(downKeyCode) + " in configuration file \""s + fileName + "\".");
    }

    if (upKeyCode > VK_OEM_CLEAR) {
      throw std::runtime_error("Invalid key code " + std::to_string(upKeyCode) + " in configuration file \""s + fileName + "\".");
    }

    if (downKeyCode == upKeyCode) {
      if (downKeyCode == 0) {
        throw std::runtime_error("No key was bound to either scroll direction in configuration file \""s + fileName + "\".");
      } else {
        throw std::runtime_error("Scroll up and scroll down both have the same binding in configuration file \""s + fileName + "\".");
      }
    }
  }

  bool isMouseButton(const DWORD keyCode) {
    return (VK_LBUTTON <= keyCode && keyCode <= VK_RBUTTON) || (VK_MBUTTON <= keyCode && keyCode <= VK_XBUTTON2);
  }

  bool isKeyboardKey(const DWORD keyCode) {
    return keyCode != 0 && !isMouseButton(keyCode);
  }
}

void setupMouseHook() {
  if (isMouseButton(downKeyCode) || isMouseButton(upKeyCode)) {
    mouseHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, nullptr, 0);

    if (!mouseHook) {
      throw std::system_error(GetLastError(), std::system_category());
    }
  }
}

void setupKeyboardHook() {
  if (isKeyboardKey(downKeyCode) || isKeyboardKey(upKeyCode)) {
    keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, nullptr, 0);

    if (!keyboardHook) {
      throw std::system_error(GetLastError(), std::system_category());
    }
  }
}

void setupInputHooks() {
  setupMouseHook();
  setupKeyboardHook();
}

void startTimer() {
  const DWORD period = 10; // Execute every 10 milliseconds, 100 times a second.
  timerId = timeSetEvent(period, 0, TimerProc, 0, TIME_PERIODIC);

  while (true) {
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }
}

int main(int argc, char **argv) {
  try {
    const LPCTSTR consoleTitle = "DOOM Eternal Freescroll Macro";
    SetConsoleTitle(consoleTitle);

    readConfiguration();
    setupInputHooks();
    std::cout << "Macro is running...\n";
    startTimer();
  } catch (std::exception &e) {
    std::fprintf(stderr, "Error: %s\n\n", e.what());
    waitForUserToExit();
  }

  timeKillEvent(timerId);
  
  return 1;
}
