# TWITS
"Trivial WSPR Information To Sondehub"

This is an extremely simple program to extract pico-balloon flight data from wspr.live and send it to the Sondehub database. It is intended to be run periodically from a cron job. The cron setup passes your flight's callsign and relevant channel information as arguments to the program. For tracking multiple flights simply create multiple cron jobs with the appropriate flight info for each.

My goal was to make the program simple and straightforward, and not reliant on external frameworks or libraries.

There is very little along the lines of error checking. Use at your own risk.
