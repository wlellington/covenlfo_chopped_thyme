// Chopped Thyme Alternate Firmware for Coven LFO
// Writen by Wesley Ellington 
// Github: https://github.com/wlellington/covenlfo_chopped_thyme

// COVEN LFO Original Firmware  written by CCTVFM
// Github: https://github.com/cctvfm/covenlfo

// See README.md for list of new features, descriptions, and usage.

// Important things to note:
// - The PWM (DAC) outputs are all inverted, meaning a digital value of 0 is the max voltage 
//   and a value of 255 is min voltage (full negative)
// - PWM resolution is 0-255 on fast pwm outputs (not 0-1023 like on normal analogwrite) v- this can make heavily
// - The Phasors used to define these waveforms are only 8 bits, meaning that a full wave is only 256 samples
//   This can make heavily sloped waveforms (like spike) sound a bit funky when large jumps in value are made at low speed (rate)

#include <FlashAsEEPROM_SAMD.h>
#include <avr/pgmspace.h>
#include "antilog.h"
#include "tables.h"
#include <Arduino.h>

// Waveform mode number constants
#define TRIANGLE    1
#define SAW         2
#define RAMP        3
#define SQUARE      4
#define SAMPHOLD    5 
#define SPIKE       6
#define RISESTEP    7
#define RANDSQROCT  8
#define RANDGATE    9
#define RANDSQR     10
#define RANDTRIG    11
#define MULTIMODE   12
#define NUMWAVS     12
#define MULTISTART  1
#define MULTIEND    10          // This is non inclusive (10 means up through wave 9)
                                // I left out the free freq random squre since the oct version sounds more musical

#define FREQ     0
#define POT      1

#define HZPHASOR 91183.0        //phasor value for 1 hz.

long unsigned int accumulator1 = 0;
long unsigned int accumulator2 = 0;
long unsigned int accumulator3 = 0;
long unsigned int accumulator4 = 0;
long unsigned int basephasor;
long unsigned int phasor1;
long unsigned int phasor2;
long unsigned int phasor3;
long unsigned int phasor4;

char randNum[4];
char randMode[4];
long unsigned int lastPulse[4];

// Trigger length time
// 8ms but in microseconds (this is a bit longer than a normal trigger @ 5ms, but seems a bit more stable w/ longer times)
// This can be tweaked for whatever trigger time you need, just change the first value for the time in ms (*1000 to convert to microseconds)
#define TRIGLEN 8 * 1000 

FlashStorage(div_storage, int);
FlashStorage(wave_storage, int);
FlashStorage(init_storage, char);

////////////////////////////////////////////////////////////////////////////////////
//       DIVIDE DOWN ARRAYS                                                       //
//                                                                                //
////////////////////////////////////////////////////////////////////////////////////

#define DIVSIZE 6  
char divs[DIVSIZE][4]={
                      {1,3,7,11},
                      {1,2,4,8},
                      {1,4,8,16},
                      {2,3,5,7},
                      {3,4,5,7},
                      {256,64,16,4},
                      }; 

////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////

// +++++++++++++++++++++++++++++++++ Pin Map +++++++++++++++++++++++=++++++++++++++
// 1 : out 1
// 2 : out 3
// 3 : out 4
// 6 : pushbutton (pull up)
// 9 : out 2
// 13 : interrupt timer 1

// A0 : Potentiometer
// A4 : CV1 (FREQ)
// A8 : Random Seed? (Floating?)
// A10 : CV2 (SYNC) 

#define OUT1_PIN 1
#define OUT2_PIN 9
#define OUT3_PIN 2
#define OUT4_PIN 3

#define BUTTON_PIN 6

#define TIMER_PIN 13

#define POT_PIN A0
#define CV1_PIN A4
#define CV2_PIN A10
#define RAND_SEED_PIN A8

// ++++++++++++++++++++++++++++++ Button/Sync Times +++++++++++++++++++++++++++++++++++++
// All in ms
#define DEBOUCEDELAY 80
#define LONGPRESS    2000
#define SYNCTIMEOUT  3000

