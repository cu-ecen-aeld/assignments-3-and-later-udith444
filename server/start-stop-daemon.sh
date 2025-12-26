#!/bin/sh

DAEMON=/usr/bin/aesdsocket

do_start(){
    start-stop-daemon --start --oknodo --exec "$DAEMON" -- -d
}

do_stop(){
    start-stop-daemon --stop --oknodo --signal TERM --exec "$DAEMON"
    rm -f "/var/tmp/aesdsocketdata" #Remove file where sended data is stored
}

do_status(){
    start-stop-daemon --status --exec "$DAEMON" && exit_status=$? || exit_status=$?
    case $exit_status in
        0)
            echo "aesdsocket is running"
            ;;
        4)
            echo "Unable to determine program aesdsocket running status"
            ;;
        *)
            echo "Program aesdsocket is not running"
            ;;
    esac
}

case "$1" in
    start)
        do_start
        ;;
    stop)
        do_stop
        ;;
    status)
        do_status
        ;;
    *)
        echo "unknown command"
        ;;
esac
