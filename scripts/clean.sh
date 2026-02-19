#!/bin/sh

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
	echo "Cannot determine the database directory. .DBDirectory file is missing. Please run: arda -d <database_path>"
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
