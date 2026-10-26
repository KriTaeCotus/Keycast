# Keycast

Keycast is a personal fork of the [Flycast](https://github.com/flyinghead/flycast) Dreamcast emulator for macOS. It adds one feature: **each physical keyboard can be its own Dreamcast keyboard**, so two people can play *The Typing of the Dead* on one Mac with two keyboards, the way the game works on real hardware.

## Why

On a Dreamcast, each keyboard plugs into its own controller port, and each player types on their own. In stock Flycast on macOS, every keyboard feeds the same input stream: Flycast reads keys through SDL, and SDL on macOS can't tell which keyboard a key came from. Two keyboards act as one, so only one player can play, and one player's Shift affects the other.

## How it works

- **Per-device input.** When **Separate Keyboards** is enabled, Keycast reads keyboards directly through macOS's IOHIDManager instead of SDL. Every keypress arrives tagged with the physical device that sent it.
- **One device per keyboard.** Each keyboard becomes its own Flycast keyboard device, with its own port and its own Shift, Ctrl and Caps Lock state. Flycast's emulated side already supported a keyboard on each of the 4 ports; only the macOS input side was missing.
- **Stable identity.** Each keyboard is identified by its serial number (the Bluetooth address for Bluetooth keyboards), or by its USB port location when it has no serial. This lets the port assignment survive restarts and reconnections, and lets two identical keyboards be told apart.
- **Assignment.** Settings > Controls > **Assign Keyboards** asks you to press a key on Player 1's keyboard, then on Player 2's. Those become ports A and B. Every other keyboard is set to no port, so it still works for the menus and hotkeys but doesn't type in the game. Assignments are saved in `emu.cfg`.
- **Everything else is unchanged.** With the option off (the default), Keycast behaves exactly like Flycast. Mice, controllers, fullscreen and the menus work as usual either way.

The code is in `core/sdl/mac_hid_keyboard.cpp`, with small hooks in `core/sdl/sdl.cpp`, `core/cfg/option.*` and `core/ui/settings_controls.cpp`.

## Playing The Typing of the Dead

1. Settings > Controls: tick **Separate Keyboards**. macOS asks for the **Input Monitoring** permission: allow Keycast (or Flycast) in System Settings > Privacy & Security > Input Monitoring, then restart the app.
2. Click **Assign Keyboards** and press a key on each player's keyboard.
3. Under **Dreamcast Devices**, set Port A and Port B to **Keyboard**.
4. Start the game.

Keycast only reads keys while its window is active. The Caps Lock lights follow macOS, not each player's in-game Caps Lock state.

## Building

Build as for Flycast: run `shell/apple/generate_xcode_project.command` from a path **without spaces**, then press Cmd+R in Xcode. That Debug build is slow; it's only meant for development.

To make a fast, self-contained app that can be copied to another Mac:

```bash
shell/apple/package_keycast.sh
```

This builds the Release version, bundles the Homebrew libraries it needs, signs it locally, and leaves `Keycast.app` and `Keycast.zip` on the Desktop. On the other Mac, the first launch needs **Open Anyway** in System Settings > Privacy & Security, and the Input Monitoring permission needs to be granted once.

## Staying up to date with Flycast

```bash
git fetch upstream
git merge upstream/master
```

where `upstream` is `https://github.com/flyinghead/flycast.git`.

## License

Flycast, and therefore Keycast, is licensed under the GNU GPL v2. See [LICENSE](LICENSE).
