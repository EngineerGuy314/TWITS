#!/bin/bash


# Create a timestamped log file
LOGFILE="/home/rob/log_of runs.txt"

# Function to run a command and log its output with timestamp
run_and_log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] Running: $*" | tee -a "$LOGFILE"
    "$@" >>"$LOGFILE" 2>&1
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] Finished: $*" | tee -a "$LOGFILE"
    echo "-----------------------------" >>"$LOGFILE"
}


run_and_log /home/rob/TWITS/twits.exe KC3LBR 585 "launch: june 23ish 2025 ch 585" "pico-WSPRer w/6 cells+boost" 1
run_and_log /home/rob/TWITS/twits.exe KC3LBR 69 "launched feb22, 2025 - channel 69" "pico-WSPRer tracker triple vertical solar panel"
run_and_log /home/rob/TWITS/twits.exe KC3LBR 226 "launched Oct 6 2024 - channel 226" "pico-WSPRer tracker 7 cell solar"

