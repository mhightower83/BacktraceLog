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
#   sudo apt-get install xclip

shopt -s extglob

# Define the dialog exit status codes
: ${DIALOG_OK=0}
: ${DIALOG_CANCEL=1}
: ${DIALOG_HELP=2}
: ${DIALOG_EXTRA=3}
: ${DIALOG_ITEM_HELP=4}
: ${DIALOG_ESC=255}

# set config file
DIALOGRC=$( realpath ~/.dialogrc.dark )
if [[ ! -s "$DIALOGRC" ]]; then
  unset DIALOGRC
fi

: ${ESP8266_BOOTROM_LISTING=~/Arduino/libraries/Backtrace_Log/scripts/boot.txt}
ESP8266_BOOTROM_LISTING=$( realpath $ESP8266_BOOTROM_LISTING )

nameshpath="${0%/*}"
namesh="${0##*/}"
myname="${namesh%.*}"
cmd_args="$@"
ESP_TOOLCHAIN_ADDR2LINE=""

function print_help() {
  cat <<EOF

  Search and display Arduino sketches found in the default Arduino Build tree.
  Select from the list the sketch to run with $myname.
  Pick action from the list $myname, map, exit, or unasm.

EOF

  if [[ "addr2line" == "${1}" ]]; then
    cat <<EOF

  $namesh

  or

  $namesh [alternate search path for .elf files]

  Presents a list of builds to select from. After selection, prompts
  for list of backtrace addresses

  Environment variables and assumed defaults:
    ESP8266_BOOTROM_LISTING=~/Arduino/libraries/Backtrace_Log/scripts/boot.txt
    ESP_ELF_ARCHIVE_PATH (none, unset)

EOF
elif [[ "decoder" == "${1}" ]]; then
    cat <<EOF

  $namesh

  or

  $namesh [alternate search path for .elf files]

  Presents a list of builds to select from. After selection, prompts
  for list of backtrace addresses

  Environment variables and assumed defaults:
    ESP8266_BOOTROM_LISTING=~/Arduino/libraries/Backtrace_Log/scripts/boot.txt
    ESP_ELF_ARCHIVE_PATH (none, unset)

EOF
  elif [[ "idf_monitor" == "${1}" ]]; then
    cat <<EOF
  $namesh

  Environment variables and assumed defaults:
    ESP_USB_PORT="/dev/ttyUSB0"
    ESP_PORT_SPEED="115200"
    ESP_ELF_ARCHIVE_PATH (none, unset)

  or

  $namesh [-p <USB device> | --port <USB device> ] [-s <BPS rate> | --speed <BPS rate> ]

  example:
    $namesh -p /dev/ttyUSB0 -s 230400

EOF
  fi
}

function find_script_paths() {
  # Find all hard linked copies.
  find -L ~/ -samefile ${nameshpath}/${namesh} | sed -e 's:/[^/]*$::' | sort -u | tr "\n" ","
}
function get_file_path() {
  if [[ -z "${1}" ]]; then
    return 1
  fi
  readarray -td, SCRIPT_LINK_PATHS < <( find_script_paths )
  for FOLDER in ${SCRIPT_LINK_PATHS[@]}; do
    FOUND_FILE="${FOLDER}/$1"
    if [[ -f "${FOUND_FILE}" ]]; then
      echo "${FOUND_FILE}"
      return 0
    fi
  done
  return 1
}
# get_file_path "boot.txt"
# exit

function get_key_value_build_json() {
  elfpath="${2%/*}"
  if [[ ! -f "${elfpath}/build.options.json" ]]; then
    clear >&2
    echo "Missing file: \"${elfpath}/build.options.json\"" >&2
    read -n1 anykey
    return
  fi
  readarray -td, key_value < <( jq -r ".${1}" ${elfpath}/build.options.json )
  for value in ${key_value[@]}; do
    echo "${value}"
  done
}

