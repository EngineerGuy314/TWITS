# TWITS
"Trivial WSPR Information To Sondehub"

A simple program to extract pico-balloon flight data from wspr.live and send it to the Sondehub database. It is intended to be run periodically from a cron job. The cron setup passes your flight's callsign and relevant channel information as arguments to the program. For tracking multiple flights simply create multiple cron jobs with the appropriate flight info for each**.

My goal was to make the program simple, straightforward and self contained. It is not reliant on 3rd party libraries, external frameworks or other runtime-environments. Specifically intended to run on embedded devices such as a wireless router running OpenWRT.

There is very little along the lines of error checking. Use at your own risk.

to compile:    `gcc twits.c -o twits.exe -lcurl`

if needed, install libcurl, something like this:  `sudo apt-get update && sudo apt install libcurl4-openssl-dev`


Call the program as a cron job, with these arguments: callsign, starting minute, id1, id3, [comment], [details]

example crontab setup: `crontab -e`

`*/5 * * * * /home/USERNAME/TWITS/twits.exe MyCall 2 Q 9 "Comment Goes Here" "Detail goes here"`

In the above example: 2 Q 9 correspond to the starting minute and id1, id3 for channel # 582:

![q](https://github.com/user-attachments/assets/b7c6b9da-4d5e-4699-8208-35be26adce0c)


**
if running multiple instances, offset the call times in crontab so they don't interfere with each other:
`*/5 * * * * /home/user/TWITS/twits.exe CALLSIGN 6 0 3 
1-59/5 * * * * /home/user/TWITS/twits.exe CALLSIGN 8 1 6 `





