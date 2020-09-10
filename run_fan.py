#!/usr/bin/python3
#-------------------------------------------------------------------------------
# Name:        run_fan.py
# Purpose:     Use PWM to run a fan to keep the a temperature in check
#              This program is managed by a systemd script
#
# Author:      Paul Versteeg
#
# Created:     01-12-2013, modified june 2019, september 2020
# Copyright:   (c) Paul 2013, 2019, 2020
# Licence:     <your licence>
#-------------------------------------------------------------------------------

# sudo apt-get install python-rpi.gpio python3-rpi.gpio
import RPi.GPIO as GPIO
from time import sleep
import subprocess
import shlex
import string
import sys
import os
import traceback
import glob

DEBUG = False
TRACE = True

FAN_PIN = 17 # GPIO 17
# DS18B20 pin GPIO 22
# add to /boot/config.txt :
#    dtoverlay=w1-gpio,gpiopin=22

GPIO.setwarnings(False) # when everything is working you could turn warnings off
GPIO.setmode(GPIO.BCM)  # choose BCM numbering scheme.
# set GPIO port as output driver for the Fan
GPIO.setup(FAN_PIN, GPIO.OUT)


Fan = GPIO.PWM(FAN_PIN, 100) # create object Fan for PWM on port 22 at 100 Hertz
Fan.start(0)            # start Fan on 0 percent duty cycle (off)

delay = 1               # seconds of delay between samples
cool_baseline = 33      # start cooling from this temp in Celcius onwards
                        # 34 degrees enclosure temp is 50 degrees inside GPSDO
pwm_baseline = 40       # lowest PWM to keep the fan running
factor = 3              # multiplication factor
max_pwm = 100           # maximum PWM value
fan_running = False     # helps to kick-start the fan

# DS18B20 data locations
base_dir = '/sys/bus/w1/devices/'
device_folder = glob.glob(base_dir + '28*')[0]
device_file = device_folder + '/w1_slave'


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




def main():
    global fan_running
    '''
    This program controls a Fan by using PWM.
    The Fan will probably not work below 40% dutycycle, so that is the
    fan PWM baseline. The maximum PWM cannot be more than 100%.

    When the temperature is above the cool_baseline in Celcius, we will start to cool.
    When the temperature reaches 70 degrees, we would like to run the fan at max speed.

    To make the PWM related to the temperature, strip the actual temp from the
    cool baseline, multiply the delta with 3 and add that to the the baseline
    PWM to get 100% at 70 degrees.  This can be changed by changing the muliplier variable.

    I have selected a PWM frequency of 100Hz to avoid high frequency noise, but
    you can change that.
    '''
    # setup the W1 interface in the kernel for the DS18B20 sensor
    os.system('modprobe w1-gpio')
    os.system('modprobe w1-therm')

    try:
        while True:

                ds_temp = read_dsb20()

                if DEBUG : print ("ds temp = : ", ds_temp)

                if ds_temp < cool_baseline - 0.5 : # a little hysteresis
                    if DEBUG : print("too low")
                    Fan.ChangeDutyCycle(0) # turn Fan off
                    fan_running = False

                if ds_temp > cool_baseline :
                    if DEBUG : print("fan temp reached")
                    if fan_running :
                        duty_cycle = ((ds_temp - cool_baseline)*factor)+pwm_baseline
                        if duty_cycle > max_pwm : duty_cycle = max_pwm # max = 100%
                        if DEBUG : print("adjust fan, dc = ", duty_cycle)
                    else:
                        # not running yet, kick-start the fan for one cycle
                        if DEBUG : print("kick-start fan")
                        duty_cycle = 100
                        fan_running = True

                    Fan.ChangeDutyCycle(duty_cycle)   # output the pwm value

                    if DEBUG : print("pwm {:.2f}".format(duty_cycle))
                    if TRACE : print("ds_temp = \t{:.2f}\tdc = \t{:.2f}".format(ds_temp, duty_cycle))
                sleep(delay)

    # the following will allow you to kill the program, you can delete these lines if you want
    except KeyboardInterrupt:
        Fan.stop()      # stop the PWM output
        GPIO.cleanup()  # clean up GPIO on CTRL+C exit()

    except Exception as e:
        sys.stderr.write("Got exception: %s" % e)
        if DEBUG :
            print(traceback.format_exc())
            print("GPIO.cleanup")
        GPIO.output(FAN_PIN, GPIO.LOW)
        GPIO.cleanup() # release any GPIO pin assignments
        os._exit(1)


if __name__ == '__main__':
    main()