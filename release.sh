# SPDX-License-Identifier: GPL-2.0-or-later
#!/bin/bash
set -ex

version="$1"

if [ -z "$version" ]; then
	echo "Please provide the version to be released";
	exit
else
	echo "Releasing version $version"
fi

# Change version in the Makefile to the new $version
sed -i "s/^VERSION.*/VERSION := $version/" Makefile

# Update the man page
rm rnbd.8.md
rm man/rnbd.8
make man

# Commit the changes
git commit -asm "rnbd: release $version"

# Tag the version of the source code package
git tag $version

# Push
git log -2
echo "Now please do \$git push --tags"
