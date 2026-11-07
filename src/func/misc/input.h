// input.h - physical keyboard state and synthetic scan-code input
#pragma once

#include <windows.h>

// Installs the low-level keyboard hook used to tell physical keys apart from the
// scan codes this process injects. Returns false when the hook cannot be set.
bool InputEnsureKeyboardHook();

// True while the low-level keyboard hook is installed.
bool InputKeyboardHookInstalled();

// Removes the keyboard hook and clears the cached physical-key state.
void InputShutdown();

// True while the user physically holds the key. Injected events are ignored.
bool InputPhysicalScanDown(WORD scan, bool extended = false);

// Sends one keyboard event by scan code, so the result does not depend on layout.
bool InputSendScan(WORD scan, bool down, bool extended = false);

// While enabled, physical auto-repeat events for space are swallowed. A held
// activator must not re-press the key between the synthetic jump pulses.
void InputSuppressSpaceRepeats(bool enabled);

#if defined(BLUE_WHALE_QUICKSTOP_TEST) || defined(BLUE_WHALE_BHOP_TEST)
// test seam: pretend a physical key is pressed/released
void InputTestSetPhysicalScan(WORD scan, bool extended, bool down);
#endif
