# TWITS

"Trivial WSPR Information To Sondehub"
------------

A simple C program to extract pico-balloon flight data from wspr.live and send it to the Sondehub database. Also supports custom user-defined Extended Telemetry. It is intended to be run periodically from a cron job. The cron setup passes your flight's callsign and relevant channel information as arguments to the program. For tracking multiple flights simply add multiple instances to the crontab.

The goal was to make the program extremely straightforward and self contained. It is not reliant on any external frameworks or other runtime-environments (but does require curl library). The intended use case is to run on an embedded device such as an OpenWRT based wireless router or old Raspberry Pi.

There is very little along the lines of error checking though, so use at your own risk.

to compile:    `gcc twits.c -o twits.exe -lcurl`
(yes, I know that in linux executables do not need a .exe suffix)

if needed, install libcurl, something like this:  `sudo apt-get update && sudo apt install libcurl4`



Call the program as a cron job, with these arguments: callsign, U4B_type_channel, comment, [ExtendedTelemetry]

example crontab setup: `crontab -e`

`*/5 * * * * /home/USERNAME/TWITS/twits.exe MyCall 582 "Comment Goes Here"`

(if using extended telemetry run it every 3 minutes instead)

In the above example: channel 582 corresponds to the starting minute 2 and id1 and id3 of Q and 9. Please see https://traquito.github.io/channelmap/ to find and reserve an open channel. TWITS assumes you are using the 20 Meter band! There is limited support for 10M however. For instance, if you are on channel 525 of the 10 Meter band, enter channel number 525-10. 

![q](https://github.com/user-attachments/assets/b7c6b9da-4d5e-4699-8208-35be26adce0c)

The pogram automatically creates a separate logfile in the working direcory for each channel being tracked. Logs are deleted automatically after exceeding 4MB.

"High Resolution Position reporting with Extended Telemetry"
------------
You can enable the specific High-Resolution Positioning extended telemetry by adding a '1' as the final command line parameter like this:

`*/3 * * * * /home/USERNAME/TWITS/twits.exe MyCall 582 "Comment Goes Here" 1` 

This will plot the regular position (6 character resolution) in addition to the high resolution (10 character maidenhead) on sondehub as two separate ballons. First one will use the channel number as suffix, 2nd one will append an additional 'e' to the suffic to denote Extended resolution.

TWITS only looks in the first of the 3 available DEXT slots for the extended resolution positions, so if generating the high resolution position on a pico-WSPRer tracker your DEXT config must be "5xx". If generating the high resolution position in a Traquito or U4B tracker the information in the first extended-telemetry spot must have this format:

This will show a smoother more precise position track:
![granularity3](https://github.com/user-attachments/assets/23b63110-da75-4497-87ca-d43b68891098)


For non picoWSPRer trackers, aka traquito, here is the extended telemetry configuration to emulation type 5 DEXT:
```
{ "name": "grid_char7",   "unit": "alpha",   "lowValue":   0,    "highValue": 23,    "stepSize": 1   },
{ "name": "grid_char8",   "unit": "alpha",    "lowValue":   0,    "highValue":    23,    "stepSize":  1 },
{ "name": "grid_char9",   "unit": "digit",      "lowValue":   0,    "highValue":   9,    "stepSize":  1 },
{ "name": "grid_char10",  "unit": "digit",      "lowValue":   0,  "highValue":     9,  "stepSize":  1 },
{ "name": "since_boot",   "unit": "minutes",    "lowValue":   0,  "highValue":     1000,  "stepSize":  10 },
{ "name": "since_gps_lock",  "unit": "minutes",  "lowValue":   0,  "highValue":     1000,  "stepSize":  10 },
```


"Generic (Custom) Extended Telemetry decoding"
------------
Rename the twits_ET_CONFIG_ch_xxx.txt file by replacing xxx with your 2 or 3 digit channel number. If tracking multiple flights you will have one file for each flight. Extended Telemetry values will be logged in the "detail" field on Sondehub.




![et](https://github.com/user-attachments/assets/55ec522e-8b79-493b-a6b4-48251697fda2)


The config file has special characters at the end which must not be removed. The program may temporarily modify the config file to store ET data in between transmissions. Add one line for each item
`name, unit,low-value,high-value,step-size,slot ` 
"slot" corresponds to ET telemetry slot. Only 3,4 or 5 are valid! (slots 1 and 2 must be standard Callsign and Basic-telemetry)

```
#example of slot 3 telemetry config
MinSinceBoot,Count,0,1000,1,3
MinSinceGPSValid,Count,0,1000,1,3
GPSValid,Count,0,1,1,3
SatCount,Count,0,60,1,3
# DO NOT CHANGE ANYTHING BELOW HERE #
<EOF><SL3><SL4><SL5><END>
```


