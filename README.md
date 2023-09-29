# Lars Walenius-GPSDO
Files for my Lars Walenius (RIP) GPSDO project

Sept-2023: NOTE that I have a new development, V4.1. Have a look at that project as well.

The blog of the project can be found here: https://www.eevblog.com/forum/projects/lars-diy-gpsdo-with-arduino-and-1ns-resolution-tic/ it also includes my activities, stumbling and experiences.

My own blog will detail my changes and experiences with this project. It can be found here : http://www.paulvdiyblogs.net/2020/07/a-high-precision-10mhz-gps-disciplined.html

I have started a new blog and a new github project to deal with the monitoring, measuring and logging of the GPSDO to keep this part cleaner.
The blog can be found here (http://www.paulvdiyblogs.net/2020/10/monitoring-measuring-logging-gpsdo.html), and the dedicated Github here (https://github.com/paulvee/GPSDO-Monitoring).

Firmware and scripts
====================
The version of the Arduino code I use (gpsdo_V3_70) is a modified one from Lars' original to be able to use the circuit without the 1Mohm discharge resistor.
It also implements a PID based fan controller for the ambient temperature of the PGSDO enclosure itself. Controller Schematic V2.1 shows the hardware components for the fan driver. Look at my blog for details. This version of the Lars code also implements the run-time setting of the fan controller parameters, so you don't need to re-compile all the time. The Help file (f1) and the variable settings (f2) incorporate these changes. Note that these values are not stored in the EEPROM, there is no space left. The latest version 3.70 has a few updates mostly on the PID controller for the fan. I noticed that it was not working correctly when my office temperature plummited in the winter time. I researched the issue and learned a lot more about tuning PID loops. The way I'm using it is very different from most other textbook applications, due to the very large inertia that the temperature inside the enclosure poses for the PID. I ended up changing the PID gain parameters dramatically, and also cleaned-up the code and comments.

There are two Python scripts that run on a Raspberry Pi that collect the Lars report through the serial interface of the Arduino Nano, and by the end of the day e-mail me the daily results to my account. The mail script is activated by a cron job. The monitoring script is installed by a simple systemd service file.


PCB layout
==========
I made four mistakes on the layout of the PCB and did a few things I should have done better. 
Let me sum them up and also add some advise on how to use the board.
1. The TO-92 footprint for the LM35's are the wrong way around. This is easy to fix by bending the middle pin the other way so you can swap the power and ground pins.

2. The footprint for the Oscilloquartz OCXO is wrong. To isolate the OCXO temperature from the PCB, I used a 10mm piece of foam, on the bottom and also build a box around it with 10mm thick walls. I extended the OCXO leads so they are long enough to protrude through the foam to the bottom siude of the PCB. Where the two wrong connections are, I drilled a 2mm hole in the PCB and used an isolated wire to go to the bottom side and then connect them to the right connections. Because this OCXO is also a 12V version, I connected it straight to the incoming 12VDC supply. You could also use a 15VDC supply and add another LM7812 and mount that on the chassis. The output of the LM7812 goes to the OCXO and also to the input terminal on the PCB where the regular 12V DC comes in. Sounds complicated, but really isn't.

3. Not only the legs, but also the locations for the two LM35 temp sensors are swapped. U6 is the ambient temperature sensor that is now connected to the OCXO and U5 is the OCXO sensor that is now measuring the ambient temperature. You could cut the traces and swap the connections to the Arduino, or make the changes in the software, which is easier. My Version 3 of the firmware has that modification.

4. There is no connection to the positive side of C117, a Tantalum decoupling capacitor. You can make this connection, or not simple not install C117.

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

Isolated Outputs and Fan Controller
===================================
I have created a PCB that can be placed inside the GPSDO enclosure. It will add two isolated output channles for the 10MHz and also the fan controller. The circuit diagram and the Gerber files are avaiable. Isolated Outputs.jpg & GPSDO-iso_gerberV2.zip