# Find hardware environment and matching tools for .elf file
function get_hardware_tool_path() {
  local hardware_folders fqbn
  elfpath="${2%/*}"
  if [[ ! -f "${elfpath}/build.options.json" ]]; then
    clear >&2
    echo "Missing file: \"${elfpath}/build.options.json\"" >&2
    read -n1 anykey
    return
  fi
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
  ESP_TOOLCHAIN_ADDR2LINE=$( get_hardware_tool_path "addr2line" "${@: -1}" )
  ESP_TOOLCHAIN_PREFIX="${ESP_TOOLCHAIN_ADDR2LINE%addr2line}"
  IDF_MONITOR=$( realpath "${ESP_TOOLCHAIN_ADDR2LINE%/*}/../../idf_monitor.py" )

  if [[ -x "${IDF_MONITOR}" && -f "${IDF_MONITOR}" ]]; then
    :
  else
    clear
    echo -e "\n\nError with identity: ${myname}"
    echo -e "file not executable or missing: $IDF_MONITOR\n"
    read -n1 anykey
    return
  fi

  while [[ "-" = "${1:0:1}" ]]; do
    # reminder "${1,,}" changes to all to lower case for simply match
    case "${1,,}" in
      -p?(=*)) [[ "$1" = "-p" ]] && shift;
        ESP_USB_PORT="${1#-p=}"; shift; ;;
      --usb-port?(=*)) [[ "$1" = "--usb-port" ]] && shift;
        ESP_USB_PORT="${1#--usb-port=}"; shift; ;;
      -s?(=*)) [[ "$1" = "-s" ]] && shift;
        ESP_PORT_SPEED="${1#-s=}"; shift; ;;
      --speed?(=*)) [[ "$1" = "--speed" ]] && shift;
        ESP_PORT_SPEED="${1#--speed=}"; shift; ;;
      # This is wrong! However, people think of it for BPS and will try it.
      --baud?(=*)) [[ "$1" = "--baud" ]] && shift;
        ESP_PORT_SPEED="${1#--baud=}"; shift; ;;
      *) clear
        echo -e "\nUnknown option: '${1} ${2}'\n"
        print_help "idf_monitor"
        read -n1 anykey
        return; ;;
    esac
  done
  : ${ESP_PORT_SPEED="115200"}
  if [[ -z "${ESP_USB_PORT}" ]]; then
    # best guess
    if [[ -c "/dev/ttyUSB0" ]]; then
      ESP_USB_PORT="/dev/ttyUSB0"
    elif [[ ESP_USB_PORT="/dev/ttyUSB1" ]]; then
      ESP_USB_PORT="/dev/ttyUSB1"
    fi
  fi
  if [[ -c "${ESP_USB_PORT}" ]]; then
    :
  elif [[ "/dev/" != "${ESP_USB_PORT:0:5}" ]]; then
    # autocorrect device name, more best guessing
    TRY="/dev/${ESP_USB_PORT}"
    TRY="${TRY//\/\//\/}"
    if [[ -c "${TRY}" ]]; then
      ESP_USB_PORT="${TRY}"
    else
      # we have come this far - try this
      TRY="/dev/tty${ESP_USB_PORT^^}"
      if [[ -c "${TRY}" ]]; then
        ESP_USB_PORT="${TRY}"
      fi
    fi
  fi

  if [[ -z "$1" ]]; then
    print_help "idf_monitor"
    return
  fi

  echo -e "\n$IDF_MONITOR\n  --port $ESP_USB_PORT\n  --baud $ESP_PORT_SPEED"
  echo -e "  --toolchain-prefix $ESP_TOOLCHAIN_PREFIX\n  $1\n"
  # read -n1 anykey

  $IDF_MONITOR \
    --port $ESP_USB_PORT \
    --baud $ESP_PORT_SPEED \
    --toolchain-prefix $ESP_TOOLCHAIN_PREFIX \
    $1
}

# https://stackoverflow.com/a/3352015
function trim() {
    local var="$*"
    # remove leading whitespace characters
    var="${var#"${var%%[![:space:]]*}"}"
    # remove trailing whitespace characters
    var="${var%"${var##*[![:space:]]}"}"
    [[ -n "${var}" ]] && printf ' %s' "$var"
}

function get_flags_from_board_txt() {
  option="${1}"
  boards_txt="${2}"
  qualifier="${3}"
  res="${fqbn#*$option=}"
  res="${res%%,*}"
  if [[ -n "${res}"} ]]; then
    if [[ -n "${qualifier}" ]]; then
      res=$( grep ".${option}.${res}.build." "${boards_txt}" | grep -nm1 "$qualifier" )
    else
      res=$( grep -nm1 ".${option}.${res}.build." "${boards_txt}" )
    fi
    res="${res#*=}"
    trim "$res"
  fi
}

