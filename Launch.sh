#!/bin/bash
# $1 channel

# -h: show help
# -l: list channels
# -o file record into given file
# -v: vlc
# -o file -v: record into file and run vlc
# -d: duration (in minutes)

IFS=$'\n'

function usage
{
   echo "$0 [option] channel_id"
}

function _sleep
{
   t=$1
   pid=$2
   while [ $t -ne 0 ]
   do
      if [ $pid -ne 0 -a -z "`ps x | grep vlc | grep $pid`" ]
      then
         return 0
      fi
      sleep 1
      t=`expr $t - 1`
   done
   return 1
}

BASE_URL="http://www.annatel.tv"
USER=`cat annatel.conf | grep USER | sed 's/USER="\([^"]*\)".*/\1/'`
PASSWD=`cat annatel.conf | grep PASSWD | sed 's/PASSWD="\([^"]*\)".*/\1/'`
USER_AGENT="User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:72.0) Gecko/20100101 Firefox/72.0"

show_channels=0
launch_vlc=0
output_file=""
channel_id=-1
duration=-1 # given in minutes, converted in packs of 12s
vlc_pid=0

while [ "$1" != "" ];
do
   case $1 in
      -l | --list )
         show_channels=1
         ;;
      -v | --vlc )
         launch_vlc=1
         ;;
      -d | --duration )
         shift
         duration=`expr $1 \* 10`
         ;;
      -o | --output )
         shift
         output_file=$1
         ;;
      -h | --help )
         usage
         exit
         ;;
      * )
         [ $channel_id -eq -1 ] && [ $1 -ne 0 -o $1 -eq 0 2>/dev/null ] &&
            { channel_id=$1; }
         [ $channel_id -eq -1 ] && { usage; exit 1; }
   esac
   shift
done

rm -f channels.html
curl -o channels.html "$BASE_URL/api/getchannels?login=$USER&password=$PASSWD" -H $USER_AGENT > /dev/null 2>&1

if [ $show_channels -ne 0 ]
then
   ch_id=1
   # Download channels page
   for ch_line in `cat channels.html | grep '<name>'`
   do
      echo -n "$ch_id: "
      echo $ch_line | sed 's/.*<name>\([^<]\+\)<.*/\1/'
      ch_id=`expr $ch_id + 1`
   done
   rm -f channel.html
   exit
fi

if [ -z $output_file ]
then
   # Launch VLC if no option was given
   launch_vlc=1
fi

# Download channel page
m3u8_line=`cat channels.html | grep m3u8 | head -n $channel_id`
rm -f channels.html

# Main m3u8 url
data_base_url=`echo $m3u8_line | sed 's/.*<url>\(.*isml\).*/\1/'`
data_res_url=`echo $m3u8_line | sed 's/.*<url>.*isml\/\(.*\)/\1/'`

# Download main m3u8
wget -T 30 -t 10 -q -O channel.m3u8 $data_base_url/$data_res_url

# Best resolution
data_res_url=`cat channel.m3u8 | grep -v "#" | head -n 1`

if [ -z $output_file ]
then
   output=annatel.mp4
   rm $output 2> /dev/null
#   mkfifo $output
else
   output=$output_file
   rm $output 2> /dev/null
fi

if [ $launch_vlc -eq 1 ]
then
   vlc $output 2>&1 2> /dev/null &
   vlc_pid=$!
fi


last_ts_url="none"

while [ 1 -eq 1 ]
do
   # Download updated m3u8
   wget -T 30 -t 10 -q -O channel.m3u8 $data_base_url/$data_res_url

   block=0
   if [ ! -z "`grep $last_ts_url channel.m3u8`" ]
   then
     block=1
   fi

   for ts in `cat channel.m3u8 | grep -v "#"`
   do
      if [ $block == 1 ]
      then
        # Block ts download until last downloaded ts is checked
        if [ $ts == $last_ts_url ]
        then
          block=0
        fi
      else
         last_ts_url=$ts
         curl $data_base_url/$ts >> $output 2> /dev/null
         if [ $duration -ne -1 ]
         then
            duration=`expr $duration - 1`
         fi
         if [ $duration -eq 0 ]
         then
            exit
         fi
      fi
   done
   rm -f channel.m3u8
   _sleep 6 $vlc_pid
   if [ $? -eq 0 ]
   then
      exit
   fi
done

