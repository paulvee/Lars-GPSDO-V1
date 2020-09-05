#!/usr/bin/python3.7
#-------------------------------------------------------------------------------
# Name:        serial_monitor.py
# Purpose:     Serial port monitoring on a RaspberryPi
#
# Author:      paulv
#
# Created:     20-07-2018
# Copyright:   (c) paulv 2018 2019
# Licence:     <your licence>
#-------------------------------------------------------------------------------

# sudo apt-get install python3-serial
import serial
import logging
import logging.handlers
import sys
import os
import traceback


# To enable the serial port on the GPIO connector, use raspi-config or:
# sudo nano /boot/config.txt
# enable_uart=1
# reboot

DEBUG = False

port = "/dev/ttyAMA0"
serialPort = serial.Serial(port, baudrate=9600, timeout=10.0)

# data path is on a USB stick to protect the SD card
data_path = "/mnt/usb/"

# -- Logger definitions
LOG_FILENAME = data_path+"ocxo.log"
LOG_LEVEL = logging.INFO  # Could be e.g. "INFO", "DEBUG", "ERROR" or "WARNING"


class MyLogger(object):
    '''
    Replace stdout and stderr with logging to a file so we can run this script
    even as a daemon and still capture all the stdout and stderr messages in the log.

    '''
    def __init__(self, logger, level):
            """Needs a logger and a logger level."""
            self.logger = logger
            self.level = level

    def write(self, message):
            # Only log if there is a message (not just a new line)
            # typical for serial data with a cr/lf ending
            if message.rstrip() != "":
                self.logger.log(self.level, message.rstrip())


def init():
    global logger, handler

    if DEBUG:print ("Setting up the logger functionality")
    logger = logging.getLogger(__name__)
    logger.setLevel(LOG_LEVEL)
    handler = logging.handlers.TimedRotatingFileHandler(LOG_FILENAME, when="midnight", backupCount=31)
    formatter = logging.Formatter('%(asctime)s %(levelname)-8s %(message)s')
    handler.setFormatter(formatter)
    logger.addHandler(handler)

    # pipe the stdout and stderr messages to the logger
    sys.stdout = MyLogger(logger, logging.INFO)
    sys.stderr = MyLogger(logger, logging.ERROR)


def main():
    if DEBUG:print("Serial logger")

    init()

    if DEBUG:print("Opened port", port, "for serial tracing")

    try:
        while True:
            while (serialPort.inWaiting()>0):
                try:
                    ser_input = serialPort.readline().decode('utf-8')
                    print(ser_input)
                except (OSError, serial.serialutil.SerialException):
                    pass
                    if DEBUG : print("No data available")
                except UnicodeDecodeError:
                    pass
                    if DEBUG: print("decode error")

    except KeyboardInterrupt: # Ctrl-C
        print("\nCtrl-C - Terminated")
        os._exit(1)

    except Exception as e:
        sys.stderr.write("Got exception: %s" % (e))
        print(traceback.format_exc())
        os._exit(1)


if __name__ == '__main__':
    main()