function do_mk_gcc_flags_json() {
  local elf elfpath sketch_ino sketch_folder
  elf="${@: -1}"
  echo "build_json_file='${elf}'"
  sketch_ino=$( get_key_value_build_json "sketchLocation" "${elf}" )
  sketch_folder="${sketch_ino%/*}"
  echo "sketch_ino='${sketch_ino}'"
  elfpath="${elf%/*}"
  if [[ ! -d "${elfpath}/sketch" ]]; then
    clear >&2
    echo "Missing folder: '${elfpath}/sketch'" >&2
    read -n1 anykey
    return
  fi
  local user_path sketch_book fqbn non32xfer mmu
  user_path=$( realpath ~/ )
  sketch_book="${sketch_folder#$user_path/}"
  sketch_book="${user_path}/${sketch_book%%/*}"
  esp8266_platform="./hardware/esp8266com/esp8266"
  platform_path=$( realpath ${sketch_book}/${esp8266_platform} )
  # echo "sketch_book     ='${sketch_book}'"
  # echo "esp8266_platform='${esp8266_platform}'"
  # echo "platform_path   ='${platform_path}'"

  echo -n '  "gccIncludePaths": "'
  tail -n+3 "${elfpath}/sketch/${sketch_ino##*/}.cpp.d" |
    sed 's:/[^/]*$::' |
    sed 's:^[[:space:]]*/:/:' |
    sed 's:/..$::' |
    sort -u |
    sed "s|${sketch_book}|.|" |
    grep -v '/tmp/arduino' |
    tr "\n" "," |
    sed 's:,$::'
  echo '",'

  echo "fqbn:"
  # get_key_value_build_json "fqbn" "${elf}"
  # readarray -td, fqbn < <( jq -r ".fqbn" ${elfpath}/build.options.json )
  fqbn="$( jq -r ".fqbn" ${elfpath}/build.options.json ),end="
  echo "${fqbn%,end=}" | tr "," "\n"
  BUILD_ARCH="${fqbn#*:}"
  BUILD_ARCH="${BUILD_ARCH%:*}"
  BUILD_BOARD_ID="${BUILD_ARCH#*:}"
  BUILD_ARCH="${BUILD_ARCH%:*}"
  BUILD_BOARD=$( grep -nm1 "${BUILD_BOARD_ID}.build.board=" "${platform_path}/boards.txt" )
  BUILD_BOARD="${BUILD_BOARD#*=}"

  [[ -n "${BUILD_ARCH}" ]] && ARDUINO_ARCH="-DARDUINO_ARCH_${BUILD_ARCH^^}"
  [[ -n "${BUILD_BOARD_ID}" ]] && ARDUINO_BOARD_ID="-DARDUINO_ID=\"${BUILD_BOARD_ID}\""
  [[ -n "${BUILD_BOARD}" ]] && ARDUINO_BOARD="-DARDUINO_BOARD_${BUILD_BOARD}"
  [[ -n "${BUILD_BOARD}" ]] && ARDUINO_BOARD_NAME="-DARDUINO_BOARD=\"${BUILD_BOARD}\""

  TOOLS_MENU="$ARDUINO_ARCH $ARDUINO_BOARD_ID $ARDUINO_BOARD $ARDUINO_BOARD_NAME"
  TOOLS_MENU="${TOOLS_MENU}$( get_flags_from_board_txt led "${platform_path}/boards.txt" '' )"
  TOOLS_MENU="${TOOLS_MENU}$( get_flags_from_board_txt float_support "${platform_path}/boards.txt" '' )"
  TOOLS_MENU="${TOOLS_MENU}$( get_flags_from_board_txt CrystalFreq "${platform_path}/boards.txt" 'extra_flags' )"
  TOOLS_MENU="${TOOLS_MENU}$( get_flags_from_board_txt FlashMode "${platform_path}/boards.txt" 'flash_flags' )"
  TOOLS_MENU="${TOOLS_MENU}$( get_flags_from_board_txt dbg "${platform_path}/boards.txt" 'debug_port' )"
  TOOLS_MENU="${TOOLS_MENU}$( get_flags_from_board_txt lvl "${platform_path}/boards.txt" 'debug_level' )"
  TOOLS_MENU="${TOOLS_MENU}$( get_flags_from_board_txt ip "${platform_path}/boards.txt" 'lwip_flags' )"
  TOOLS_MENU="${TOOLS_MENU}$( get_flags_from_board_txt vt "${platform_path}/boards.txt" 'vtable_flags' )"
  TOOLS_MENU="${TOOLS_MENU}$( get_flags_from_board_txt exception "${platform_path}/boards.txt" 'exception_flags' )"
  TOOLS_MENU="${TOOLS_MENU}$( get_flags_from_board_txt stacksmash "${platform_path}/boards.txt" 'stacksmash_flags' )"
  TOOLS_MENU="${TOOLS_MENU}$( get_flags_from_board_txt ssl "${platform_path}/boards.txt" '' )"
  TOOLS_MENU="${TOOLS_MENU}$( get_flags_from_board_txt mmu "${platform_path}/boards.txt" '' )"
  TOOLS_MENU="${TOOLS_MENU}$( get_flags_from_board_txt non32xfer "${platform_path}/boards.txt" '' )"
  VAR=$( get_flags_from_board_txt xtal "${platform_path}/boards.txt" 'f_cpu' )
  TOOLS_MENU="${TOOLS_MENU} -DF_CPU=${VAR:1}"
  VAR=$( get_flags_from_board_txt sdk "${platform_path}/boards.txt" 'sdk' )
  [[ -n "${VAR}" ]] && TOOLS_MENU="${TOOLS_MENU} -D${VAR:1}=1"
  [[ " " == "${TOOLS_MENU:0:1}" ]] && TOOLS_MENU="${TOOLS_MENU:1}"
  echo "TOOLS_MENU='${TOOLS_MENU}'"
  read -n1 anykey
}

