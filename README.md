# DisplayManager for macOS

A macOS menu bar app and `dm` CLI for disabling and restoring connected displays without physically disconnecting them. Requires macOS 11 or later.

## Usage

Click the display icon in the menu bar to access the following controls. The app has no Dock icon or separate main window.

- Active displays: view names, pixel resolutions, and the main display indicator; disable individual displays.
- Displays to restore: restore individual displays or all saved displays.
- Restore All and Quit: restore displays before exiting; keep the app open and show an error if restoration fails.
- Quit (Keep Display State): exit while keeping recovery records.

The last active display cannot be disabled. Opening the menu refreshes the display list to reflect connection changes. If a disabled display's name is unavailable, the app shows the beginning of its UUID. Reconnect a disconnected display before restoring it.

## CLI

```bash
./build/dm
```

Enter a display's menu number to disable it. Enter the `RESTORE ALL` menu number to restore all saved displays, or the `EXIT` menu number to quit. Ctrl-C also exits. The menu bar app and CLI cannot control displays concurrently; quit one before starting the other.

## Recovery Records

The menu bar app and CLI share this per-user file:

```text
~/Library/Application Support/DisplayManager/disabled-displays.txt
```

When the new file does not exist, the first launch imports records from `/tmp/disabled_displays.txt` and preserves the original file. Before disabling a display, the app saves its UUID, display ID, and boot identifier. A record is removed only after restoration succeeds. If the UUID of a disabled display cannot be resolved, restoration can use the saved display ID during the same boot. After a reboot, the app resolves the UUID again rather than reusing the previous display ID. A failed or interrupted disable request may leave a recovery record; use Restore All to clear records after successful restoration. Records persist across reboots, but restoration cannot proceed if the UUID remains unresolvable.

Older records containing only a UUID lack the ID of an already disabled display. Reconnect an external display, or reboot for a built-in display, then try restoring it again. Quit the old app and launch the updated version to apply changes.

Display control uses the original project's private SkyLight API, `CGSConfigureDisplayEnabled`. Its availability or behavior may change with macOS updates. If the API is unavailable, the app shows an error in the control menu.

## Code Structure

- `src/display_manager.cc`, `include/display_manager.h`: display control, recovery records, last-display protection, and exclusive access.
- `src/menu_bar.mm`: AppKit menu bar UI.
- `src/main.cc`: CLI entry point.
- `resources/Info.plist.in`: menu bar app bundle settings (`LSUIElement`).
