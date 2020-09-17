# Lars Walenius-GPSDO
Files for my Lars Walenius (RIP) GPSDO project

The blog of the project can be found here: https://www.eevblog.com/forum/projects/lars-diy-gpsdo-with-arduino-and-1ns-resolution-tic/ it also includes my activities, stumbling and experiences.

My own blog will detail my changes and experiences with this project. It can be found here : http://www.paulvdiyblogs.net/2020/07/a-high-precision-10mhz-gps-disciplined.html

Firmware and scripts
====================
The version 2.0 of the Arduino code I use is a modified one from Lars' original one to be able to use the circuit without the 1Mohm discharge resistor. Basically, all this version (gpsdo_V2) does is change the ISR a little and adds a software probe to see what is going on inside the ISR.

I have also added a few Python scripts that I use on my Raspberry Pi's that are connected to the USB serial out connector of the Arduino Nano. There is a script (Serial_monitor.py) that collects the output, and a script to mail the daily results to my account based on a cron setting. The log files go to a USB flash stick, not to the SD card, to avoid wear and tear. You need to also install an email program on the RPi of course. Version 3.4 of the Lars firmware adds low-pass filters for the display of the temperature sensors, and meaningfull labels for them in the header.

I have also added a script (run_fan.py) that takes the values of a DS18B20 temperature sensor connected to a Raspberry Pi, which drives a little 30mm 5V fan with a PWM signal based on the temperatures. This sensor is located in the extra plastic enclosure I put my Oscilloquartz GPSDO enclosure in. It also houses the Raspberry Pi, which is powered from the GPSDO regulated 5V supply. This plastic enclosure provides another level of insulation from sudden room temperature changes to the GPSDO. Without the fan, the enclosure gets too hot. The PWM driven fan will stablize the temperatures inside the plastic enclosure within a very small temperature band.

Another version of the Serial_monitor(V2) takes the DS18B20 sensor reading and addes that in front of the Lars firmware report, such that I can measure and graph the OCXO oven temperature, the ambient temperature inside the aluminum GPSDO enclosure, and the temperature inside the plastic enclosure together with the usual ns and DAC values.

There is a new version of run_fanV2_0.py, that implements a PID algorithm to better control the fan. Although it works very well, I'm now doing some long-term testing with this version. The published version has my lates PID tweaks in them, but they may be different for your application.

PCB layout
==========
I made three mistakes on the layout of the PCB and did a few things I should have done better. 
Let me sum them up and also add some advise on how to use the board.
1. The TO-92 footprint for the LM35's are the wrong way around. This is easy to fix by bending the middle pin the other way so you can swap the power and ground pins.

2. The footprint for the Oscilloquartz OCXO is wrong. To isolate the OCXO temperature from the PCB, I used a 10mm piece of foam, on the bottom and also build a box around it with 10mm thick walls. I extended the OCXO leads so they are long enough to protrude through the foam to the bottom siude of the PCB. Where the two wrong connections are, I drilled a 2mm hole in the PCB and used an isolated wire to go to the bottom side and then connect them to the right connections. Because this OCXO is also a 12V version, I connected it straight to the incoming 12VDC supply. You could also use a 15VDC supply and add another LM7812 and mount that on the chassis. The output of the LM7812 goes to the OCXO and also to the input terminal on the PCB where the regular 12V DC comes in. Sounds complicated, but really isn't.

3. Not only the legs, but also the locations for the two LM35 temp sensors are swapped. U6 is the ambient temperature sensor that is now connected to the OCXO and U5 is the OCXO sensor that is now measuring the ambient temperature. You could cut the traces and swap the connections to the Arduino, or make the changes in the software, which is easier. My Version 3 of the firmware has that modification.

a. The location of U3 is a little too close to the edge of the board. I use metal enclosures where the board slips in on rails, and I had to cut the pins on the edge of the chip and I also used some tape over them to make sure they don't short to the enclosure.

b. There are provisions on the board for sine wave OCXO's.

c. I forgot to add a connector on the PCB for a power LED because at first I didn't think I needed one. You can easily add an LED and tie it to any exposed 5V place with a resistor and ground.

d. Use a high efficiency LED for the lock indicator. There is a noticeable temperature effect when there is no lock. I now use a 10K series resistor to the LED to minimize the effect.

e. I use a socket for the GPS module, and bend the pins of the module a bit to flatten the angle to roughly 45-60 degrees. I also use an antenna cable to connect it to the back panel through an SMA socket.

f. I suggest you seriously isolate the OCXO you are going to use with a foam box. Some of them radiate a lot of heat. I included U6, the LM35, within the isolation box to properly measure the OCXO temperature. In the case of the Trimble, where I didn't make a footprint because it was too large, I mounted it on top of a little foam and connected the pins with short wires before I added a box on top of it.

g. I have not used U7, I put a socket in it's place. This the special chip that creates a 1Hz output from the 10MHz from the GPSDO yet. Info can be found here: http://www.leapsecond.com/pages/ppsdiv/ppsdiv.asm

h. Make sure you use a good quality 1nF NPO capacitor for C1. I used Chinese general purpose ones before, and there was a lot more stability when I changed it.

i. After the burn-in period, make sure you take the trimmer out and replace it with resistors. It makes a big difference on the temperature sensitivity because it is so close to the oven of the OCXO's.

j. The ambient sensor LM35 (U6) is too close to the OCXO, so it picks up a lot of heat from it's temperature. The quick fix is to add some 5-8cm leads to the pins of the LM35 and move it out of the way. I suggest you move it close to the circuit around C1, because that has a significant temperature influence on the loop operation.

k. The current output booster for the REF02 is not needed, and gives problems. The 74HC14N circuit (U102) and the related parts draws less than 4 mA. The REF02 can drive up to 10 mA. Replace R101 with a 0 Ohm resistor and do not install Q101.
 

IF you are a purist, it's easy to use this PCB with the original Lars' design by using some wires to bridge the extra gates. R7 can be changed back to the original 10M Ohm value and if you either ground the end that now goes to the Arduino output pin, or change the software to always output a low on that pin, you have the original design back.

Modifications
=============
My modifictions based on the original V1 schematics that the PCB's are based upon, are documented in the V2 schematics.