function do_viewer_dialog() {
  [[ -z "${menu_viewer_idx}" ]] && menu_viewer_idx=1

  # Duplicate (make a backup copy of) file descriptor 1 on descriptor 3
  exec 3>&1

  # catch the output value
  menu_viewer_idx=$(dialog \
    --no-collapse \
    --clear \
    --extra-label "Copy Results" \
    --extra-button \
    --cancel-label "Exit" \
    --ok-label "View" \
    --column-separator "|-|-|-|" \
    --title "Backtrace Decoded Results" \
    --default-item $menu_viewer_idx \
    --menu "Pick a file to view" 0 0 0 \
    --file $menu_config 2>&1 1>&3)

  rc=$?

  echo "$menu_viewer_idx"

  case $rc in
    $DIALOG_OK)
      # the Yes or OK button.
      # we use this for view/less
      ;;
    $DIALOG_HELP)
      menu_viewer_idx=${menu_viewer_idx#* } ;;
    $DIALOG_EXTRA)
      # We use this for "copy decoded results"
      ;;
    $DIALOG_CANCEL | $DIALOG_ESC)
      # process as cancel/Exit
      return 1 ;;
    * )
      # everything else
      return $rc ;;
  esac

  # recover the associated line in the output of the command
  entry=$(sed -n "${menu_viewer_idx}p" $arduino_elfs_found)

  echo -e "\nYou selected:\n  '$entry'\n"
  LINE_NO=${entry##*:}
  ZXADDR=${entry%%:*}
  ADDR=${ZXADDR#0x}
  if [[ "0x" == "${ZXADDR:0:2}" ]]; then
    FILE_NAME=${entry#*: }
  else
    ZXADDR=""
    ADDR=""
    FILE_NAME=${entry#*) }
  fi

  if [[ "$FILE_NAME" != "${FILE_NAME#* ?? ??:0}" ]]; then
    FILE_NAME=""
    FUNC_NAME=""
  elif [[ "$FILE_NAME" != "${FILE_NAME#* at ??:?}" ]]; then
    FUNC_NAME="${FILE_NAME%% at ??:?*}"
    FUNC_NAME="${FUNC_NAME%%(*}"
    FILE_NAME=""
    LINE_NO=0
  else
    if [[ $FILE_NAME != "${FILE_NAME%% at /*}" ]]; then
      FUNC_NAME="${FILE_NAME%% at /*}"
      FUNC_NAME="${FUNC_NAME%%\(*}"
      FILE_NAME="/${FILE_NAME#* at /}"
      FILE_NAME="${FILE_NAME%:*}"
    elif [[ $FILE_NAME != "${FILE_NAME%% at *}" ]]; then
      FUNC_NAME="${FILE_NAME%% at *}"
      FUNC_NAME="${FUNC_NAME%%\(*}"
      FILE_NAME="${FILE_NAME#* at }"
      FILE_NAME="${FILE_NAME%:*}"
      LINE_NO=0
    else
      FILE_NAME=""
      FUNC_NAME=""
      LINE_NO=0
    fi
    # echo "FUNC_NAME='$FUNC_NAME'"
    # echo "FILE_NAME='$FILE_NAME'"
    # read -n1 anyway
  fi

  # Not sure why some files have no path and some do
  # Need to fix this to use .json file to find core sources.
  if [[ -n "${FILE_NAME}" ]]; then
    if [[ -f "${FILE_NAME}" ]]; then
      FILE_NAME=$( realpath "$FILE_NAME" )
    elif [[ -f "cores/esp8266/${FILE_NAME}" ]]; then
      FILE_NAME=$( realpath "cores/esp8266/$FILE_NAME" )
    fi
    if [[ -f "$FILE_NAME" && 0 -eq $LINE_NO && -n "${FUNC_NAME}" ]]; then
      LINE_NO=$( grep -nom1 "${FUNC_NAME}" $FILE_NAME )
      LINE_NO=${LINE_NO%%:*}
    fi
  fi
  PATTERN=""
  [[ -n "${FUNC_NAME}" ]] && PATTERN="-p${FUNC_NAME}"

  # if [[ $LINE_NO != ?(-)+([0-9]) ]]; then
  #   LINE_NO=0
  # fi

  if [[ 0 -eq $LINE_NO ]]; then
    if [[ "0x4000" == "${ZXADDR:0:6}" ]]; then
      FILE_NAME="${ESP8266_BOOTROM_LISTING}"
      if [[ -n "$FILE_NAME" && -f "$FILE_NAME" ]]; then
        LINE_NO=$( grep -nom1 "${ADDR}" $FILE_NAME )
        LINE_NO=${LINE_NO%%:*}
        PATTERN="-p${ADDR}:"
      else
        FILE_NAME=""
        echo -e "\nBoot ROM listing file, '${ESP8266_BOOTROM_LISTING}', missing\n  See ReadMe.md\n  press any key to continue"
        read -n1 anykey
      fi
    fi
  fi
  if [[ $DIALOG_OK == $rc ]]; then
    if [[ 0 -eq $LINE_NO ]]; then
      FUNC_ADDR=$( grep "${ADDR}:" $backtrace_input )
      FUNC_ADDR="${FUNC_ADDR##*:<0x}"
      FUNC_ADDR="${FUNC_ADDR%%>*}"
      if [[ -n "$FUNC_ADDR" ]]; then
        if [[ -n "$FUNC_NAME" ]]; then
          do_unasm --lite-up "${ADDR}:|${FUNC_ADDR}:|<${FUNC_NAME}>:" "--start-address=0x${FUNC_ADDR}" $1
        else
          do_unasm --lite-up "${ADDR}:|${FUNC_ADDR}:" "--start-address=0x${FUNC_ADDR}" $1
        fi
      elif [[ -n "$FUNC_NAME" ]]; then
        do_unasm --lite-up "<${FUNC_NAME}>:|${ADDR}:" "--disassemble=$FUNC_NAME" $1
      else
        BACK_ADDR=$( printf "%08x" $(( 0x${ADDR} - 256 )) )
        do_unasm --lite-up "${ADDR}:" "--start-address=0x${BACK_ADDR}" $1
      fi
    elif [[ -n "$FILE_NAME" ]]; then
      add2filehistory "$FILE_NAME"
      [[ -n $LINE_NO && 1 -ne $LINE_NO ]] && LINE_NO=$(( $LINE_NO - 1 ))
      less ${PATTERN} +$LINE_NO -N -i "$FILE_NAME"
      lastfile="$FILE_NAME"
    else
      echo "Diagnostic:"
      echo "  LINE_NO='$LINE_NO'"
      echo "  PATTERN='$PATTERN'"
      echo "  FILE_NAME='$FILE_NAME'"
      read -n1 anykey
    fi
  elif [[ $rc == $DIALOG_EXTRA ]]; then
    cat "$addr2line_output" | xclip -selection clipboard
  fi
  return $rc
}

function make_file_menu() {
  cat $1 |
  sed 's/\\/\\\\/g' |
  sed 's/"/\\"/g' >$arduino_elfs_found

  if [[ -s $arduino_elfs_found ]]; then
    #build a dialog configuration file
    cat $arduino_elfs_found |
      awk '{print NR " \"" $0 "\""}' |
      tr "\n" " " >$menu_config
  else
    return 1
  fi
  return 0
}

function do_file_viewer() {
  rc=255
  if make_file_menu $2; then
    while :; do
      do_viewer_dialog $1
      rc=$?
      if [[ $rc == $DIALOG_OK ]]; then     # 0 == OK
        :
      elif [[ $rc == $DIALOG_CANCEL ]]; then   # 1 == Cancel
        clear
        break
      elif [[ $rc == $DIALOG_HELP ]]; then   # 2 = Help
        :
      elif [[ $rc == $DIALOG_EXTRA ]]; then   # 3 == Extra
        :
      else
        clear
        echo "Error: $rc"
        echo "  menu_viewer_idx: '${menu_viewer_idx}'"
        break
      fi
    done
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
    echo "Terminate the list with an <CTRL-D> at the start of a new line."
    echo ""
    # INPUT=$( cat |
    #   sed -n -e 's/[[:space:],:=]/\n/pg' |
    #   grep -E "^0x[a-f0-9]{8}$" |
    #   grep -v "0x00000000" | grep -v "0x3ff" )
    # INPUT=$( cat |
    #   sed -n -e 's/[[:space:],:=]/\n/pg' |
    #   grep -E "^0x[a-f0-9]{8}$" |
    #   grep "0x40" )
      # cat | $backtrace_input
      # INPUT=$( cat $backtrace_input |
      #   sed -n -e 's/[[:space:],:=]/\n/pg' |
      #   grep -E "^0x40[a-f0-9]{6}$" )
      #   # grep -E "^0x40[a-f0-9]{6}$|^<0x40[a-f0-9]{6}>$"
    cat |
      sed -n -e 's/[[:space:],:=]/\n/pg' |
      grep -E "^0x40[a-f0-9]{6}$|^<0x40[a-f0-9]{6}>$" |
      sed ':a; N; $!b a; s/\n</:</g' >$backtrace_input
    INPUT=$( cat $backtrace_input | cut -d: -f1 )
    echo ""

    ${ESP_TOOLCHAIN_ADDR2LINE} -pfiaC -e $1 $INPUT >$addr2line_output
  else
    ${ESP_TOOLCHAIN_ADDR2LINE} -pfiaC -e $* >$addr2line_output
  fi
}

function do_decoder() {
  ESP_TOOLCHAIN_ADDR2LINE=$( get_hardware_tool_path "addr2line" "${@: -1}" )
  ESP_TOOLCHAIN_PREFIX="${ESP_TOOLCHAIN_ADDR2LINE%addr2line}"
  ESP_DECODER=$( realpath "${ESP_TOOLCHAIN_ADDR2LINE%/*}/../../decoder.py" )

  if [[ -x "${ESP_DECODER}" && -f "${ESP_DECODER}" ]]; then
    :
  else
    clear
    echo -e "\n\nError with identity: ${myname}"
    echo -e "file not executable or missing: $ESP_DECODER\n"
    read -n1 anykey
    return
  fi

  if [[ $# -eq 1 ]]; then
    clear
    echo "Paste below the backtrace line."
    echo "Terminate the list with an <CTRL-D> at the start of a new line."
    echo ""
    INPUT=$( cat |
      sed -n -e 's/[[:space:],:=]/\n/pg' |
      grep -E "^0x[a-f0-9]{8}$" |
      grep -v "0x00000000" | grep -v "0x3ff" )
    echo ""

    echo "$INPUT" | \
      ${ESP_DECODER} \
        --elf-path $1 \
        --toolchain-path "${ESP_TOOLCHAIN_ADDR2LINE}" \
        --tool "${ESP_TOOLCHAIN_ADDR2LINE}" >$addr2line_output
  else
    echo "$*" | \
      ${ESP_DECODER} \
        --elf-path $1 \
        --toolchain-path "${ESP_TOOLCHAIN_ADDR2LINE}" \
        --tool "${ESP_TOOLCHAIN_ADDR2LINE}" >$addr2line_output
  fi
}

function do_unasm2() { # WIP
  ADDR=""
  PATTERN=""
  if [[ "${1:0:4}" == "0x40" ]]; then
    ADDR="--start-address=${1}"
    PATTERN="-p${1:2}"
    shift
  elif [[ "${1:0:2}" == "40" ]]; then
    ADDR="--start-address=0x${1}"
    PATTERN="-p${1}"
    shift
  fi

  while [[ "--" = "${1:0:2}" ]]; do
    # reminder "${1,,}" changes to all to lower case for simply match
    case "${1,,}" in
      --lite-up?(=*)) [[ "${1,,}" = "--lite-up" ]] && shift;
      TMP="${1#--lite-up=}"; shift;
      if [[ "-p" == "${PATTERN:0:2}" ]]; then
        PATTERN="${PATTERN}|$TMP"
      else
        PATTERN="-p$TMP"
      fi
      ;;

      -p?(=*)) [[ "$1" = "-p" ]] && shift;
        ESP_USB_PORT="${1#-p=}"; shift; ;;
      --usb-port?(=*)) [[ "$1" = "--usb-port" ]] && shift;
        ESP_USB_PORT="${1#--usb-port=}"; shift; ;;
      -s?(=*)) [[ "$1" = "-s" ]] && shift;
        ESP_PORT_SPEED="${1#-s=}"; shift; ;;
      --speed?(=*)) [[ "$1" = "--speed" ]] && shift;
        ESP_PORT_SPEED="${1#--speed=}"; shift; ;;
      # This is wrong! However, people think of it for BPS and will try it.
      --baud?(=*)) [[ "$1" = "--baud" ]] && shift;
        ESP_PORT_SPEED="${1#--baud=}"; shift; ;;
      *) clear
        echo -e "\nUnknown option: '${1} ${2}'\n"
        print_help "idf_monitor"
        read -n1 anykey
        return; ;;
    esac
  done
  esp_toolchain_objdump=$( get_hardware_tool_path "objdump" "${@: -1}" )
  # echo ${esp_toolchain_objdump} -d ${ADDR} $*
  # read -n1 anyway
  ${esp_toolchain_objdump} -xsD ${ADDR} $* | less -i ${PATTERN}
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
  elif [[ "--lite-up" == "${1}" ]]; then
    ADDR=""
    PATTERN="-p${2}"
    shift 2
  else
    ADDR=""
    PATTERN=""
  fi
  esp_toolchain_objdump=$( get_hardware_tool_path "objdump" "${@: -1}" )
  # echo ${esp_toolchain_objdump} -d ${ADDR} $*
  # read -n1 anyway
  ${esp_toolchain_objdump} -xsD ${ADDR} $* | less -i ${PATTERN}
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
  if [ -n "${2}" ]; then
    find ${1} -xdev -type d -name "${2}" -exec find "{}" -type f -name '*elf' \; 2>/dev/null
  elif [ -n "{1}" ]; then
    find "${1}" -type f -name '*elf' 2>/dev/null
  fi
}

