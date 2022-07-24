# A backtrace script
A script to run `addr2line` or `idf_monitor.py`.
* It first searches and display Arduino sketches found in the Arduino Build tree.
* Then you select from the list the sketch to run the decoder against.
* Pick the action from the list `addr2line`/`idf_monitor.py`, map, exit, or unasm.
  * `addr2line`
     *  you copy paste the backtrace output into the window and finish with `<CTRL-D>` on a new line.
     * A decoded backtrace report is presented with `dialog`
     * you can select a function/file/line to view from the decoded resutls.
  * `idf_monitor.py`
     * Starts a python based terminal emulator
     * Monitors ESP8266 output for code addresses to interpret with `addr2line`
     * The decoded results are intermixed with the output of the ESP8266

For a typical install, create links to `common_backtrace_script.sh` similar to those below:
```
ln common_backtrace_script.sh ~/bin/addr2line.sh
ln common_backtrace_script.sh ~/bin/idf_monitor.sh
```

Additional application required: `jq`, `xclip`
```
sudo apt-get update
sudo apt-get install jq
sudo apt-get install xclip
```

# Archive of build files
When creating an archive of build directories, be sure to save the `build.options.json` file with the matching `.elf` file. This aids in finding the correct utilities that match your built.