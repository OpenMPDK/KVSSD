#!/usr/bin/python

import sys
import os
import subprocess 

target = 'all';
if (len(sys.argv) > 1):
    target = sys.argv[1]

cmd = "make %s 2>&1 | tee mk_%s.log" % (target, target)

print "running " + cmd + "\n"
runp = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
output, err = runp.communicate()