# Show a list of possible Arduino .elf files to pick from
function do_main_dialog() {
  [[ -z "${menu_sketch_idx}" ]] && menu_sketch_idx=1

  OK_CMD=$1
  shift

  # Duplicate (make a backup copy of) file descriptor 1 on descriptor 3
  exec 3>&1

  # catch the output value
  menu_sketch_idx=$(dialog \
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
    --default-item $menu_sketch_idx \
    --menu "Select a build and click an operation" 0 0 0 \
    --file $menu_config 2>&1 1>&3)

  rc=$?

  # Close file descriptor 3
  exec 3>&-

  echo "$menu_sketch_idx"

  case $rc in
    $DIALOG_OK)
      # the Yes or OK button.
      # we use this for view/less
      ;;
    $DIALOG_CANCEL)
      ;;
    $DIALOG_HELP)
      # Repurpose for unasm, skip "HELP " to get to the menu number
      menu_sketch_idx=${menu_sketch_idx#* } ;;
    $DIALOG_EXTRA)
      # We use this to diaplay the .map file
      ;;
    $DIALOG_ESC)
      # process as cancel/Exit
      return 1 ;;
    * )
      # everything else
      return $rc ;;
  esac

  # recover the associated line in the output of the command
  entry=$(sed -n "${menu_sketch_idx}p" $arduino_elfs_found)

  echo -e "\nYou selected:\n  '$entry'\n"
  jumpto=1
  file=$( echo "$entry" | cut -d\' -f2 )
  file=$( realpath "$file" )

  if [[ $rc == $DIALOG_OK ]]; then
    add2filehistory "$file"
    if [[ "addr2line" == "${myname}" ]]; then
      do_addr2line "$file" $*
      do_file_viewer "$file" $addr2line_output
      rc=$?
      [[ "$rc" -eq 1 ]] && rc=0
    elif [[ "decoder" == "${myname}" ]]; then
      do_addr2line "$file" $*
      do_file_viewer "$file" $addr2line_output
      rc=$?
      [[ "$rc" -eq 1 ]] && rc=0
    elif [[ "idf_monitor" == "${myname}" ]]; then
      do_idf_monitor $* "$file"
      # idf_monitor.py trashed the default character set selection
      # these did not work.
      # export LANG=en_US.UTF-8
      # export LC_CTYPE=en_US.utf8
      reset # really slow
    elif [[ "mklintergccflags" == "${myname}" ]]; then
      do_mk_gcc_flags_json $* "$file"
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
  elif [[ $rc == $DIALOG_EXTRA ]]; then
    if [[ -f "${file%.*}.map" ]]; then
      add2filehistory "${file%.*}.map"
      less -i "${file%.*}.map"
      lastfile="${file}"
    else
      # clear
      echo "Missing file: \"${file%.*}.map\""
      read -n1 anykey
    fi
  elif [[ $rc == $DIALOG_HELP ]]; then
    add2filehistory "$file"
    do_unasm $* "$file"
    lastfile="$file"
  fi
  return $rc
}


