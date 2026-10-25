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
#include "types.h"
#if defined(__APPLE__) && !defined(TARGET_IPHONE)
#include "mac_hid_keyboard.h"
#include "cfg/option.h"
#include "input/keyboard_device.h"
#include "log/Log.h"
#include <SDL.h>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <IOKit/hid/IOHIDUsageTables.h>
#include <IOKit/hidsystem/IOHIDLib.h>
#include <algorithm>
#include <bitset>
#include <cctype>
#include <map>
#include <memory>
#include <string>

namespace machid {

struct DeviceInfo
{
	std::string product;
	std::string transport;
	std::string serial;
	std::string physicalId;
	long vendorId = 0;
	long productId = 0;
	long locationId = 0;
	// Identifies the physical keyboard across reconnections and restarts
	std::string uniqueId;
	// Short suffix shown in the UI to tell identical keyboards apart
	std::string shortId;
};

class MacHidKeyboard : public KeyboardDevice
{
public:
	MacHidKeyboard(int maple_port, const DeviceInfo& info)
		: KeyboardDevice(maple_port, "HID")
	{
		displayName = info.product;
		if (!info.shortId.empty())
			displayName += " (" + info.shortId + ")";
		_name = displayName;
		_unique_id = info.uniqueId;
		loadMapping();
	}

	void keyInput(u8 keycode, bool pressed)
	{
		if (pressedKeys[keycode] == pressed)
			return;
		pressedKeys[keycode] = pressed;
		if (settings.input.keyboardLangId != KeyboardLayout::US && keycode == 0x31)	// US: backslash and pipe
			KeyboardDevice::input(0x32, pressed, 0);	// non-US: hash and tilde
		else
			KeyboardDevice::input(keycode, pressed, 0);
	}

	bool isPressed(u8 keycode) const {
		return pressedKeys[keycode];
	}

