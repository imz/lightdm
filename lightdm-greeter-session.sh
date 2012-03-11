#!/bin/sh
# -*- Mode: sh; indent-tabs-mode: nil; tab-width: 4 -*-
#
# Copyright (C) 2011 Canonical Ltd
# Author: Michael Terry <michael.terry@canonical.com>
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation, version 3 of the License.
#
# See http://www.gnu.org/copyleft/gpl.html the full text of the license.

# This wrapper merely ensures that dbus-daemon lives only as long as this
# script does.  Otherwise, it's very easy for dbus-daemon to be autolaunched
# and detached from the greeter.

trap cleanup TERM EXIT

cleanup()
{
    trap - TERM EXIT
    if [ -n "$DBUS_SESSION_BUS_PID" ]; then
        kill "$DBUS_SESSION_BUS_PID"
    fi
    if [ -n "$CMD_PID" ]; then
        kill "$CMD_PID"
    fi
    exit 0
}

eval `dbus-launch --sh-syntax`

exec $@ &
CMD_PID=$!
wait $CMD_PID
CMD_PID=