function make_main_menu() {
  find_arduino_elfs '/tmp' 'arduino_build_*' |
    sed '/^.\/.git/d' |
    sed 's/"/\\"/g' >$arduino_elfs_found
  find_arduino_elfs '/tmp' 'arduino-sketch-*' |
    sed '/^.\/.git/d' |
    sed 's/"/\\"/g' >>$arduino_elfs_found
  if [[ -n "${ESP_ELF_ARCHIVE_PATH}" ]]; then
    find_arduino_elfs "${ESP_ELF_ARCHIVE_PATH}" |
      sed '/^.\/.git/d' |
      sed 's/"/\\"/g' >>$arduino_elfs_found
  fi
  if [[ -n "${1}" ]]; then
    find_arduino_elfs "${1}" |
      sed '/^.\/.git/d' |
      sed 's/"/\\"/g' >>$arduino_elfs_found
  fi
  if [[ -s $arduino_elfs_found ]]; then
    # mv $arduino_elfs_found $temp_io
    sort -u <$arduino_elfs_found >$temp_io
    # build a dialog menu configuration file
    maxwidth=$( sed 's:.*/: :' $temp_io | wc -L | cut -d' ' -f1 )
    if [[ $maxwidth -gt 8 ]]; then
      maxwidth=$( $maxwidth - 8 )
    fi
    export maxwidth
    cat $temp_io |
      xargs -I {} bash -c 'statusfile "$@"' _ {} |
      sort >$arduino_elfs_found
    # cut -c 34-
    cat $arduino_elfs_found |
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

# make some temporary files
arduino_elfs_found=$(mktemp)
addr2line_output=$(mktemp)
menu_config=$(mktemp)
temp_io=$(mktemp)
backtrace_input=$(mktemp)
lastfile=""
menu_sketch_idx=1
menu_viewer_idx=1
maxwidth=20

#make sure the temporary files are removed even in case of interruption
trap "rm $arduino_elfs_found;
      rm $addr2line_output;
      rm $temp_io;
      rm $backtrace_input;
      rm $menu_config;" SIGHUP SIGINT SIGTERM

if [[ "--help" == "${1}" ]]; then
  print_help "${myname}"
  exit 255
elif make_main_menu "${1}"; then
  while :; do
    do_main_dialog "${myname}" "$*"
    rc=$?
    if [[ $rc == $DIALOG_OK ]]; then
      :
    elif [[ $rc == $DIALOG_CANCEL ]]; then
      clear
      break
    elif [[ $rc == $DIALOG_HELP ]]; then
      :
    elif [[ $rc == $DIALOG_EXTRA ]]; then
      :
    elif [[ $rc == $DIALOG_ESC ]]; then
      clear
      break
    else
      clear
      echo "Error: $rc"
      echo "  menu_sketch_idx: '${menu_sketch_idx}'"
      echo "  menu_viewer_idx: '${menu_viewer_idx}'"
      break
    fi
    make_main_menu "${1}"
  done
  echo "$namesh "${cmd_args[@]}
  [[ -n "${ESP_ELF_ARCHIVE_PATH}" ]] && echo "  ESP_ELF_ARCHIVE_PATH='${ESP_ELF_ARCHIVE_PATH}'"
  echo "  ESP_TOOLCHAIN_ADDR2LINE='${ESP_TOOLCHAIN_ADDR2LINE}'"
  printf '  %s\n' "${filehistory[@]}"
else
  echo "Empty search results for:"
  echo "  $namesh "${cmd_args[@]}
fi

# clean up the temporary files
[ -f $arduino_elfs_found ] && rm $arduino_elfs_found
[ -f $addr2line_output ] && rm $addr2line_output
[ -f $menu_config ] && rm $menu_config
[ -f $temp_io ] && rm $temp_io
[ -f $backtrace_input ] && rm $backtrace_input

exit 0

# https://stackoverflow.com/questions/20398499/remove-last-argument-from-argument-list-of-shell-script-bash
