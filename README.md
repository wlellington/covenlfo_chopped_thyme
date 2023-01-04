# Chopped Thyme - Alternate Coven LFO Firmware

Alternate firmware for CCTV's DIY Coven LFO eurorack module, a "super witchy eurorack LFO Module based on SAMD21/ Xiao"

This firmware adds some new synchronization features, as well as several new waveshapes, and a "multi mode" that cycles randomly between waveshapes.

### Clock Division and Multiplication
In sync mode, the module can now make internal multiplications or divisions of the incoming clock signal based on the postion of the speed knob and the CV "FREQ" input. This allows for fast clocks to be cut down to slower wave cycles, or for slow clocks to be multiplied up for more rhythmic results. This also alows the module to be used as a customizable clock divider with four outputs if the square LFO waveform is selected. Several odd divisions have been added to get some "groovy" results.

The module can make clock divisions of 1/2, 1/3, 1/4, 1/5, 1/8, 1/12, and 1/16 and can multiply by 2, 3, 4, 5, 8, 12, and 16. With the speed knob at 12 o'clock, no division or multiplication is applied. Divisions occur when turned left (counter clockwise), and multiplications occure when turned right (clockwise). The FREQ cv input applies no change at 0V, divisions with negative voltage, and multiplications with positive voltage.

This also enabled an update to the sync mode detection, which is now based on a 3 second timeout rather than knob movement. This means the module will return to free running mode if no sync pulses are detected for 3 seconds. This can be changed with a constant value at the top of the source code.

### New waveforms
Several new waveforms have been added to the existing set. A complete list of all waveforms in the order they appear in the menu (with updated names) can be found at the bottom of this section.

#### Old Waveforms:
1. Triangle
2. Saw
3. Square
4. Random (ranamed SAMPHOLD)

#### New Waveforms:
1. Ramp - Inverse saw wave that rises over a cycle, rather than falling.
2. Spike - An exponetial spike centered around the begining/end of each cycle, creating a rapid upward and downward sweep.
3. RiseStep - A "quantized" rising ramp that moves up through 16 discrete values over each cycle.
4. Random Square Octave (RANDSQROCT) - Generates a square wave of a random power of 2 divisor of the base frequency every cycle. This means resulting. squarewaves will have octave relationships with eachother. With a 1hz input clock example, on cycle 1, the wave could be 4 hz, on cycle two it could be 8 hz, on cycle three it could be 2 hz. This is a lot of fun near or above audio rate.
5. Random Gate - Flips a 50/50 coin each cycle, creating gates of random lenth that are multiples of the base frequency. Each cycle, the value of the output has a 50% chance to flip from low to high, or high to low (respectively) or stay the same.
6. Random Square (RANDSQR) - Similar to the Random Square Octave mode, but without clean octave relationships. This means that a base frequency of 1 hz could yeild waves of any frequency up through 128 times the base frequency. This is less musical, and more noisy.
7. (WORK IN PROGRESS) Random Trigger - Similar to Random Gate, but generates a trigger pulse on each coinflip, rather than switching from low to high or high to low.

#### All waveforms in order as they appear on in the mode menu:
1. TRIANGLE    
2. SAW         
3. RAMP        
4. SQUARE      
5. SAMPHOLD     
6. SPIKE       
7. RISESTEP    
8. RANDSQROCT  
9. RANDGATE    
10. RANDSQR     
11. MULTIMODE

This will change as new waves are implemented

### Multi Mode
In addition to the new waveforms, a new multi mode has been added to randomly switch between several waveforms. It is accessed as a final waveform at the end of the list, before the mode wraps back around to the base triangle wave. This mode takes inspiration from the Green mode on the Qu-bit Nano Rand, but is built from different waveforms and handles a bit differently.

Modes 1 - 9 are accessed by this mode, but the range can be changed by altering some constants in the source if desired. The Random Square and Random Trigger modes were left out, since they are fairly similar to the Random Square Octave waveform anyway.

At audio frequencies, this mode sounds pretty wild, so try using it as a digital noise source.

## Changes made in this version:
1. Changed formatting and performed various style cleanups, added comments
2. Replaced hardcoded pin names with #defines for readability, same for a few constants
3. Added new sync/clock/freq devisions to div table
4. Updated phasor calculation math to:
	- Allow the first divisor value to be values other than one and for funky divisions (good in sync mode)
	- Use a "base phasor" for phasors 2,3,4 rather than relying on phasor1, allowing for free running timings between all four
5. Use freq CV and pot value as input to create clock mult/division in sync mode. This required using timout to jump out of sync mode, rather than pot movement
6. Changed CV and Pot behavior in free running mode to sum values BEFORE HZ table lookup (for better CV based control)
7. Added "spike" LFO waveshape (spaced inverted exponentials) (includes new table lookup file)
8. Added rising 16 step LFO waveshape
9. Added random freq squarewave LFO waveshape with octave relationships (renamed existing wave "SAMPHOLD")
10. Added random "on/off" gate "randgate" LFO w/ 50/50 chance for state change on edge
11. Added random freq squarewave LFO waveshape with "free" frequency mults between 2 and 127 (any freq multiple in that range)
12. Refactored generator functions to have single return point (for consistancy, expandability, but mostly for multi mode support)
13. Added "multi mode" that randomly selects between algorithms Tri, saw, ramp, square, samphold, psike, risestep, randsqroct, and randgate. This mode takes a lot of inspiration from the QU-BIT NanoRand's Green mode
14. (Work in progress) Added random trigger LFO that has 50/50 chance to create a trigger each cycle (fun in sync mode)

## Setup

Flash your XIAO board using the same protocol you would for the base module. If the Arduino IDE requires that you make a new folder with the source, be sure to move the "antilog.h" and "tables.h" files into that same folder. I have added the folder generated by my IDE so that you can just use that as a starting point, rather than making a new sketchbook.