// +++++++++++++++++++++++++++++++ Global Vars +++++++++++++++++++++++++++++++++++++
char debounceState = 0;
unsigned long int debounceTime = 0; 
int waveSelect = 1;
int divSelect = 1;
unsigned long lastSettingsSave = 0;

bool Mode = 0; // 0 = POT and 1 = SYNC 
float sweepValue;
long unsigned int Time1 = 0;
long unsigned int Time2 = 0; 
long unsigned int prev_rise = 0; 
long unsigned int Periud = 0; // Period (arduino didn't allow use of word "period")
float syncFrequency = 0.0; 

// Clock mult/div scalar for sync mode
float scalar = 1.0;

void timerIsr();
void setupTimers();
void TCC0_Handler();

// +++++++++++++++++++++++++++++++++++ SETUP ++++++++++++++++++++++++++++++++++++++++
void setup() {
  pinMode(OUT1_PIN, OUTPUT);          // LFO 1
  pinMode(OUT2_PIN, OUTPUT);          // LFO 2
  pinMode(OUT3_PIN, OUTPUT);          // LFO 3
  pinMode(OUT4_PIN, OUTPUT);          // LFO 4
  pinMode(TIMER_PIN, OUTPUT);         // using pin 13 to check interupt on timer 1
  
  pinMode(BUTTON_PIN, INPUT_PULLUP);  // pin 6 pushbutton to select waveform
  
  pinMode(POT_PIN, INPUT);            //used for analogReading potentiometer
  pinMode(CV1_PIN, INPUT);            //used for analogReading freq (cv1) 
  pinMode(CV2_PIN, INPUT);            //used for analogReading sync (cv2) 
  readSettings();
  
  //Serial.begin(9600);
  
  setupTimers();                      //  **this may not be the right location
  randomSeed(analogRead(RAND_SEED_PIN));
}

