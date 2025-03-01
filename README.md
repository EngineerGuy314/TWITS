# TWITS
"Trivial WSPR Information To Sondehub"

This is an extremely simple program to extract pico-balloon flight data from wspr.live and send it to the Sondehub database. It is intended to be run periodically from a cron job. The cron setup passes your flight's callsign and relevant channel information as arguments to the program. For tracking multiple flights simply create multiple cron jobs with the appropriate flight info for each.

My goal was to make the program simple and straightforward, and not reliant on external frameworks or libraries. Can easily run for instance on a wireless router running OpenWRT.

There is very little along the lines of error checking. Use at your own risk.

to compile:    `gcc twits.c -o twits.exe -lcurl`

args: callsign, starting minute, id1, id3, [comment], [details]

example crontab setup: `crontab -e`

`*/5 * * * * /home/USERNAME/twits/twits.exe MyCall 2 Q 9 "Comment Goes Here" "Detail goes here"`

2 Q 9 correspond to the starting minute and id1, id3 for channel # 582:

![q](https://github.com/user-attachments/assets/b7c6b9da-4d5e-4699-8208-35be26adce0c)






