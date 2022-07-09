#!/bin/bash
#
#   Copyright 2022 M Hightower
#
#   Licensed under the Apache License, Version 2.0 (the "License");
#   you may not use this file except in compliance with the License.
#   You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
#   Unless required by applicable law or agreed to in writing, software
#   distributed under the License is distributed on an "AS IS" BASIS,
#   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#   See the License for the specific language governing permissions and
#   limitations under the License.
#
# This script from: https://github.com/mhightower83/BacktraceLog/extras/common_backtrace_script.sh
#
#  To install:
#  > ln common_backtrace_script.sh addr2line.sh
#  > ln common_backtrace_script.sh idf_monitor.sh
#
# Additional applications required: jq
#   sudo apt-get install jq

# set config file
DIALOGRC=$( realpath ~/.dialogrc.dark )
if [[ ! -s "$DIALOGRC" ]]; then
  unset DIALOGRC
fi

namesh="${0##*/}"
myname="${namesh%.*}"
cmd_args="$@"

function print_help() {
  cat <<EOF

  Search and display Arduino sketches found in the Arduino Build tree.
  Select from the list the sketch to run with $myname.
  Pick action from the list $myname, map, exit, or unasm.

EOF

  if [[ "addr2line" == "${1}" ]]; then
    cat <<EOF

  $namesh

  Prompted for list of backtrace addresses, when no backtrace-address is provided.

  or

  Use fully specified command line:

  $namesh sketch-name.ino.elf [ backtrace-address1 [backtrace-address1] ...]

EOF
  elif [[ "idf_monitor" == "${1}" ]]; then
    cat <<EOF
  $namesh

EOF
  fi
}

#
# # TODO: Replace broad search with specific using build.options.json from
# # the arduino build tree. This should allow for more precise tool match up
# # with the .elf file. This needs to run for each .elf file selected.
# # build.options.json should be archived with the .elf file.
# function find_current() {
#   # find and use newest xtensa-lx106-elf-addr2line
#   find ~/  -xdev -type f -name $1 2>/dev/null |
#     grep -v '/\.local/' |
#     xargs stat --print="%.19y\t%N\n" |
#     sort -u |
#     tail -1 |
#     cut -d\' -f2
# }
#
#
# if [[ -z "${ESP_TOOLCHAIN_ADDR2LINE}" ]]; then
#   ESP_TOOLCHAIN_ADDR2LINE=$( find_current 'xtensa-lx106-elf-addr2line' )
# fi
# ESP_TOOLCHAIN_PREFIX="${ESP_TOOLCHAIN_ADDR2LINE%/*}/xtensa-lx106-elf-"


# Get hardware environment matching tool for .elf file.
function get_hardware_tool_path() {
  elfpath="${2%/*}"
  readarray -td, hardware_folders < <( jq -r '.hardwareFolders' ${elfpath}/build.options.json )
  readarray -td, fqbn < <( jq -r '.fqbn' ${elfpath}/build.options.json )
  _fqbn="${fqbn[0]%:*}"
  platform="${_fqbn[0]%:*}"
  board_path="${platform//:/\/}"
  bin_path="dontknow"
  if [[ "esp8266com:esp8266" == "${platform}" ]]; then
    bin_path="xtensa-lx106-elf"
  elif [[ "espressif:esp32" == "${platform}" ]]; then
    # esp32 has two chips and addr2line programs and no clear selection string. :(
    if [[ "espressif:esp32:esp32s2" == "${_fqbn:0:23}" ]]; then
      bin_path="xtensa-esp32s2-elf"
    else
      bin_path="xtensa-esp32-elf"
    fi
  fi
  # echo "platform=${platform}"
  # echo "board_path=${board_path}"
  for folder in ${hardware_folders[@]}; do
    tool_path="${folder}/${board_path}/tools/${bin_path}/bin"
    # echo "tool_path=${tool_path}"
    if [[ -d "${tool_path}" ]]; then
      # echo "tool_path=${tool_path}"
      if [[ -f "${tool_path}/${bin_path}-${1}" && -x "${tool_path}/${bin_path}-${1}" ]]; then
        echo "${tool_path}/${bin_path}-${1}"
      fi
    fi
  done
}

