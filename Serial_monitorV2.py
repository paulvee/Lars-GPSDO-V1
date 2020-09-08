#!/usr/bin/python3.7
#-------------------------------------------------------------------------------
# Name:        serial_monitorV2.py
# Purpose:     Serial port monitoring on a RaspberryPi with ambient temp logging
#              and PWM fan control to stabelize the ambient temperature
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
import shlex
import string
import glob

# To enable the serial port on the GPIO connector, use raspi-config or:
# sudo nano /boot/config.txt
# enable_uart=1
# reboot

DEBUG = False
ds_sensor = True


# DS18B20 sensor data locations
base_dir = '/sys/bus/w1/devices/'
# try to see if we have a DS18B20 sensor installed
try:
    device_folder = glob.glob(base_dir + '28*')[0]
    device_file = device_folder + '/w1_slave'
    print("ds_sensorsor found")
except Exception as e:
    ds_sensor = False
    print("no DS18B20 sensor found")



# IIR filter for the temperature sensor
ds_temp_IIR = 40.0        # this value will be primed at startup
IIR_Filter_Weight = 16  # use only 1/16 of the new value into account


def read_temp_raw():
    f = open(device_file, 'r')
    lines = f.readlines()
    f.close()
    return lines


def read_dsb20():
    lines = read_temp_raw()
    while lines[0].strip()[-3:] != 'YES':
        time.sleep(0.2)
        lines = read_temp_raw()
    equals_pos = lines[1].find('t=')
    if equals_pos != -1:
        temp_string = lines[1][equals_pos+2:]
        temp_c = float(temp_string) / 1000.0    # temp in Celcius
        return temp_c


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
    global logger, handler, ds_temp_IIR

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

    if ds_sensor:
        # setup the W1 interface in the kernel for the DS18B20 sensor
        os.system('modprobe w1-gpio')
        os.system('modprobe w1-therm')
        # prime the filter
        ds_temp_IIR = read_dsb20()


def main():
    global ds_temp_IIR

    if DEBUG:print("Serial logger V2")

    init()

    if DEBUG:print("Opened port", port, "for serial tracing")

    try:
        while True:
            while (serialPort.inWaiting()>0):
                try:
                    ser_input = serialPort.readline().decode('utf-8')
                    if ds_sensor:
                        ds_temp = read_dsb20()
                        ds_temp_IIR = ds_temp_IIR + ((ds_temp - ds_temp_IIR) / IIR_Filter_Weight)
                        print(str("{:.2f}".format(ds_temp_IIR)) + "\t" + ser_input)
                    else:
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
