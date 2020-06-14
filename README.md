# Lars Walenius-GPSDO
Files for my Lars Walenius (RIP) GPSDO project

The blog of the project can be found here: https://www.eevblog.com/forum/projects/lars-diy-gpsdo-with-arduino-and-1ns-resolution-tic/ it also includes my activities, stumbling and experiences.

The version of the Arduino code is a modified one from Lars' original one to be able to use the circuit without the 1Mohm discharge resistor. Basically, all it does is change the ISR a little.

I made three mistakes on the layout of the PCB and did a few things I should have done better. 
Let me sum them up and also add some advise on how to use the board.
1. The TO-92 footprint for the LM35's are the wrong way around. This is easy to fix by bending the middle pin the other way so you can swap the power and ground pins.

2. The footprint for the Oscilloquartz OCXO is wrong. To isolate the OCXO temperature from the PCB, I used a 10mm piece of foam, on the bottom and also build a box around it with 10mm thick walls. I extended the OCXO leads so they are long enough to protrude through the foam to the bottom siude of the PCB. Where the two wrong connections are, I drilled a 2mm hole in the PCB and used an isolated wire to go to the bottom side and then connect them to the right connections. Because this OCXO is also a 12V version, I added another LM7812 and mounted that on the chassis. The output of the LM7812 goes to the OCXO and to the input terminal on the PCB where the regular 12V DC comes in. Sounds complicated, but really isn't.

3. The locations for the two LM35 temp sensors are swapped. U6 is the ambient sensor that is now connected to the OCXO and U5 is the OCXO sensor that is now measuring the ambient temperature. Its easy to cut the traces and swap the connections to the Arduino, or make the changes in the software. My Version 2 has that modification.

a. The location of U3 is a little too close to the edge of the board. I use metal enclosures where the board slips in on rails, and I had to cut the pins on the edge of the chip and I used some tape over them to make sure they don't short to the enclosure.

b. There are provisions on the board for sine wave OCXO's.

c. I forgot to add a connector on the PCB for a power LED because at first I didn't think I needed one. You can easily add an LED and tie it to any exposed 5V place with a resistor and ground.

d. L101 is located on the bottom side, just in case a large size OCXO could be in the way on the top. I have always mounted it on top.

e. I use a socket for the GPS module, and bend the pins of the module a bit to flatten the angle. I also use a cable to connect it to the back panel as an SMA socket.

f. I suggest you seriously isolate the OCXO you are going to use with a foam box. Some of them radiate a lot of heat. I included U6, the LM35, within the isolation box to properly measure the OCXO temperature. In the case of the Trimble, where I didn't make a footprint because it was too large, I mounted it on top of a little foam and connected the pins with short wires before I added a box on top of it.

g. I have not used U7, I put a socket in it's place. This the special chip that creates a 1Hz output from the 10MHz from the GPSDO yet. Info can be found here: http://www.leapsecond.com/pages/ppsdiv/ppsdiv.asm
h. Make sure you use a quality 1nF NPO capacitor for C1. I used Chinese general purpose ones before, and there was a lot more stability when I changed it.

i. After the burn-in period, make sure you take the trimmer out and replace it with resistors. It makes a big difference on the temperature sensitivity because it is so close to the oven of the OCXO's.

j. The ambient sensor LM35 (U6) is too close to the OCXO, so it picks up a lot of heat from it's temperature. The quick fix is to add some 8-10cm leads to the pins of the LM35 and move it away. I suggest you move it close to the circuit around C1, because that has a significant temperature influence on the loop operation.

k. In earlier schematics, I used a 2N2907 transistor for Q101, to boost the 5V reference output. That transistor may raise the reference output level. Use a 2N3906 transistor instead. Watch out of the change in pin-out, the 2N3906 has the e-c swapped compared to the 2N2907.


IF you are a purist, it's easy to use this PCB with the original Lars' design by using some wires to bridge the extra gates. R7 can be changed back to the original 10M Ohm value and if you either ground the end that now goes to the Arduino output pin, or change the software to always output a low on that pin, you have the original design back.

