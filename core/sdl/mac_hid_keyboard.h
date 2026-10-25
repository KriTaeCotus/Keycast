/*
	This file is part of Flycast.

    Flycast is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Flycast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Flycast.  If not, see <https://www.gnu.org/licenses/>.
*/
// Per-device keyboard input on macOS using IOHIDManager.
// SDL merges all keyboards into a single stream, so this reads each keyboard
// separately so that each one can be assigned to its own maple port.
#pragma once
#if defined(__APPLE__) && !defined(TARGET_IPHONE)
#include <memory>

class GamepadDevice;

namespace machid {

// Starts or stops per-device keyboard input according to config::SeparateKeyboards.
void update();
void term();
// True when HID keyboards are feeding the emulator instead of the SDL keyboard
bool active();

// Keyboard assignment. While capturing, key presses are not sent to the emulator
// and the keyboard on which a key is pressed is reported by pollCapture().
enum class CaptureResult { None, Key, Escape };
void startCapture();
void stopCapture();
CaptureResult pollCapture(std::shared_ptr<GamepadDevice>& keyboard);

}

#endif
