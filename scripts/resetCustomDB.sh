#!/bin/sh

if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
	echo "This script erases all database files created with current references."
	exit
fi

if [ ! -f ./.DBDirectory ]; then
	echo "Cannot determine the database directory. .DBDirectory file is missing. Please run set_targets.sh first to set up the database."
	exit 1
fi

echo "Are you sure to reset the database? (yes/no)"
read decision

if [ $decision = "yes" ] || [ $decision = "y" ] || [ $decision = "Y" ] || [ $decision = "Yes" ] || [ $decision = "YES" ]; then
	echo "Resetting database... "

	for DIR in `cat ./.DBDirectory`
	do
		echo "Cleaning metadata..."
		rm -f $DIR/targets.txt
		rm -Rf $DIR/custom*
		rm -Rf $DIR/*_custom*
		rm -f $DIR/.custom*
		echo -n " Done"

		echo "Resetting the list of references..."
		find $DIR/Custom/ -name '*.f*' > $DIR/.custom
		echo -n " Done"
	done

	echo "Database reset complete."
else
	echo "Aborting database reset."
	exit
fi

