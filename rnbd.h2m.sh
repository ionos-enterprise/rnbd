#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later

########################################
# Run that and redirect to man/rnbd.8 #
########################################

PATH=$PATH:./
VER=$(egrep -oe "[0-9,.]+" -m 1 debian/changelog)
DATE=$(LANG=en_us_8859_1 && date +"%B %Y")

echo ".TH RNBD \"8\" \"$DATE\" \"rnbd $VER\" \"System Administration Utilities\""
echo ".SH NAME"
echo "rnbd - Configuration tool for RNBD driver and RTRS library."
echo ".SH SYNOPSIS"

rnbd help | grep 'Usage:' | sed 's/Usage: //g'

echo ".SH SYNOPSIS"
rnbd help all | grep -v "client|server|both|help" | grep -v -e "--help|--verbose|--debug|--simulate" | sed 's/Usage: //g'

echo -e "\n"
echo ".SH DESCRIPTION"
echo "The commands of the tool are structured in the following fashion:"
echo "client vs server -> device vs. session vs. path. If the context of"
echo "a command is unambiguous, it can be also called directly."
echo "For example: rnbd map (instead of rnbd client device map), rnbd session list"
echo "(instead of rnbd client session list), rnbd show client@server (instead of rnbd client session show client@server), etc."

modes="client server"
objects="device session path"
cmds="list show map resize unmap remap close disconnect reconnect add delete readd recover"

echo ".SH SUBCOMMANDS"

for m in $modes; do
	for o in $objects; do
		for c in $cmds; do
			if rnbd $m $o $c help 2>1 > /dev/null; then
				echo -e ".B\n"
				rnbd $m $o $c help all| sed 's/[[:space:]]*$//' | sed 's/Usage: //'
				echo -e "\n"
			fi
		done
	done
done

echo ".SH EXAMPLES"
echo -e "List server devices\n"
echo -e ".B rnbd server devices list\n"
echo -e "List client sessions\n"
echo -e ".B rnbd client sessions list \n"
echo -e "List paths of server, display sizes in KB, display all columns\n\n"
echo -e ".B rnbd server paths list K all\n"
echo -e "List imported on client devices, show only mapping_path and devpath, output in json\n"
echo -e ".B rnbd client devices list mapping_path,devpath json\n"

echo ".SH COPYRIGHT"
echo "Copyright \(co 2019 - 2020 IONOS Cloud GmbH. All Rights Reserved"
echo ".SH AUTHORS"
echo "Danil Kipnis <danil.kipnis@cloud.ionos.com>"
echo ".RE"
echo "Lutz Pogrell <lutz.pogrell@cloud.ionos.com>"
