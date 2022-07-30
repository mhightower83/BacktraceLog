# A backtrace script
A script to make life wasier when using `addr2line` or `idf_monitor.py`.
* It first searches and display Arduino sketches found in the Arduino Build tree.
* Then you select from the list the sketch to run the decoder against.
* Pick the action from the list `addr2line`/`idf_monitor.py`, map, exit, or unasm.
  * `addr2line`
     *  you copy paste the backtrace output into the window and finish with `<CTRL-D>` on a new line.
     * A decoded backtrace report is presented with `dialog`
     * you can select a function/file/line to view from the decoded resutls.
     * See [`addr2line.sh`](#addr2linesh) below, for more details.
  * `idf_monitor.py`
     * Starts a python based terminal emulator
     * Monitors ESP8266 output for code addresses to interpret with `addr2line`
     * The decoded results are intermixed with the output of the ESP8266

For a typical install, create links to `common_backtrace_script.sh` similar to those below:
```
ln common_backtrace_script.sh ~/bin/addr2line.sh
ln common_backtrace_script.sh ~/bin/idf_monitor.sh
```
For `addr2line.sh`, add `boot.txt` to your script's directory. This allows `addr2line.sh` to show something for ROM addresses in the backtrace.
```
wget "http://cholla.mmto.org/esp8266/bootrom/boot.txt"
or boot.txt from
https://github.com/trebisky/esp8266/tree/master/reverse/bootrom
https://raw.githubusercontent.com/trebisky/esp8266/master/reverse/bootrom/boot.txt
```
The default directory/path for this file is:
```
~/Arduino/libraries/Backtrace_Log/scripts/boot.txt
```
To use an alternate path, set the environment variable `ESP8266_BOOTROM_LISTING` to the full path of the file. Or edit the script's internal value for `ESP8266_BOOTROM_LISTING`. It is toward the top.

Additional application required: `jq`, `xclip`
```
sudo apt-get update
sudo apt-get install jq
sudo apt-get install xclip
```

# Archive of build files
When creating an archive of build directories, be sure to save the `build.options.json` file with the matching `.elf` file. This aids in finding the correct utilities that match your built.

# `addr2line.sh`
This script automates a lot of frequently steps for debugging.
It accepts a backtrace by way of a copy/paste. After paste, you will need to close the input process with a `<CNTRL-D>`. The text is stripped down to hex values and passed to addr2line for decoding.

The results are display via `dialog` which allows you to select a decoded line for expanded viewing.
* When sources are available, the source file is shown using `less`.
* When the source file name is unknown, the function is disassembled from the `.elf` file.
* If the function name is unknown, the whole `.eft` file is disassembled and presented with `less` positioned at the return address.
* If the address is in the Boot ROM, an annotated Boot ROM listing is used. (you need to download one as part of your install.)

Some general limitations.
* File path is missing for functions that are declared to not return
* `less` pattern match may fail for functions declared inside the class of a dot h file. The CLASSNAME::FUNC reported by the decode is not an exact matchup with the contents of the dot h.
* In general line numbers are used to position in a source file, then function name. For assembly, position at address and include function name, when available, for `less` pattern matching.
* Depending on the crash, the address may be at or after the bad event. When presenting the file with `less` the line is at the top minus 1. You will often need to scroll back to get context of where you are.
