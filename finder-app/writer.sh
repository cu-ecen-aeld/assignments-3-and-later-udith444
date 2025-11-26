#!/bin/bash

# Accepts the following arguments: 
# the first argument is a full path to a file (writefile); 
# the second argument is a text string which will be written within this file (writestr)

# Exits with value 1 error and print statements if any of the arguments above were not specified

if [ $# -eq 0 ]; then 
    echo "Directory path and text string not specified."
    exit 1
elif [ $# -eq 1 ]; then
    echo "Text string not specified."
    exit 1
elif [ $# -gt 2 ]; then
    echo "Extra parameters specified."
    exit 1
fi

writefile=$1
dir=$(dirname ${writefile})
writestr=$2

# Creates a new file with name and path writefile with content writestr, 
# overwriting any existing file and creating the path if it doesn’t exist. 

mkdir -p ${dir}

if [ $? -ne 0 ]; then
    echo "Couldn't create directory" 
    exit 1
fi

echo ${writestr} > ${writefile}

if [ $? -ne 0 ]; then
    echo "Couldn't write to file ${writefile}" 
    exit 1
fi