function do_idf_monitor() {
  # https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/tools/idf-monitor.html?highlight=idf_monitor
  # /path/to/idf_monitor.py
  #         --port /dev/ttyUSB0
  #         --baud 115200
  #         --toolchain-prefix /path/to/esp8266/tools/xtensa-lx106-elf/bin/xtensa-lx106-elf-
  #         /tmp/new-d1/new.ino.elf
  ESP_TOOLCHAIN_ADDR2LINE=$( get_hardware_tool_path "addr2line" "${1}" )
  ESP_TOOLCHAIN_PREFIX="${ESP_TOOLCHAIN_ADDR2LINE%addr2line}"
  IDF_MONITOR=$( realpath "${ESP_TOOLCHAIN_ADDR2LINE%/*}/../../idf_monitor.py" )
  PORT_SPEED="115200"

  if [[ -x "${IDF_MONITOR}" && -f "${IDF_MONITOR}" ]]; then
    :
  else
    clear
    echo -e "\n\nError with identity: ${myname}"
    echo -e "file not executable or missing: $IDF_MONITOR\n"
    read -n1 anykey
    return
  fi

  if [[ -z $2 ]]; then
    USB_PORT="/dev/ttyUSB0"
  else
    USB_PORT="$1"
    shift
  fi

  $IDF_MONITOR \
    --port $USB_PORT \
    --baud $PORT_SPEED \
    --toolchain-prefix $ESP_TOOLCHAIN_PREFIX \
    $1
}

