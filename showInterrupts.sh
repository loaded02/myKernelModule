#!/bin/bash
# Shows content of /proc/interrupts
while test 1
do
      clear
      echo ---- /proc/interrupts ----
      cat /proc/interrupts
      sleep 1
done
 
