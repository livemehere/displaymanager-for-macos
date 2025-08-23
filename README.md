# DisplayManager for MacOS

> A macOS command-line tool that allows you to enable or disable connected displays without physically disconnecting them.

## Manual Install

```bash
# Install to /usr/local/bin
cmake -S . -B build -G Ninja -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build --config Release
cmake --install build --config Release

# Run globally (Ctrl-C | 4 to exit)
dm
# == Online Displays ==
# [0] ID=1 bounds=(0,0,1728x1117) [Main]
# [1] ID=2 bounds=(1728,-219,2560x1440)
# [2] ID=3 bounds=(4288,-465,2560x1440)
# [3] RESTORE ALL (from /tmp/disabled_displays.txt)
# [4] EXIT
#  Select index: 
```