// +++++++++++++++++++++++++++++++++ MAIN LOOP ++++++++++++++++++++++++++++++++++++++++++++
void loop() {
  
  static int modeCounter = 0;
  bool lastButtonState = 0;
  static bool buttonState = 0;
  static int syncState = 0;          // used to find find the leading edge to calculate period (static so it isn't updated to zero each loop)
  static long int syncCounter = 0;
  int Sync; 

  ////////////////////////////////////////////////////////////////////////////////////
  //       "EEPROM" Save                                                            //
  //       Store the settings after 60 seconds                                      //
  ////////////////////////////////////////////////////////////////////////////////////
  if((millis() - (lastSettingsSave)) > (60000)){
    saveSettings();
    lastSettingsSave = millis();
  }
  
  ////////////////////////////////////////////////////////////////////////////////////
  //       Sync mode                                                                //
  //       sync the 1:1 output with an incoming signal                              //
  ////////////////////////////////////////////////////////////////////////////////////
  Sync = analogRead(CV2_PIN); // CV2 pin A10 on XIAO 

  // Sync state machine
  // S0 - wait for first pulse, transition to 1
  // S1 - wait for falling edge, transition to 2
  // S2 - wait for second rising edge (end of measured cycle)
  // S3 - wait for second falling edge, transition back to 2 (loop between 2 and 3 indefinately)

  if((Sync < 370) && (syncState == 0)) {            //if analog read CV2 is less than 370 RISING EDGE
    // This only enters on the first pulse received,
    // following pulses alternate between syncstate 2 and 3     
    Time1 = micros();                               // Capture the time 
    syncState = 1;                                  // set syncState to 1 
  }

  else if((Sync > 370) && (syncState == 1)){        //FALLING
    syncState = 2; 
  }

  else if((Sync < 370) && (syncState == 2)){        //NEXT RISING EDGE
    syncState = 3;
    Time2 = micros();
    prev_rise = millis();
    Mode = 1;
    Periud = (float)(Time2 - Time1);                // Find the period by subtracting Time2 from Time1 
    syncFrequency = 1000000.0;
    syncFrequency = syncFrequency/Periud;           // NEW FREQUENCY HERE 
    //accumulator1=0; 
    Time1 = Time2;
    
    if(syncCounter == 0)                            // first time calculation, clear all accs
    {
      accumulator1=0;
      accumulator2=0;  
      accumulator3=0;
      accumulator4=0;
    }

    // divide out scalar to account for changing size of sync counter
    if((syncCounter % (int)((float) divs[divSelect-1][0] / scalar)) == 0){      // div 1
      accumulator1 = 0;
    }

    if((syncCounter % (int)((float) divs[divSelect-1][1] / scalar)) == 0){      // div 2
      accumulator2 = 0;
    }

    if((syncCounter % (int)((float) divs[divSelect-1][2] / scalar)) == 0){      // div 3
      accumulator3 = 0;
    }

    if((syncCounter % (int)((float) divs[divSelect-1][3] / scalar)) == 0){       // div 4
      accumulator4 = 0;
    }

    if( ((syncCounter % (int)((float) divs[divSelect-1][0] / scalar)) == 0) && 
        ((syncCounter % (int)((float) divs[divSelect-1][1] / scalar)) == 0) && 
        ((syncCounter % (int)((float) divs[divSelect-1][2] / scalar)) == 0) && 
        ((syncCounter % (int)((float) divs[divSelect-1][3] / scalar)) == 0)){
      syncCounter = 0;
    
    }
    syncCounter++;
  }
  
  else if((Sync > 370) && (syncState == 3)){      // last falling edge
    syncState = 2;
  }

  // Check for timeout window every 100 samples, leave sync mode if too long since last pulse
  modeCounter++;
  if(modeCounter>200) { //do this only every 200 samples
    if ((millis() - prev_rise) > SYNCTIMEOUT){
      Mode = 0;     
    }
    modeCounter = 0;   
  }

  ///////////////// END OF SYNC STUFF ////////////////////////  
     
  ////////////////////////////////////////////////////////////////////////////////////
  //       Button read                                                              //
  //       Read and debounce button presses                                         //
  ////////////////////////////////////////////////////////////////////////////////////

  if(digitalRead(BUTTON_PIN)==LOW && debounceState ==0){ //button has been pressed and hasn't been previously pressed                           
    debounceState = 1;                            //real button press?
    debounceTime = millis();
  }

  else if(debounceState == 1){
    if((millis() - debounceTime) > DEBOUCEDELAY){
      if(digitalRead(BUTTON_PIN) == LOW){
        debounceState = 2;
      }
      else{
        debounceState = 0;
      }
    }
  }

  else if(debounceState == 2){
    if(digitalRead(BUTTON_PIN) == HIGH){
      debounceState = 0;
      if((millis() - debounceTime) < LONGPRESS){         //short press to change waveform
        waveSelect++;                               //move to the next waveform
        if(waveSelect>NUMWAVS){                           //if we're trying to select more than 4 waveforms, cycle back to the 1st
          waveSelect=1;
        }
      }
    }
    else if((millis() - debounceTime) > LONGPRESS) {     //long press to change divisions
      debounceState = 3;
      debounceTime=millis();
      accumulator1=0; 
      accumulator2=0; 
      accumulator3=0; 
      accumulator4=0;
      phasor1 = 0;
      phasor2 = 0;
      phasor3 = 0;
      phasor4 = 0;
      delay(2000);
      
      divSelect++;

      if(divSelect>DIVSIZE){
          divSelect=1;
      }
    }
  }

  else if(debounceState == 3){                      //holding only
    if((millis() - debounceTime) > LONGPRESS) {          //long press
      debounceState = 3;
      debounceTime=millis();
      divSelect++;

      if(divSelect>3){
        divSelect=1;
      }
    }

    else if(digitalRead(BUTTON_PIN) == HIGH) {
      debounceState = 0;
    }
  }
  
  ////////////////////////////////////////////////////////////////////////////////////
  //       Read pot values                                                          //
  //       Store current reading to array, return avg over N samples                //
  ////////////////////////////////////////////////////////////////////////////////////

  float tempphasor;

  filterPut(POT, analogRead(POT_PIN));
  int raw_pot = filterGet(POT);
  
  //Serial.println(potValue);

  ////////////////////////////////////////////////////////////////////////////////////
  //       Read FREQ CV value                                                       //
  //       Store current reading to array, return avg over N samples                //
  ////////////////////////////////////////////////////////////////////////////////////

  // Read CV1 for frequency CV
  filterPut(FREQ, analogRead(CV1_PIN));
  int raw_cv = filterGet(FREQ);

  // Center CV value around 0
  int center_cv1 = raw_cv - 512;

  // Create dead zone in center of pot
  int center_pot = (1023 - raw_pot) - 512;
  if ((center_pot < 20) && (center_pot > -20)){
    center_pot = 0;
  }
  else {
    // Fix range for smooth movement outside of deadzone
    if (center_pot < 0) {
      center_pot = map(center_pot, 20, 1023, 0, 1023);
    }
    else{
      center_pot = map(center_pot, -1024, -20, -1024, 0);
    }   
  }

  ////////////////////////////////////////////////////////////////////////////////////
  //       Sum CV and POT values                                                    //
  //       This gives fuller control of usable range with cv                        //
  ////////////////////////////////////////////////////////////////////////////////////

  // Add CV and final pot reading
  int sum_val = (center_pot*2) + (center_cv1*2);

  // Clip value to keep in -1024 to 1023
  if (sum_val < -1024){
    sum_val = -1024;
  }
  else if (sum_val > 1023){
    sum_val = 1023;
  }

  ////////////////////////////////////////////////////////////////////////////////////
  //       Calculate Phasor sweep value                                             //
  //       SYNC MODE                                                                //
  ////////////////////////////////////////////////////////////////////////////////////

  // If in sync mode, set sweep speed based on clock mult/div
  if(Mode == 1) {
    
    // Value here can be between -1024 and 1023, with 13 bins
    
    // Available divisions and multiplications are:
    // 1/16 , 1/12, 1/8, 1/5, 1/4, 1/3, 1/2, 1, 2, 3, 4, 5, 8, 12, 16
    
    // This means that each range is ~136 values wide (extra will be allocated to the x1 zone)
    // If combined value is significantly negative, divide clock (slower than clock pulse)
    // Else if combined value is positive, multiply clock (faster than clock pulse)
    // Values in the middle will stay around 1

    if (sum_val < -888) {
      scalar = 16.0;
    }
    else if (sum_val < -752) {
      scalar = 12.0;
    }
    else if (sum_val < -616) {
      scalar = 8.0;
    }
    else if (sum_val < -480) {
      scalar = 5.0;
    }
    else if (sum_val < -344) {
      scalar = 4.0;
    }
    else if (sum_val < -208) {
      scalar = 3.0;
    }
    else if (sum_val < -72) {
      scalar = 2.0;
    }
    else if (sum_val < 71) {
      scalar = 1.0;
    }
    else if (sum_val < 207) {
      scalar = 0.5;
    }
    else if (sum_val < 343) {
      scalar = 0.33333;
    }
    else if (sum_val < 479) {
      scalar = 0.25;
    }
    else if (sum_val < 615) {
      scalar = 0.2;
    }
    else if (sum_val < 751) {
      scalar = 0.125;
    }
    else if (sum_val < 887) {
      scalar = 0.083333;
    }
    else {
      scalar = 0.0625;
    }
    
    // Multiply calculated freq times scalar to get multiplied / divided freq
    sweepValue = syncFrequency * scalar;
  }

  ////////////////////////////////////////////////////////////////////////////////////
  //       Calculate Phasor sweep value                                             //
  //       FREE MODE                                                                //
  ////////////////////////////////////////////////////////////////////////////////////

  // If not in sync mode, read speed from table hzcurve at location potValue + CV (index between 0 and 1023)
  else {
    // Compress -1024 to 1023 ranged and clipped value to 0-1023 to work in pgm_read
    int cv_plus_pot_map = map(sum_val, -1024, 1023, 1023, 0);
    
    // Get phasor sweep speed from table
    sweepValue=pgm_read_float_near(hzcurve + cv_plus_pot_map);
    
    // Force scalar to 1 to not break acc reset behavior
    scalar = 1.0;
  }

  ////////////////////////////////////////////////////////////////////////////////////
  //       Update LFO Phasor math                                                   //
  //       Calculate base phasor and use div values from table to scale freq        //
  ////////////////////////////////////////////////////////////////////////////////////

  tempphasor=sweepValue*HZPHASOR;

  basephasor = (unsigned long int) tempphasor;         // Cast to long form

  phasor1=basephasor/divs[divSelect-1][0];             // Set output freqs based on div down array
  phasor2=basephasor/divs[divSelect-1][1];             // dividing down for the slower outputs 
  phasor3=basephasor/divs[divSelect-1][2];
  phasor4=basephasor/divs[divSelect-1][3];
} // end main loop

