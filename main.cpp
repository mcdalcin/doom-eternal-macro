#include <cassert>
#include <iostream>
#include <string>

#include <stdlib.h>
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

using namespace std::literals;

namespace {
  HHOOK mouseHook;
  HHOOK keyboardHook;

  DWORD downKeyCode = 0;
  DWORD upKeyCode = 0;

  bool spamUp = false;
  bool spamDown = false;

  int upKeyRepeatCount = 0;
  int downKeyRepeatCount = 0;

  void waitForUserToExit() {
    std::cout << "Press Enter to exit . . .";
    std::cin.get();
  }

  bool isGameInFocus() {
    auto window = GetForegroundWindow();

    constexpr int bufferSize = 35;
    wchar_t buffer[bufferSize];

    std::wstring_view str = {buffer, static_cast<std::size_t>(GetClassNameW(window, buffer, bufferSize))};
									   
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

    if ((!spamUp && !spamDown) || !isGameInFocus()) {
      return false;
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
      case WM_MOUSEWHEEL: {
        if (spamUp || spamDown) {
          MSLLHOOKSTRUCT* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
          bool scrollingDown = static_cast<std::make_signed_t<WORD>>(HIWORD(info->mouseData)) < 0;

          // If macro is active prevent manual scrolling of the opposite direction for proper freescroll emulation
          if (spamDown == !scrollingDown || spamUp == scrollingDown) {
            return 1;
          }        
        }
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
  
    switch (wParam) {
      case WM_KEYDOWN: {
        [[fallthrough]];
      }
      case WM_SYSKEYDOWN: {
        if (info.vkCode == upKeyCode) {
          upKeyRepeatCount++;
        }
        else if (info.vkCode == downKeyCode) {
          downKeyRepeatCount++;
        }
        break;
      }
      case WM_KEYUP: {
        [[fallthrough]];
      }
      case WM_SYSKEYUP: {
        if (info.vkCode == upKeyCode) {
          upKeyRepeatCount = 0;
        }
        else if (info.vkCode == downKeyCode) {
          downKeyRepeatCount = 0;
        }
        break;
      }
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

  void CALLBACK TimerProc(void*, BOOLEAN) {
    if (isGameInFocus()) {
      auto wheelDelta = getWheelDelta();

      if (wheelDelta != 0) {
        mouse_event(MOUSEEVENTF_WHEEL, 0, 0, wheelDelta, 0);
      } 
    } else {
      spamUp = false;
      spamDown = false;
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
  void readConfiguration() {
    auto fileName = "bindings.txt";
    unique_file file(std::fopen(fileName, "rb"));
    if (!file) {
      throw std::runtime_error("Unable to open configuration file \""s + fileName + "\".");
    }

    auto keyCodesRead = std::fscanf(file.get(), "%li %li", &downKeyCode, &upKeyCode);

    if (keyCodesRead < 1) {
      throw std::runtime_error("Unable to read key code from configuration file \""s + fileName + "\".");
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
            throw std::runtime_error("Invalid wheel direction '+"s + wheelDirection + "+' in configuration file \"" + fileName + "\".");
          }
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

  bool isMouseButton(DWORD keyCode) {
    return (VK_LBUTTON <= keyCode && keyCode <= VK_RBUTTON) || (VK_MBUTTON <= keyCode && keyCode <= VK_XBUTTON2);
  }

  bool isKeyboardKey(DWORD keyCode) {
    return keyCode != 0 && !isMouseButton(keyCode);
  }
}

int strcompare(const char* firstString, const char* secondString, bool caseSensitive) {
#if defined _WIN32 || defined _WIN64
  return caseSensitive ? strcmp(firstString, secondString) : _stricmp(firstString, secondString);
#else
  return caseSensitive ? strcmp(firstString, secondString) : strcasecmp(firstString, secondString);
#endif
}

MODULEENTRY32 getModuleInfo(std::uint32_t processID, const char* moduleName) {
  void* hSnap = nullptr;
  MODULEENTRY32 mod32 = {0};

  if ((hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, processID)) == INVALID_HANDLE_VALUE) {
    return mod32;
  }

  mod32.dwSize = sizeof(MODULEENTRY32);
  while (Module32Next(hSnap, &mod32)) {
    if (!strcompare(moduleName, mod32.szModule, false)) {
      CloseHandle(hSnap);
      return mod32;
    }
  }

  CloseHandle(hSnap);
  mod32 = {0};
  return mod32;
}

uintptr_t getPointerByBaseSize(DWORD baseSize, uintptr_t pBaseAddr) {
  uintptr_t pointer = NULL;
  std::string platform = "unknown";

  if (baseSize == 507191296 || baseSize == 515133440 || baseSize == 510681088) { // Old Steam versions
    platform = "Steam";
    pointer = pBaseAddr + 0x25B4A80;
  } else if (baseSize == 450445312 || baseSize == 444944384) { // Old Bethesda versions
    platform = "Bethesda";
    pointer = pBaseAddr + 0x25818C0;
  }
  
  // Commented out because of uncertainties with Denuvo AC running even in Singleplayer on the current version
  /*else if (baseSize == 546783232) { // Current Steam version
    platform = "Steam";
    pointer = pBaseAddr + 0x2607348;
  } else if (baseSize == 455708672) { // Current Bethesda version
    platform = "Bethesda";
    pointer = pBaseAddr + 0x25D4188;
  }*/

  std::cout << "Found " << platform << " game version (" << baseSize << ")" << std::endl;

  return pointer;
}

bool writeBytesToFPSRow(HANDLE processHandle, uintptr_t pFPSRow) {
  char bytesToWrite[] = "(M) %i FPS";

  DWORD oldProtection;
  VirtualProtectEx(processHandle, (LPVOID)pFPSRow, 10, PAGE_READWRITE, &oldProtection);
  if (!WriteProcessMemory(processHandle, (LPVOID)pFPSRow, bytesToWrite, strlen(bytesToWrite) + 1, nullptr)) {
    throw std::runtime_error("Couldn't modify FPS counter display, exiting... (Code " + std::to_string(GetLastError()) + ")");

    return false;
  }
  VirtualProtectEx(processHandle, (LPVOID)pFPSRow, 10, oldProtection, nullptr);

  return true;
}

bool modifyFPSDisplay() {
  PROCESSENTRY32 entry;
  entry.dwSize = sizeof(PROCESSENTRY32);

  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);

  if (Process32First(snapshot, &entry) == TRUE) {
    while (Process32Next(snapshot, &entry) == TRUE) {
      if (stricmp(entry.szExeFile, "DOOMEternalx64vk.exe") == 0) {
        HANDLE processHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, entry.th32ProcessID);
        MODULEENTRY32 moduleEntry = getModuleInfo(entry.th32ProcessID, "DOOMEternalx64vk.exe");

        uintptr_t pFPSRow = getPointerByBaseSize(moduleEntry.modBaseSize, (uintptr_t) moduleEntry.modBaseAddr);
        if (pFPSRow != NULL) {
          return writeBytesToFPSRow(processHandle, pFPSRow);
        }

        return true;
      }
    }

    throw std::runtime_error("Game is not running. Make sure to start the game first, then the macro.");
  }

  return false;
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
  DWORD period = 10; // Execute every 10 milliseconds, 100 times a second.

  HANDLE timer;
  if (!CreateTimerQueueTimer(&timer, nullptr, TimerProc, nullptr, 0, period, WT_EXECUTEDEFAULT)) {
    throw std::system_error(GetLastError(), std::system_category());
  }

  while (true) {
    MSG message;
    while (GetMessageW(&message, 0, 0, 0)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }
}

int main(int argc, char** argv) {
  try {
    LPCTSTR consoleTitle = "DOOM Eternal Freescroll Macro";
    SetConsoleTitle(consoleTitle);

    if (modifyFPSDisplay()) {
      readConfiguration();
      setupInputHooks();

      std::cout << "Macro is running...\n";

      startTimer();
    }
  } catch (std::exception& e) {
    std::fprintf(stderr, "Error: %s\n\n", e.what());    
    waitForUserToExit();

    return 1;
  }
}