function do_viewer_dialog() {
  #launch the dialog, get the output in the menu_output file
  [[ -z "${menu_item2}" ]] && menu_item2=1
  dialog \
    --no-collapse \
    --clear \
    --cancel-label "Exit" \
    --ok-label "View" \
    --column-separator "|-|-|-|" \
    --title "Backtrace File Viewer" \
    --default-item $menu_item2 \
    --menu "Pick a file to view" 0 0 0 \
    --file $menu_config 2>$menu_output

  rc=$?

  # recover the output value
  menu_item2=$(<$menu_output)
  echo "$menu_item2"

  if [[ $rc == 0 ]]; then
    # the Yes or OK button.
    # we use this for view/less
    :
  elif [[ $rc == 2 ]]; then
    # --help-button was pressed.
    # Repurpose for edit, skip "HELP " to get to the menu number
    menu_item2=${menu_item2#* }
  elif [[ $rc == 3 ]]; then
    # --extra-button was pressed.
    # We use this for diff
    :
  else
    # export menu_item2
    # Exit/No/Cancel, ESC and everything else
    return $rc
    # return 0  # don't exit
  fi

  # recover the associated line in the output of the command
  # Format "* branch/tdescription"
  entry=$(sed -n "${menu_item2}p" $command_output)

  #replace echo with whatever you want to process the chosen entry
  echo "You selected: $entry"
  jumpto=$( echo "$entry" | sed 's|^.* at /|/|' | cut -d':' -f2 | xargs )
  # file=$( echo "$entry" | cut -d':' -f2 | sed 's|^[^/]*||g' )
  file=$( echo "$entry" | sed 's|^.* at /|/|' | cut -d':' -f1 )
  file=$( realpath "$file" )

  if [[ $rc == 0 ]]; then
    # echo -n "$file" | xclip -selection clipboard
    # add2filehistory "$file"
    # clear
    # less +$jumpto -p"${grep_pattern}" $ignore_case "$file"
    [[ -n $jumpto && 1 -ne $jumpto ]] && jumpto=$(( $jumpto - 1 ))
    less +$jumpto -N -i "$file"
    # read -n1 anykey
    lastfile="$file"
  fi
  return $rc
}

function make_file_menu() {
  #replace ls with what you want
  # search_tree "--no-color" | sed 's/ *$//g' >$command_output
  # sed 's/\t/|-|-|-|/' |
  cat $1 |
  sed 's/\\/\\\\/g' |
  sed 's/"/\\"/g' >$command_output

  if [[ -s $command_output ]]; then
    #build a dialog configuration file
    cat $command_output |
      awk '{print NR " \"" $0 "\""}' |
      tr "\n" " " >$menu_config
  else
    return 1
  fi
  return 0
}

function do_file_viewer() {
  rc=255
  if make_file_menu $1; then
    while :; do
      do_viewer_dialog
      rc=$?
      if [[ $rc == 0 ]]; then     # OK
        :
      elif [[ $rc == 1 ]]; then   # Cancel
        clear
        # echo "Exit"
        # read -n1 anykey
        break
      elif [[ $rc == 2 ]]; then   # Help
        :
      elif [[ $rc == 3 ]]; then   # Extra
        :
      else
        clear
        echo "Error: $rc"
        cat $menu_output
        break
      fi
    done
    # echo "what $rc"
    # read -n1 anykey
    echo "$namesh "${cmd_args[@]}
    echo " grep_pattern: \"${grep_pattern}\""
    printf '  %s\n' "${filehistory[@]}"
  fi
  return $rc
}

function do_addr2line() {
  ESP_TOOLCHAIN_ADDR2LINE=$( get_hardware_tool_path "addr2line" "${1}" )
  if [[ $# -eq 1 ]]; then
    clear
    echo "Paste below the backtrace line."
    echo "Terminate the list with an <CTRL-D> on an empty line."
    echo ""
    INPUT=$( cat |
      sed -n -e 's/[[:space:],:=]/\n/pg' |
      grep -E "^0x[a-f0-9]{8}$" |
      grep -v "0x00000000" | grep -v "0x3ff" )
    echo ""

    ${ESP_TOOLCHAIN_ADDR2LINE} -pfiaC -e $1 $INPUT >$file_command_output
    # cat $file_command_output
    # echo -e "\nPress any key to contine"
    # read -n1 anykey
  else
    ${ESP_TOOLCHAIN_ADDR2LINE} -pfiaC -e $* >$file_command_output
    # cat $file_command_output
  fi
}

function do_unasm() {
  if [[ "${1:0:4}" == "0x40" ]]; then
    ADDR="--start-address=${1}"
    PATTERN="-p${1:2}"
    shift
  elif [[ "${1:0:2}" == "40" ]]; then
    ADDR="--start-address=0x${1}"
    PATTERN="-p${1}"
    shift
  else
    ADDR=""
    PATTERN=""
  fi
  esp_toolchain_objdump=$( get_hardware_tool_path "objdump" "${@: -1}" )
  ${esp_toolchain_objdump} -d ${ADDR} $* | less -i ${PATTERN}
}

declare -a filehistory
function add2filehistory() {
  filehistory[${#filehistory[@]}]="$1"
}


function statusfile() {
  short_name=$( echo -n "$1" | sed 's:.*/::' | sed 's:[.]ino[.]elf::')
  printf "%${maxwidth}s  " "${short_name}"
  stat --print="%.19y %N\n" "${1}"
}
export -f statusfile


function find_arduino_elfs() {
  # find -L . -xdev -type f ${1} "${2}" 2>/dev/null
  if [ -n "${1}" ]; then
    find /tmp  -xdev -type d -name "${1}" -exec find "{}" -type f -name '*elf' \; 2>/dev/null
  fi
}


function do_main_dialog() {
  #launch the dialog, get the output in the menu_output file

  OK_CMD=$1
  shift

  # Duplicate (make a backup copy of) file descriptor 1
  # on descriptor 3
  exec 3>&1

  # catch the output value
  menu_item=$(dialog \
    --no-collapse \
    --clear \
    --extra-label "map" \
    --extra-button \
    --help-label "unasm" \
    --help-button \
    --cancel-label "Exit" \
    --ok-label $OK_CMD \
    --column-separator "\t" \
    --title "Arduino ESP8266 Sketch Builds" \
    --default-item $menu_item \
    --menu "Select a build and click an operation" 0 0 0 \
    --file $menu_config 2>&1 1>&3)

  rc=$?

  # Close file descriptor 3
  exec 3>&-

  # recover the output value
  # menu_item=$(<$menu_output)
  echo "$menu_item"

  if [[ $rc == 0 ]]; then
    # the Yes or OK button.
    # we use this to run the our main identity
    :
  elif [[ $rc == 2 ]]; then
    # --help-button was pressed.
    # Repurpose for unasm, skip "HELP " to get to the menu number
    menu_item=${menu_item#* }
  elif [[ $rc == 3 ]]; then
    # --extra-button was pressed.
    # We use this to diaplay the .map file
    # select_action
    :
  else
    # Exit/No/Cancel (1), ESC (255) and everything else
    return $rc
  fi

  # recover the associated line in the output of the command
  # Format "* branch/tdescription"
  entry=$(sed -n "${menu_item}p" $command_output)

  #replace echo with whatever you want to process the chosen entry
  echo "You selected: $entry"
  jumpto=1
  file=$( echo "$entry" | cut -d\' -f2 )
  file=$( realpath "$file" )

  if [[ $rc == 0 ]]; then
    # echo -n "$file" | xclip -selection clipboard
    add2filehistory "$file"
    if [[ "addr2line" == "${myname}" ]]; then
      do_addr2line "$file" $*
      do_file_viewer $file_command_output
      rc=$?
      [[ "$rc" -eq 1 ]] && rc=0
    elif [[ "idf_monitor" == "${myname}" ]]; then
      do_idf_monitor $* "$file"
      # idf_monitor.py trashed the default character set selection
      # these did not work.
      # export LANG=en_US.UTF-8
      # export LC_CTYPE=en_US.utf8
      reset # really slow
    else
      clear
      echo -e "\n\nUnkown identity: ${myname}\n"

      echo -en "addr2line path:\n  "
      get_hardware_tool_path "addr2line" "$file"

      echo -e "\nTo finish install:"
      echo "  ln common_backtrace_script.sh addr2line.sh"
      echo "  ln common_backtrace_script.sh idf_monitor.sh"

      read -n1 anykey
      return 255
    fi
    lastfile="$file"
    # echo "less +$jumpto -p\"${grep_pattern}\" $ignore_case \"$file\""
  elif [[ $rc == 3 ]]; then
    add2filehistory "${file%.*}.map"
    less -i "${file%.*}.map"
    lastfile="${file}"
  elif [[ $rc == 2 ]]; then
    # echo -n "$file" | xclip -selection clipboard
    add2filehistory "$file"
    do_unasm $* "$file"
    lastfile="$file"
  fi
  return $rc
}


function make_main_menu() {
  find_arduino_elfs 'arduino_build_*' |
    sed '/^.\/.git/d' |
    sed 's/"/\\"/g' >$command_output
  if [[ -s $command_output ]]; then
    mv $command_output $temp_io
    # build a dialog menu configuration file
    maxwidth=$( sed 's:.*/: :' $temp_io | wc -L | cut -d' ' -f1 )
    if [[ $maxwidth -gt 8 ]]; then
      maxwidth=$( $maxwidth - 8 )
    fi
    export maxwidth
    cat $temp_io |
      xargs -I {} bash -c 'statusfile "$@"' _ {} |
      sort >$command_output
    # cut -c 34-
    cat $command_output |
      awk '{print NR " \"" $0 "\""}' |
      tr "\n" " " >$menu_config
  else
    return 1
  fi
  return 0
}

################################################################################
# main -
#

# From https://unix.stackexchange.com/a/70868

#make some temporary files
command_output=$(mktemp)
file_command_output=$(mktemp)
menu_config=$(mktemp)
menu_output=$(mktemp)
temp_io=$(mktemp)
lastfile=""
lastaction=255
menu_item=1
maxwidth=20

#make sure the temporary files are removed even in case of interruption
trap "rm $command_output;
      rm $file_command_output;
      rm $menu_output;
      rm $temp_io;
      rm $menu_config;" SIGHUP SIGINT SIGTERM


# if make_main_menu "${cmd_args[@]}"; then
if [[ "--help" == "${1}" ]]; then
  print_help "${myname}"
  exit 255
elif make_main_menu; then
  while :; do
    do_main_dialog "${myname}"
    rc=$?
    if [[ $rc == 0 ]]; then     # OK
      :
    elif [[ $rc == 1 ]]; then   # Cancel
      clear
      break
    elif [[ $rc == 2 ]]; then   # Help
      :
    elif [[ $rc == 3 ]]; then   # Extra
      :
    elif [[ $rc == 255 ]]; then   # ESC
      clear
      break
    else
      clear
      echo "Error: $rc"
      cat $menu_output
      break
    fi
    make_main_menu "${cmd_args[@]}"
  done
  echo "$namesh "${cmd_args[@]}
  echo "  ESP_TOOLCHAIN_ADDR2LINE='${ESP_TOOLCHAIN_ADDR2LINE}'"
  printf '  %s\n' "${filehistory[@]}"
else
  echo "Empty search results for:"
  echo "  $namesh "${cmd_args[@]}
fi

# do_idf_monitor $( find_arduino_elfs 'arduino_build_*' )

#clean the temporary files
[ -f $command_output ] && rm $command_output
[ -f $file_command_output ] && rm $file_command_output
[ -f $menu_output ] && rm $menu_output
[ -f $menu_config ] && rm $menu_config
[ -f $temp_io ] && rm $temp_io


# https://stackoverflow.com/questions/20398499/remove-last-argument-from-argument-list-of-shell-script-bash