// +++++++++++++++++++++++++++++++++++++++++ FUNCTION DEFINITIONS ++++++++++++++++++++++++++++++++++
// +++++++++++++++++++++++++++++++++++++++++ FUNCTION DEFINITIONS ++++++++++++++++++++++++++++++++++
// +++++++++++++++++++++++++++++++++++++++++ FUNCTION DEFINITIONS ++++++++++++++++++++++++++++++++++

unsigned long int previous_acc[4];

// Waveform generator functions
unsigned char generator(unsigned long int acc, char waveshape, char channel) {
  unsigned char shifted_acc = acc>>24;
  unsigned char ret_val = 0;
  bool multi_mode_toggle = false;

  // Sneaky trick to allow for waveshape mode changes on the fly
  if (waveshape == MULTIMODE){

    // set flag so we know we were in multimode
    multi_mode_toggle = true;

    // if its time to switch modes (once per cycle)
    if (shifted_acc < previous_acc[channel]) {
      randMode[channel] = random(MULTISTART, MULTIEND);
    }

    // set waveshape variable
    waveshape = randMode[channel];
  }

  // Draw waveshapes
  switch(waveshape) {

    // Triangle wave updated each pulse
    case TRIANGLE:
      if ((shifted_acc)<127) {
          ret_val = ((shifted_acc)<<1);
        }
      else if (shifted_acc == 127) {
        ret_val = 255;  
      }
      else { //greater than 127 (128-255)
        ret_val = ((255-shifted_acc)<<1);
      }
    
      break; // End triangle
  
    // Saw wave updated eachpulse
    case SAW:
      ret_val = (shifted_acc);
      
      break; // End saw

    // Saw wave updated eachpulse
    case RAMP:
      ret_val = (255-shifted_acc);
    
      break; // End ramp
  
    // Square wave updated each clock
    case SQUARE:
      if ((shifted_acc)>127) {
        ret_val = 255;
      }
      else {
        ret_val = 0;
      }
    
      break; // End square

    // Sample and hold style random updated each pulse
    case SAMPHOLD:
      if(shifted_acc < previous_acc[channel] ) {      // LESS THAN PREVIOUS NOT SURE HOW TO IMPLEMENT THIS 
        randNum[channel] = (char) random(256);
      }
    
      ret_val = randNum[channel]; 
    
      break; // End sample and hold

    // Spike waveform (rising and falling exponential "pulse trains"), good for spooky swells or rhythmic bursty noises
    // Spike is at begining/end of each cycle (NOT IN MIDDLE)
    case SPIKE:
      // Start with falling exponential
      // Scan backward at 2x speed (to accomidate full up down in one cycle) and invert
      if (shifted_acc < 128){
        ret_val = (255-(expRiseTable[255-shifted_acc*2 + 1]));
      }
      // Start rising again at halfway, scan w/ 2x+1 speed and offset and invert 
      else {
        ret_val = (255-(expRiseTable[(shifted_acc-128)*2 + 1]));
      }
    
      break; // End spike

    // Rising 16 step LFO (16 steps per clock)
    case RISESTEP:
      // Use ramp shape, but zero out last 4 bits
      ret_val = ((255 - shifted_acc) & 0xF0);

      break; // End rising steps

    // Random frequency squarewave w/ octave relationships
    case RANDSQROCT:
      // Roll a value between 0-7, this sets the target bit
      if (shifted_acc < previous_acc[channel]) {
        randNum[channel] = 0x01 << random(8);
      }

      // Generate square wave
      if (shifted_acc & randNum[channel]){
        ret_val = 0;
      }
      else{
        ret_val = 255;
      }
    
      break; // End RANDSQROCt

    // Random gates generated by coin toss calculated once per cycle, if heads, max voltage, if tails, min voltage
    case RANDGATE:
      // Roll a random number at begining of each cycle
      if (shifted_acc < previous_acc[channel]) {
        // On if heads (greater than 127), off if tails
        if (random(256) > 127) {
          randNum[channel] = 0;
        }
        else {
          randNum[channel] = 255;
        }
      }

      ret_val = (randNum[channel]); 
      
      break; // End rand gate

    // Random frequency squarewave w/ no octave rule
    // Can take on frequency multiplications of 1/2, 1/3, 1/4, 1/5 ... 1/127 (will always be more than 2x base freq)
    // I imagine this is the most expensive LFO to calculate due to the %, so if performance issues arise, remove this first
    case RANDSQR:
      // Roll a value between 2-128, this sets the target freq of the wave    
      if (shifted_acc < previous_acc[channel]) {
        randNum[channel] = random(2, 127);
      }

      // Generate square wave
      // 1) mod acc
      // 2) find midpoint of # samples per revolution (mult/2 implemented with shift)
      // 3) if below midpoint - go high, if above midpoint, go low
      if ((shifted_acc % randNum[channel]) > (randNum[channel] >> 1)){
        ret_val = 0;
      }
      else{
        ret_val = 255;
      }
    
      break; // end rand square

    case RANDTRIG:
    // At the start of each cycle...
      if(shifted_acc < previous_acc[channel]) {
        // Roll a 1 or a zero 
        char rando = (char) random(0, 2);
        // If one, set output high
        if (rando == 1){
          rando = 0;
        }
        // Otherwise, stay low
        else{
          rando = 255;
        }
        randNum[channel] = rando;
        lastPulse[channel] = micros();
      }
      // If output is still set high
      if (randNum[channel] == 0){
        // Check to see if total time since trigger open is larger then length
        if ((micros() - lastPulse[channel]) > TRIGLEN){
          // If so, set output low for remainder of cycle
          randNum[channel] = 255;       
        }        
      }
 
      ret_val = randNum[channel]; 
      break; // end rand trig
  }

  // Undo any weirdness created by multimode 
  if(multi_mode_toggle){
    waveshape = MULTIMODE;
  }

  // Update save point for calculations that take place once per cycle
  previous_acc[channel] = shifted_acc;
  
  // Return value
  return ret_val;

} // end generator function


