# Lars-GPSDO
Files for my Lars GPSDO project


I made a two mistakes and did a few things I should have done better on the pcb layout. 
Let me sum them up.
1. The TO-92 footprint for the LM35's are the wrong way around. This is easy to fix by bending the middle pin the other way so you can swap the power and ground pins.
2. One of the footprints for the OCXO's is wrong. It doesn't matter because another footprint is the same anyway.
3. The location of U3 is a little too close to the edge of the board. I use metal enclosures where the board slips in on rails, and I had to cut the pins on the edge of the chip and I used some tape over them to make sure they don't short to the enclosure.
4. If you use 5V OCXO's, there is no need to fiddle with the power supply for them. If you use 12V versions, you need to feed 15VDC to the board and also add a 7812 to create that voltage before you pass it on to U105 and directly to the OCXO. You can use the place of L103 to fit the 7812. I bolted the 7812 to the enclosure as a heatsink and added a small cap directly on the leads for filtering.
5. There are provisions on the board for sine wave OCXO's.
6. I forgot to add pins for a power LED because at first I didn't think I needed one. You can easily add one and tie it to any exposed 5V place with a resistor and ground.
7. L101 is located on the bottom side, just in case a large size OCXO could be in the way on the top. I have always mounted it on top.
8. I suggest you seriously isolate the OCXO you are going to use with a foam box. Some of them radiate a lot of heat. I included U6, the LM35, within the isolation box, sometimes with extended leads to properly measure the temperature. In the case of the Trimble, where I didn't make a footprint because it was too large, I mounted it on top of a little foam and connected the pins with short wires before I added a box on top of it.
