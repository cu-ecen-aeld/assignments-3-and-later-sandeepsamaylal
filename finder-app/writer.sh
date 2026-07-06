#!/bin/sh

if [ $# -lt 2 ]
then
	echo "Error: insufficient arguments"
	exit 1
fi

writefile=$1
writestr=$2

writefiledir=$(dirname "$writefile")

if ! mkdir -p "$writefiledir"
then
	echo "Error: could not create directory path for $writefile"
	exit 1
fi

if ! printf '%s\n' "$writestr" > "$writefile"
then
	echo "Error: could not create file $writefile"
	exit 1
fi