	void releaseAll()
	{
		for (u32 keycode = 0; keycode < pressedKeys.size(); keycode++)
			if (pressedKeys[keycode])
				keyInput(keycode, false);
	}

protected:
	void refreshName() override {
		_name = displayName;
	}

private:
	std::string displayName;
	std::bitset<256> pressedKeys;
};

static IOHIDManagerRef manager;
static bool permissionDenied;
// Some keyboards expose several HID interfaces. They are merged into one
// keyboard if they share the same unique id.
static std::map<IOHIDDeviceRef, std::shared_ptr<MacHidKeyboard>> devices;
static std::map<std::string, std::shared_ptr<MacHidKeyboard>> keyboards;
static bool capturing;
static CaptureResult captureResult;
static std::shared_ptr<MacHidKeyboard> capturedKeyboard;

static std::string getStringProperty(IOHIDDeviceRef device, CFStringRef key)
{
	CFTypeRef ref = IOHIDDeviceGetProperty(device, key);
	if (ref == nullptr || CFGetTypeID(ref) != CFStringGetTypeID())
		return {};
	char buf[256];
	if (!CFStringGetCString((CFStringRef)ref, buf, sizeof(buf), kCFStringEncodingUTF8))
		return {};
	return buf;
}

static long getIntProperty(IOHIDDeviceRef device, CFStringRef key)
{
	CFTypeRef ref = IOHIDDeviceGetProperty(device, key);
	long v = 0;
	if (ref != nullptr && CFGetTypeID(ref) == CFNumberGetTypeID())
		CFNumberGetValue((CFNumberRef)ref, kCFNumberLongType, &v);
	return v;
}

static DeviceInfo getDeviceInfo(IOHIDDeviceRef device)
{
	DeviceInfo info;
	info.product = getStringProperty(device, CFSTR(kIOHIDProductKey));
	info.transport = getStringProperty(device, CFSTR(kIOHIDTransportKey));
	info.serial = getStringProperty(device, CFSTR(kIOHIDSerialNumberKey));
	info.physicalId = getStringProperty(device, CFSTR("PhysicalDeviceUniqueID"));
	info.vendorId = getIntProperty(device, CFSTR(kIOHIDVendorIDKey));
	info.productId = getIntProperty(device, CFSTR(kIOHIDProductIDKey));
	info.locationId = getIntProperty(device, CFSTR(kIOHIDLocationIDKey));
	if (info.product.empty())
		info.product = "Keyboard";

	// Vendor and product ids are the same for two keyboards of the same model,
	// so use the first available property that tells them apart.
	std::string key;
	if (!info.serial.empty())
		key = info.serial;
	else if (!info.physicalId.empty())
		key = info.physicalId;
	else if (info.locationId != 0)
		key = std::to_string(info.locationId);
	for (char& c : key)
		c = std::isalnum((unsigned char)c) ? std::tolower((unsigned char)c) : '_';

	char ids[32];
	snprintf(ids, sizeof(ids), "%04lx_%04lx", info.vendorId, info.productId);
	info.uniqueId = std::string("hid_keyboard_") + ids;
	if (!key.empty())
		info.uniqueId += "_" + key;
	info.shortId = key.size() > 5 ? key.substr(key.size() - 5) : key;

	return info;
}

static void deviceAdded(void *context, IOReturn result, void *sender, IOHIDDeviceRef device)
{
	DeviceInfo info = getDeviceInfo(device);
	NOTICE_LOG(INPUT, "HID keyboard added: \"%s\" vendor %04lx product %04lx transport \"%s\" serial \"%s\" physical id \"%s\" location %08lx -> %s",
			info.product.c_str(), info.vendorId, info.productId, info.transport.c_str(), info.serial.c_str(),
			info.physicalId.c_str(), info.locationId, info.uniqueId.c_str());
	std::shared_ptr<MacHidKeyboard>& keyboard = keyboards[info.uniqueId];
	if (keyboard == nullptr)
	{
		// Default port in case none is saved for this keyboard
		int port = std::min<int>(keyboards.size() - 1, 3);
		keyboard = std::make_shared<MacHidKeyboard>(port, info);
		GamepadDevice::Register(keyboard);
	}
	devices[device] = keyboard;
}

static void deviceRemoved(void *context, IOReturn result, void *sender, IOHIDDeviceRef device)
{
	auto it = devices.find(device);
	if (it == devices.end())
		return;
	std::shared_ptr<MacHidKeyboard> keyboard = it->second;
	devices.erase(it);
	NOTICE_LOG(INPUT, "HID keyboard removed: %s", keyboard->unique_id().c_str());
	for (const auto& [dev, kb] : devices)
		if (kb == keyboard)
			// another interface of this keyboard is still connected
			return;
	keyboard->releaseAll();
	GamepadDevice::Unregister(keyboard);
	keyboards.erase(keyboard->unique_id());
}

static void inputValue(void *context, IOReturn result, void *sender, IOHIDValueRef value)
{
	IOHIDElementRef element = IOHIDValueGetElement(value);
	if (IOHIDElementGetUsagePage(element) != kHIDPage_KeyboardOrKeypad)
		return;
	u32 usage = IOHIDElementGetUsage(element);
	// 0-3 are error codes, 0xE0-0xE7 are modifiers. Ignore anything above.
	if (usage < kHIDUsage_KeyboardA || usage > kHIDUsage_KeyboardRightGUI)
		return;
	bool pressed = IOHIDValueGetIntegerValue(value) != 0;
	auto it = devices.find(IOHIDElementGetDevice(element));
	if (it == devices.end())
		return;
	MacHidKeyboard& keyboard = *it->second;
	// HID input is received even when another app is active, so ignore
	// key presses unless the emulator window has the keyboard focus.
	// Releases are always accepted so that no key stays stuck down.
	if (pressed && SDL_GetKeyboardFocus() == nullptr)
		return;
	if (capturing && pressed)
	{
		// Swallowed. The matching release is ignored since the key isn't marked as pressed.
		captureResult = usage == kHIDUsage_KeyboardEscape ? CaptureResult::Escape : CaptureResult::Key;
		capturedKeyboard = it->second;
		return;
	}
	DEBUG_LOG(INPUT, "HID key %02x %s on %s", usage, pressed ? "down" : "up", keyboard.unique_id().c_str());
	keyboard.keyInput(usage, pressed);
}

static bool checkPermission()
{
	IOHIDAccessType access = IOHIDCheckAccess(kIOHIDRequestTypeListenEvent);
	if (access == kIOHIDAccessTypeGranted)
		return true;
	// Shows the system prompt the first time. The user must then allow
	// the app in System Settings > Privacy & Security > Input Monitoring.
	if (access == kIOHIDAccessTypeUnknown && IOHIDRequestAccess(kIOHIDRequestTypeListenEvent))
		return true;
	WARN_LOG(INPUT, "Separate keyboards: Input Monitoring permission not granted");
	return false;
}

static void stop()
{
	if (manager == nullptr)
		return;
	for (auto& [id, keyboard] : keyboards)
	{
		keyboard->releaseAll();
		GamepadDevice::Unregister(keyboard);
	}
	keyboards.clear();
	devices.clear();
	stopCapture();
	IOHIDManagerUnscheduleFromRunLoop(manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
	IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
	CFRelease(manager);
	manager = nullptr;
	NOTICE_LOG(INPUT, "Separate keyboards: stopped");
}

static void start()
{
	if (!checkPermission())
	{
		permissionDenied = true;
		return;
	}
	manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);

	int page = kHIDPage_GenericDesktop;
	int usage = kHIDUsage_GD_Keyboard;
	CFNumberRef pageRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &page);
	CFNumberRef usageRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &usage);
	const void *keys[] { CFSTR(kIOHIDDeviceUsagePageKey), CFSTR(kIOHIDDeviceUsageKey) };
	const void *values[] { pageRef, usageRef };
	CFDictionaryRef matching = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 2,
			&kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	IOHIDManagerSetDeviceMatching(manager, matching);
	CFRelease(matching);
	CFRelease(usageRef);
	CFRelease(pageRef);

	IOHIDManagerRegisterDeviceMatchingCallback(manager, deviceAdded, nullptr);
	IOHIDManagerRegisterDeviceRemovalCallback(manager, deviceRemoved, nullptr);
	IOHIDManagerRegisterInputValueCallback(manager, inputValue, nullptr);
	// Callbacks are called on this thread when SDL pumps events
	IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
	IOReturn rc = IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);
	if (rc != kIOReturnSuccess)
	{
		WARN_LOG(INPUT, "Separate keyboards: IOHIDManagerOpen failed: %x", rc);
		stop();
		permissionDenied = true;
		return;
	}
	NOTICE_LOG(INPUT, "Separate keyboards: started");
}

void update()
{
	if (config::SeparateKeyboards)
	{
		if (manager == nullptr && !permissionDenied)
			start();
	}
	else
	{
		// Try again next time the option is enabled
		permissionDenied = false;
		stop();
	}
}

void term()
{
	stop();
}

bool active()
{
	return manager != nullptr;
}

void startCapture()
{
	capturing = true;
	captureResult = CaptureResult::None;
	capturedKeyboard = nullptr;
}

void stopCapture()
{
	capturing = false;
	capturedKeyboard = nullptr;
}

CaptureResult pollCapture(std::shared_ptr<GamepadDevice>& keyboard)
{
	CaptureResult result = captureResult;
	captureResult = CaptureResult::None;
	keyboard = capturedKeyboard;
	capturedKeyboard = nullptr;
	return result;
}

}
#endif
