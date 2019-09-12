#!/bin/bash

########################################
# Run that and redirect to man/ibnbd.8 #
########################################

VER=$(egrep -oe "[0-9,.]+" -m 1 debian/changelog)
DATE=$(date +"%B %Y")

echo ".TH AM \"8\" \"$DATE\" \"ibnbd $VER\" \"System Administration Utilities\""
echo ".SH NAME"
echo "ibnbd - Configuration tool for IBNBD driver and IBTRS library."
echo ".SH SYNOPSIS"

ibnbd help | grep 'Usage:' | sed 's/Usage: //g'

echo ".SH DESCRIPTION"
ibnbd help

cmds="list show map resize unmap remap disconnect reconnect addpath delpath"

echo ".SH SUBCOMMANDS"
for i in $cmds; do
	echo -e ".B $i\n"
	ibnbd $i help
	echo -e "\n"
done

echo ".SH EXAMPLES"
echo -e "List devices\n"
echo -e ".B ibnbd list\n"
echo -e "List sessions\n"
echo -e ".B ibnbd list sess\n"
echo -e "List paths, display sizes in KB, display all columns\n\n"
echo -e ".B ibnbd list paths KB all\n"
echo -e "List only imported devices, show only mapping_path and devpath, output in json\n"
echo -e ".B ibnbd list devs clt mapping_path,devpath json\n"

echo ".SH COPYRIGHT"
echo "Copyright \(co 2019 IONOS Cloud GmbH. All Rights Reserved"
echo ".SH AUTHOR"
echo "Danil Kipnis <danil.kipnis@cloud.ionos.com>"
