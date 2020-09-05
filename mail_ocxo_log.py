#!/usr/bin/python
#-------------------------------------------------------------------------------
# Name:        module1
# Purpose:
#
# Author:      paulv
#
# Created:     13-03-2020
# Copyright:   (c) paulv 2020
# Licence:     <your licence>
#-------------------------------------------------------------------------------

import os
import re
import subprocess
import sys, traceback
import email
from time import time, sleep, gmtime, strftime, localtime
import zipfile

VERSION="1.0" #  initial version
DEBUG = False

# here is where we store the errors and warnings
log_file = "/mnt/usb/ocxo.log"
zip_file = "/mnt/usb/bliley.zip"
mail_address = "pw.versteeg@gmail.com"


def mail_err_log():
    '''
    Just before a new day has been found by cron, this function emails the daily
    local error logs, but only if an error condition has been reported.
    '''
    try:
        print("zip the file")
        os.chdir('/mnt/usb')
        zipfile.ZipFile('bliley.zip', mode='w').write('ocxo.log', compress_type=zipfile.ZIP_DEFLATED)
    except Exception as e:
        print(e)
    try:
        if os.path.isfile(log_file):
            with open(zip_file, "r") as fin:
                f_data = fin.read()

            # send it out as an attachement
            cmd = 'mpack -s "Bliley log file" {} {}'.format(zip_file, mail_address)
            print("mail_ocxo_log cmd : {}".format(cmd))
            subprocess.call([cmd], shell=True)

    except Exception as e:
        send_mail("error", "Unexpected Exception in mail_ocxo_log() {0}".format(e))
        return



def main():
    print("Mail ocxo log Version {}".format(VERSION))
    mail_err_log()

if __name__ == '__main__':
    main()