// +++++++++++++++++++++++++++++++ ARDUINO FAST PWM FUNCTIONS/TIMERS ++++++++++++++++++++++++++++++++++
// +++++++++++++++++++++++++++++++ ARDUINO FAST PWM FUNCTIONS/TIMERS ++++++++++++++++++++++++++++++++++
// +++++++++++++++++++++++++++++++ ARDUINO FAST PWM FUNCTIONS/TIMERS ++++++++++++++++++++++++++++++++++

void setupTimers() {                                // used to set up fast PWM on pins 1,9,2,3 
  REG_GCLK_GENDIV = GCLK_GENDIV_DIV(2) |            //// Divide the 48MHz clock source by divisor N=1: 48MHz/1=48MHz
                    GCLK_GENDIV_ID(4);              //// Select Generic Clock (GCLK) 4
  
  while (GCLK->STATUS.bit.SYNCBUSY);                // Wait for synchronization

  REG_GCLK_GENCTRL = GCLK_GENCTRL_IDC |             // Set the duty cycle to 50/50 HIGH/LOW
                     GCLK_GENCTRL_GENEN |           // Enable GCLK4
                     GCLK_GENCTRL_SRC_DFLL48M |     // Set the 48MHz clock source
                     GCLK_GENCTRL_ID(4);            //// Select GCLK4
  
  while (GCLK->STATUS.bit.SYNCBUSY);                // Wait for synchronization

  // enable our 4 pins to be PWM outputs
  PORT->Group[g_APinDescription[OUT1_PIN].ulPort].PINCFG[g_APinDescription[OUT1_PIN].ulPin].bit.PMUXEN = 1;
  PORT->Group[g_APinDescription[OUT2_PIN].ulPort].PINCFG[g_APinDescription[OUT2_PIN].ulPin].bit.PMUXEN = 1;
  PORT->Group[g_APinDescription[OUT3_PIN].ulPort].PINCFG[g_APinDescription[OUT3_PIN].ulPin].bit.PMUXEN = 1;
  PORT->Group[g_APinDescription[OUT4_PIN].ulPort].PINCFG[g_APinDescription[OUT4_PIN].ulPin].bit.PMUXEN = 1;

  // assign the 4 outputs to the PWM registers on PMUX
  PORT->Group[g_APinDescription[OUT1_PIN].ulPort].PMUX[g_APinDescription[OUT1_PIN].ulPin >> 1].reg = PORT_PMUX_PMUXE_E;   // D3 is on PA11 = odd      
  PORT->Group[g_APinDescription[OUT2_PIN].ulPort].PMUX[g_APinDescription[OUT2_PIN].ulPin >> 1].reg |= PORT_PMUX_PMUXO_E;  // D11 is on PA08 = even 
  PORT->Group[g_APinDescription[OUT3_PIN].ulPort].PMUX[g_APinDescription[OUT3_PIN].ulPin >> 1].reg |= PORT_PMUX_PMUXE_F;  // D3 is on PA11 = odd
  PORT->Group[g_APinDescription[OUT4_PIN].ulPort].PMUX[g_APinDescription[OUT4_PIN].ulPin >> 1].reg |= PORT_PMUX_PMUXO_F;  // D11 is on PA08 = even

  // Feed GCLK4 to TCC0 and TCC1
  REG_GCLK_CLKCTRL = GCLK_CLKCTRL_CLKEN |           // Enable GCLK4 to TCC0 and TCC1
                     GCLK_CLKCTRL_GEN_GCLK0 |       // Select GCLK4 //0 only works for interrup?
                     GCLK_CLKCTRL_ID_TCC0_TCC1;     // Feed GCLK4 to TCC0 and TCC1
  
  while (GCLK->STATUS.bit.SYNCBUSY);                // Wait for synchronization

  // Dual slope PWM operation: timers countinuously count up to PER register value then down 0
  REG_TCC0_WAVE |= TCC_WAVE_POL(0xF) |              // Reverse the output polarity on all TCC0 outputs
                    TCC_WAVE_WAVEGEN_DSBOTH |
                    TCC_WAVE_WAVEGEN_NFRQ;          // Setup dual slope PWM on TCC0
  
  while (TCC0->SYNCBUSY.bit.WAVE);                  // Wait for synchronization

  // Each timer counts up to a maximum or TOP value set by the PER register,
  // this determines the frequency of the PWM operation: Freq = 48Mhz/(2*N*PER)
  REG_TCC0_PER = 0xFF;                              // Set the FreqTcc and period of the PWM on TCC1
  
  while (TCC0->SYNCBUSY.bit.PER);                   // Wait for synchronization
 
  REG_TCC0_CC1 = 10;                                // TCC1 CC1 - on D3  50% pin 9
  
  while (TCC0->SYNCBUSY.bit.CC1);                   // Wait for synchronization
  
  REG_TCC0_CC0 = 50;                                // TCC1 CC0 - on D11 50% pin 1
  
  while (TCC0->SYNCBUSY.bit.CC0);                   // Wait for synchronization
  
  REG_TCC0_CC2 = 200;                               // TCC1 CC1 - on D3  50% pin 2
  
  while (TCC0->SYNCBUSY.bit.CC2);                   // Wait for synchronization
  
  REG_TCC0_CC3 = 254;                               // TCC1 CC0 - on D11 50% pin 3
  
  while (TCC0->SYNCBUSY.bit.CC3);                   // Wait for synchronization
 
  // Divide the GCLOCK signal by 1 giving  in this case 48MHz (20.83ns) TCC1 timer tick and enable the outputs
  REG_TCC0_CTRLA |= TCC_CTRLA_PRESCALER_DIV2 |      // Divide GCLK4 by 1 ****************************************************************************
                    TCC_CTRLA_ENABLE;               // Enable the TCC0 output
  
  while (TCC0->SYNCBUSY.bit.ENABLE);                // Wait for synchronization

  TCC0->INTENSET.reg = 0;
  TCC0->INTENSET.bit.CNT = 1;  //*****************************************************
  TCC0->INTENSET.bit.MC0 = 0;

  NVIC_EnableIRQ(TCC0_IRQn);
  TCC0->CTRLA.reg |= TCC_CTRLA_ENABLE ;
} // end setupTimers

