#!/bin/bash
gcc twits.c -o twits.exe -lcurl


# Check if compilation was successful
if [ $? -eq 0 ]; then
    echo "Compilation successful. Running program..."
    ./twits.exe KC3IBR 8 1 6 "comms 2 here" "deets 2 here"
else
    echo "Compilation failed."
fi
