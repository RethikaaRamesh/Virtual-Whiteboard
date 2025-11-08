#// Smart Power Saver (Dev-C++ Friendly)
// Platform: Windows (tested with MinGW/Dev-C++)
// Language: C++11
// Features:
//  - Reads battery % and charging state (GetSystemPowerStatus)
//  - Auto-brightness using gamma ramp (no extra libs; resolves SetDeviceGammaRamp at runtime)
//  - Beeps when battery is low (rate-limited)
//  - Logs status to ./logs/power_saver.log
// Build in Dev-C++:
//  - New Project → Console Application (C++) → paste this file and build.
//  - No special linker libs needed (SetDeviceGammaRamp loaded dynamically).

#include <windows.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cmath>

// ---------------- Utility ----------------
static std::string nowTimestamp() {
    SYSTEMTIME st; GetLocalTime(&st);
    char buf[64];
    wsprintfA(buf, "%04u-%02u-%02u %02u:%02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return std::string(buf);
}

static void ensureLogDir() {
    CreateDirectoryA("logs", NULL); // OK if exists
}

static void logLine(const std::string& line) {
    ensureLogDir();
    std::ofstream ofs("logs/power_saver.log", std::ios::app);
    ofs << line << "\n";
}

// -------------- Battery --------------
struct BatteryStatus { int percent; bool charging; bool present; };

static BatteryStatus getBatteryStatus() {
    SYSTEM_POWER_STATUS s; BatteryStatus b = { -1, false, false };
    if (GetSystemPowerStatus(&s)) {
        b.percent = (s.BatteryLifePercent == 255) ? -1 : (int)s.BatteryLifePercent;
        b.charging = (s.ACLineStatus == 1);
        b.present  = (s.BatteryFlag != 128);
    }
    return b;
}

// -------------- Brightness via Gamma Ramp --------------
// We avoid linking to gdi32 by loading SetDeviceGammaRamp dynamically.
// This adjusts display gamma (acts like brightness on many systems).
typedef BOOL (WINAPI *PFN_SetDeviceGammaRamp)(HDC, LPVOID);

static bool setBrightnessFactor(float factor01) {
    if (factor01 < 0.05f) factor01 = 0.05f; // avoid black screen
    if (factor01 > 1.0f)  factor01 = 1.0f;

    // Load function at runtime
    HMODULE hgdi = LoadLibraryA("gdi32.dll");
    if (!hgdi) return false;
    PFN_SetDeviceGammaRamp pSetGamma = (PFN_SetDeviceGammaRamp)GetProcAddress(hgdi, "SetDeviceGammaRamp");
    if (!pSetGamma) { FreeLibrary(hgdi); return false; }

    // Build ramp (simple linear scaling). 3 x 256 WORDs
    WORD ramp[3][256];
    for (int i = 0; i < 256; ++i) {
        // linear scale, then expand to 16-bit range
        int v = (int)(i * 256 * factor01);
        if (v < 0) v = 0; if (v > 65535) v = 65535;
        ramp[0][i] = ramp[1][i] = ramp[2][i] = (WORD)v;
    }

    HDC hdc = GetDC(NULL); // entire screen
    BOOL ok = pSetGamma(hdc, ramp);
    ReleaseDC(NULL, hdc);
    FreeLibrary(hgdi);
    return ok == TRUE;
}

// Convenience wrappers to set common levels
static bool setBrightnessNormal() { return setBrightnessFactor(1.00f); }   // ~100%
static bool setBrightnessMedium() { return setBrightnessFactor(0.75f); }   // ~75%
static bool setBrightnessLow()    { return setBrightnessFactor(0.50f); }   // ~50%

// -------------- Beep (rate-limited) --------------
static void beepAlert(int freqHz = 1000, int durationMs = 400) {
    Beep(freqHz, durationMs);
}

// -------------- Main --------------
int main() {
    std::cout << "Smart Power Saver (Dev-C++)\n";
    std::cout << "Ctrl+C to exit. Logs in ./logs/power_saver.log\n\n";

    // --- Tunables ---
    const int LOW_BATTERY_BEEP_PCT   = 20;  // Beep when battery <= this and on DC
    const int DIM_BRIGHTNESS_PCT     = 30;  // Dim when battery <= this and on DC
    const int POLL_INTERVAL_SECONDS  = 5;   // Check every N seconds
    const int BEEP_COOLDOWN_SECONDS  = 60;  // At most one beep per minute

    enum BrightnessState { BRIGHT_NORMAL, BRIGHT_MEDIUM, BRIGHT_LOW };
    BrightnessState lastState = BRIGHT_NORMAL;
    DWORD lastBeepTick = 0;

    // Start at normal brightness
    setBrightnessNormal();

    while (true) {
        BatteryStatus bat = getBatteryStatus();
        std::ostringstream oss;
        oss << "[" << nowTimestamp() << "] ";
        if (!bat.present) {
            oss << "Battery: NONE";
        } else {
            oss << "Battery: ";
            if (bat.percent >= 0) oss << bat.percent << "% "; else oss << "?% ";
            oss << (bat.charging ? "(Charging)" : "(On Battery)");
        }
        std::string line = oss.str();
        std::cout << line << std::endl;
        logLine(line);

        // Decide brightness target and beep behavior
        BrightnessState target = BRIGHT_NORMAL;
        if (bat.present && !bat.charging) {
            if (bat.percent >= 0 && bat.percent <= DIM_BRIGHTNESS_PCT) {
                target = BRIGHT_LOW;   // very low battery → dim more
            } else {
                target = BRIGHT_MEDIUM; // on battery but not too low → medium
            }

            if (bat.percent >= 0 && bat.percent <= LOW_BATTERY_BEEP_PCT) {
                DWORD now = GetTickCount();
                if (now - lastBeepTick >= (DWORD)(BEEP_COOLDOWN_SECONDS * 1000)) {
                    beepAlert();
                    lastBeepTick = now;
                    logLine("  Action: Low-battery beep");
                }
            }
        }

        if (target != lastState) {
            bool ok = false;
            if (target == BRIGHT_NORMAL) ok = setBrightnessNormal();
            else if (target == BRIGHT_MEDIUM) ok = setBrightnessMedium();
            else if (target == BRIGHT_LOW) ok = setBrightnessLow();

            if (ok) {
                if (target == BRIGHT_NORMAL) logLine("  Action: Brightness NORMAL (~100%)");
                else if (target == BRIGHT_MEDIUM) logLine("  Action: Brightness MEDIUM (~75%)");
                else logLine("  Action: Brightness LOW (~50%)");
            } else {
                logLine("  Action: Brightness change FAILED (driver may block gamma ramp)");
            }
            lastState = target;
        }

        Sleep(POLL_INTERVAL_SECONDS * 1000);
    }

    return 0;
}
