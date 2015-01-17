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

BASE_URL="http://client.annatel.tv"
USER=`cat annatel.conf | grep USER | sed 's/USER="\([^"]*\)".*/\1/'`
PASSWD=`cat annatel.conf | grep PASSWD | sed 's/PASSWD="\([^"]*\)".*/\1/'`

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
         duration=`expr $1 \* 5`
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

if [ $show_channels -ne 0 ]
then
   # Download channels page
   wget -T 30 -t 10 -q -O channels.html --post-data='login='$USER'&password='$PASSWD'' $BASE_URL/auth/dologin
   for ch_line in `cat channels.html | grep category | grep title`
   do
      echo $ch_line | sed 's/.*a href=".channel.\([0-9]\+\).*title="\([^"]\+\)".*/\1: \2/'
   done
   rm channels.html
   exit
fi

if [ -z $output_file ]
then
   # Launch VLC if no option was given
   launch_vlc=1
fi

# Download channel page
wget -T 30 -t 10 -q -O channel.html --post-data='login='$USER'&password='$PASSWD'&auth_url_return=/channel/'$channel_id'.html' $BASE_URL/auth/dologin

# Main m3u8 url
data_base_url=`cat channel.html | grep "src:" | sed 's/.*src: "\(.*isml\).*"/\1/'`
data_res_url=`cat channel.html | grep "src:" | sed 's/.*src: ".*isml\/\(.*\)"/\1/'`
rm channel.html
# Download main m3u8
wget -T 30 -t 10 -q -O channel.m3u8 $data_base_url/$data_res_url
# Best resolution
data_res_url=`cat channel.m3u8 | tail -n 1`

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


max_ts_id=0

while [ 1 -eq 1 ]
do
   # Download updated m3u8
   wget -T 30 -t 10 -q -O channel.m3u8 $data_base_url/$data_res_url
   for ts in `cat channel.m3u8 | grep -v "#"`
   do
      ts_id=`echo $ts | sed 's/.*-\([0-9]\+\).ts.*/\1/'`
      if [ $ts_id -gt $max_ts_id ]
      then
         max_ts_id=$ts_id
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
   rm channel.m3u8
   _sleep 12 $vlc_pid
   if [ $? -eq 0 ]
   then
      exit
   fi
done

