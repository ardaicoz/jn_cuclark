#!/bin/sh
#
#   CLARK, CLAssifier based on Reduced K-mers.
#
#
#   This program is free software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
#   Copyright 2013-2016, Rachid Ounit <rouni001@cs.ucr.edu>
#   clean.sh: To erase database directories/files generated/downloaded.
#
#   Usage:
#     ./clean.sh --all    Remove entire database directory and all config files
#     ./clean.sh --reset  Remove built database files but keep references
#

if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
	echo "Usage: ./clean.sh [--all|--reset]"
	echo "  --all    Remove entire database directory and all config files"
	echo "  --reset  Remove built database files but keep reference files"
	exit
fi

if [ "$1" != "--all" ] && [ "$1" != "--reset" ]; then
	echo "Usage: ./clean.sh [--all|--reset]"
	echo "  --all    Remove entire database directory and all config files"
	echo "  --reset  Remove built database files but keep reference files"
	exit 1
fi

if [ ! -f "./.DBDirectory" ]; then
	echo "Cannot determine the database directory. .DBDirectory file is missing. Please run set_targets.sh first to set up the database."
	exit 1
fi

if [ "$1" = "--all" ]; then
	DIR=`cat ./.DBDirectory`
	echo "Are you sure you want to delete all data in the database directory: $DIR? (yes/no)"
	read decision

	if [ "$decision" = "yes" ] || [ "$decision" = "y" ] || [ "$decision" = "Y" ] || [ "$decision" = "Yes" ] || [ "$decision" = "YES" ]; then
		echo "Cleaning: on-going..."
		rm -Rf $DIR
		rm -f .dbAddress
		rm -f .DBDirectory
		rm -f .settings
		echo "Cleaning: done."
	else
		echo "Cleaning: canceled"
	fi

elif [ "$1" = "--reset" ]; then
	echo "Are you sure you want to reset the database? (yes/no)"
	read decision

	if [ "$decision" = "yes" ] || [ "$decision" = "y" ] || [ "$decision" = "Y" ] || [ "$decision" = "Yes" ] || [ "$decision" = "YES" ]; then
		echo "Resetting database..."

		for DIR in `cat ./.DBDirectory`
		do
			echo "Cleaning metadata..."
			rm -f $DIR/targets.txt
			rm -Rf $DIR/custom*
			rm -Rf $DIR/*_custom*
			rm -f $DIR/.custom*
			echo " Done"

			echo "Resetting the list of references..."
			find $DIR/Custom/ -name '*.f*' > $DIR/.custom
			echo " Done"
		done

		echo "Database reset complete."
	else
		echo "Aborting database reset."
	fi
fi
