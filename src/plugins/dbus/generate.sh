#!/bin/bash

# Generate escaped string from xml file, example:
# $ ./generate.sh com.nokia.NonGraphicFeedback1.Backend.xml dbus_plugin_introspect_string

input=$1
output=$1.h
string=$2

echo -n "const char *$string = \"" > $output
perl -p -e 's/\"/\\"/g' $input | perl -p -e 's/\n/\\n/' >> $output
echo '";' >> $output

