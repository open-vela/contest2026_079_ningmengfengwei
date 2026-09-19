#!/bin/sh
/usr/bin/control_center &
/usr/bin/lvgl_xiaozhi &
sleep 5
amixer -c 0 cset numid=50 1
amixer -c 0 cset numid=50 1
amixer -c 0 cset numid=46 1
amixer cset numid=3 60000 60000
/usr/bin/sound_app &
