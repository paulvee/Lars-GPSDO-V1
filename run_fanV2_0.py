#!/usr/bin/python3
#-------------------------------------------------------------------------------
# Name:        run_fan.py
# Purpose:     Use PWM to run a fan to keep the a temperature in check
#              This program is managed by a systemd script
#              The PWM is calculated by using a PID callableculation
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

DEBUG = True
TRACE = False
VERSION = "2.0"

FAN_PIN = 17 # GPIO 17
# DS18B20 pin GPIO 22
# to install kernel drivers, add this to /boot/config.txt :
#    dtoverlay=w1-gpio,gpiopin=22

GPIO.setwarnings(False) # when everything is working you could turn warnings off
GPIO.setmode(GPIO.BCM)  # choose BCM numbering scheme.
# set GPIO port as output driver for the Fan
GPIO.setup(FAN_PIN, GPIO.OUT)


Fan = GPIO.PWM(FAN_PIN, 100) # create object Fan for PWM on port 22 at 100 Hertz
Fan.start(0)            # start Fan on 0 percent duty cycle (off)

delay = 1               # seconds of delay between samples
cool_baseline = 33      # start cooling from this temp in Celcius onwards.
                        # 33 degrees enclosure temp is 52 degrees inside GPSDO
                        # and 65 degrees for the Oscilloquartz oven temperature
pwm_baseline = 25       # lowest PWM to keep the fan running and baseline
factor = 20             # gain factor
max_pwm = 100           # maximum PWM value
fan_running = False     # helps to kick-start the fan
Kp = 6
Ki = 0.5
Kd = 1

# DS18B20 data locations
base_dir = '/sys/bus/w1/devices/'
device_folder = glob.glob(base_dir + '28*')[0]
device_file = device_folder + '/w1_slave'

# IIR low pass filter
IIR_Filter_Weight = 3;  # IIC filter weight value
ds_temp = cool_baseline

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
        temp_c = float(temp_string) / 1000.0  # temp in Celsius
        return temp_c




def main():
    global fan_running, ds_temp
    '''
    This program controls a Fan by using PWM.
    The Fan will probably not work below a certain dutycycle, so set that value in
    fan PWM baseline. The larger the fan (inertia), the more important this is.
    The maximum PWM cannot be more than 100%.

    I have selected a PWM frequency of 100Hz to avoid high frequency noise, but
    you can change that.
    '''
    # setup the W1 interface in the kernel for the DS18B20 sensor
    os.system('modprobe w1-gpio')
    os.system('modprobe w1-therm')

    prev_error = 0
    integral = 0
    duty_cycle = pwm_baseline
    Fan.ChangeDutyCycle(50)   # kick_start the fan

    try:
        while True:

#            ds_temp = read_dsb20()
            ds_temp = ds_temp + ((read_dsb20() - ds_temp) / IIR_Filter_Weight);

            # PID calculation
            temp_error = ds_temp - cool_baseline
            integral = integral + temp_error * delay
            derivative = (temp_error - prev_error) / delay
            duty_cycle = pwm_baseline + (Kp * temp_error) + \
                        (Ki * integral) + (Kd * derivative)
            prev_error = temp_error

            if duty_cycle > max_pwm : duty_cycle = max_pwm # max = 100%
            if duty_cycle < pwm_baseline : duty_cycle = pwm_baseline
            if DEBUG :
                print("Kp = {:.2f}\t Ki = {:.2f}\t Kd = {:.2f}\t temp = {:.2f}\t dc = {:.2f}"\
                    .format((Kp * temp_error), (Ki * integral),(Kd * derivative), ds_temp, duty_cycle))

            Fan.ChangeDutyCycle(duty_cycle)   # output the pwm value

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