void TCC0_Handler() { // This doesn't appear to be called in the main code so how is it working??
  if (TCC0->INTFLAG.bit.CNT == 1) { //*************************************************
    accumulator1 = accumulator1 + phasor1;
    accumulator2 = accumulator2 + phasor2;
    accumulator3 = accumulator3 + phasor3;
    accumulator4 = accumulator4 + phasor4;
    
    delayMicroseconds(6);
    
    REG_TCC0_CC0 = generator(accumulator1, waveSelect, 3); // pin 9 //#4 // Pretty sure these comments are wrong, but not sure...
    REG_TCC0_CC1 = generator(accumulator4, waveSelect, 0); // pin 2 //#1
    REG_TCC0_CC2 = generator(accumulator2, waveSelect, 1); // pin 1 //#2  
    REG_TCC0_CC3 = generator(accumulator3, waveSelect, 2); // pin 3 //#3

    TCC0->INTFLAG.bit.CNT = 1; //*******************************************************
  }
} // End TCC0_Handler

// +++++++++++++++++++++++++++ FLASH/SAVE SETTINGS FUNCTIONS ++++++++++++++++++++++++++++++
// +++++++++++++++++++++++++++ FLASH/SAVE SETTINGS FUNCTIONS ++++++++++++++++++++++++++++++
// +++++++++++++++++++++++++++ FLASH/SAVE SETTINGS FUNCTIONS ++++++++++++++++++++++++++++++

