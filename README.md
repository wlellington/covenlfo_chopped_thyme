# COOL TITLE HERE

Alternate firmware for CCTV's covenlfo a "super witchy eurorack LFO Module based on SAMD21/ Xiao"

## Changes made in this version:
1. Changed formatting and performed various style cleanups, added comments
2. Replaced hardcoded pin names with #defines for readability, same for a few constants
3. Added new sync/clock/freq devisions to div table
4. Updated phasor calculation math to:
	- Allow the first divisor value to be values other than one and for funky divisions (good in sync mode)
	- Use a "base phasor" for phasors 2,3,4 rather than relying on phasor1
5. Use freq CV and pot value as input to create clock mult/division in sync mode
     This required using timout to jump out of sync mode, rather than pot movement
6. Changed CV and Pot behavior in free running mode to sum values BEFORE HZ table lookup (for better CV based control)
7. Added "spike" LFO waveshape (spaced inverted exponentials) (includes new table lookup file)
8. Added rising 16 step LFO waveshape
9. Added random freq squarewave LFO waveshape with octave relationships (renamed existing wave "SAMPHOLD")
10. Added random "on/off" gate "randgate" LFO w/ 50/50 chance for state change on edge
11. Added random freq squarewave LFO waveshape with "free" frequency mults between 2 and 127 (any freq multiple in that range)
12. Refactored generator functions to have single return point (for consistancy, expandability, but mostly for multi mode support)
13. Added "multi mode" that randomly selects between algorithms Tri, saw, ramp, square, samphold, psike, risestep, randsqroct, and randgate
      This mode takes a lot of inspiration from the QU-BIT NanoRand's Green mode
(WIP) *14) Added random trigger LFO that has 50/50 chance to create a trigger each cycle (fun in sync mode)

Flash your XIAO board using the same protocol you would for the base module. If the arduino IDE requires that you make a new folder with the source, be sure to move the "antilog.h" and "tables.h" files into that same folder.


![image of module](https://github.com/cctvfm/covenlfo/blob/main/IMG_E0173-01.jpeg)

# How to Hack Your Coven LFO  

If you are interested in modifying the code, you will first need to download the latest version of Arduino IDE which can be found here:  

https://www.arduino.cc/en/software 

Now, follow these instructions so that Arduino can recognize our board, the Seeeduino XIAO.  

https://wiki.seeedstudio.com/Seeeduino-XIAO/ 

Next, we need to install an additional library so that your code will compile. This library can be downloaded directly from inside the Arduino IDE by going to Tools> Manage Libraries... 

Once the Library Manager opens you can use the search bar located at the top to find the library we will need to install. Using the search bar, type in “FlashStorage_SAMD” and install the latest version. Note that this code was written with version 1.3.2 of the FlashStorage_SAMD library.

Select the Xiao Board: Tools -> Board -> Seeed SAMD -> Seeeduino Xiao

Once you have the Arduino IDE installed and recognizing your XIAO and have installed the “FlashStorage_SAMD” library, you are now ready to compile the code and modify it until your heart and ears are content.  

Read the comments for more info on what to HACK
