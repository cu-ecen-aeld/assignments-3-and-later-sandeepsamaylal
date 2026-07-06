#!/bin/sh

if [ $# -lt 2 ]
then
	echo "Error: insufficient arguments"
	exit 1
fi

filesdir=$1
searchstr=$2

if [ ! -d "$filesdir" ]
then
	echo "Error: $filesdir is not a directory"
	exit 1
fi

file_count=$(find "$filesdir" -type f | wc -l)
match_count=$(grep -rF -- "$searchstr" "$filesdir" 2>/dev/null | wc -l)

echo "The number of files are ${file_count} and the number of matching lines are ${match_count}"