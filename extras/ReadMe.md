# A backtrace script
A script to run `addr2line` or `idf_monitor.py`. It first searches and display Arduino sketches found in the Arduino Build tree.
Then you select from the list the sketch to run the decoder against. Pick the action from the list `addr2line`/`idf_monitor.py`, map, exit, or unasm.

For a typical install, create links to `common_backtrace_script.sh` similar to those below:
```
ln common_backtrace_script.sh ~/bin/addr2line.sh
ln common_backtrace_script.sh ~/bin/idf_monitor.sh
```

Additional application required: `jq`
```
sudo apt-get install jq
```