// Read settings from flash
void readSettings (void)
{
  char x;
  char c = 'S';
  int y = 1;
  init_storage.read(x);
  if(x == 'S') { //S means eeprom has been initialized.
    wave_storage.read(waveSelect);
    div_storage.read(divSelect);
  }
  else {
    //we initialize, no 'S' found
    init_storage.write(c);  //use variables because this library hates constants
    wave_storage.write(y);
    div_storage.write(y);
    divSelect = 1;
    waveSelect = 1;
  }
} // end readSettings

// Write settings to Flash
void saveSettings (void) {
  int x;
  
  wave_storage.read(x);
  
  if(x != waveSelect) {
    wave_storage.write(waveSelect);  // <-- save the waveSelect 
  }
  
  div_storage.read(x);
  
  if(x != divSelect) {
    div_storage.write(divSelect);  // <-- save the divSelect  
  }
} // end saveSettings


// +++++++++++++++++++++++++++ ANALOG READ FUNCTIONS ++++++++++++++++++++++++++++++
// +++++++++++++++++++++++++++ ANALOG READ FUNCTIONS ++++++++++++++++++++++++++++++
// +++++++++++++++++++++++++++ ANALOG READ FUNCTIONS ++++++++++++++++++++++++++++++

#define NUMREADINGS 50
unsigned int pot[NUMREADINGS];
unsigned int freq[NUMREADINGS];

// Write analog values to array for filtering (to decrease value gitter)
void filterPut (char input, unsigned int newreading)
{
  static unsigned char potptr=0;
  static unsigned char freqptr = 0;

  if(input == POT) {
    pot[potptr] = newreading;
    potptr++;
    
    if(potptr >= NUMREADINGS) {
      potptr=0;
    }
  }

  else if(input == FREQ) {
    freq[freqptr] = newreading;
    freqptr++;
    
    if(freqptr >= NUMREADINGS)
      freqptr = 0;
  }
} // end filterPut

// Read avg value over last NUMREADINGS samples
unsigned int filterGet (bool input) {
  unsigned long int x;
  float z;
  unsigned char y;

  x = 0;
  if(input == POT) {
    for (y=0;y<NUMREADINGS;y++) {
      x = x + pot[y];
    }
  }
  else if(input == FREQ) {
    for (y=0;y<NUMREADINGS;y++) {
      x = x + freq[y];
    }
  }

  z = x;
  z = z/NUMREADINGS;
  z = z + 0.5;
  return (unsigned int)z;
} //end filterGet
