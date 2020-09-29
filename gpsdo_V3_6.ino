/*
  
  ONLY for 328 based Arduinos!!
  With modulo 50000
  With getcommand from JimH (replaces dipswitches)
  Added timer_us to ns TIC values to get much much larger capture range
  Changed PI-loop now uses float with remainder and not DI-term for P-term
  Changed average TIC+DAC storing to instant TIC+TNCT1 in hold mode
  Still EEPROM storage of 3hour averages and dac start value
  Added EEPROM storage of time constant and gain and many more parameters(see getcommand "help" below)
  
  Please check gain and timeConst and adapt to your used VCO. Holdmode is very useful to check range of VCO
  
  -Hardware:
  This version uses input capture on D8 !!! so jumper D2-D8 if you have an old shield with 1PPS to D2
  Uses 1 ns res/ 1 us TIC and internal PWM (2 x 8bits)
  1PPS to capture interrupt (D8)and HC4046 pin 14
  1MHz from HC390 (div2*5)to HC4046 pin 3
  5MHz from HC390 (div2)to timer1 (D5)
  1N5711+3.9k in series from HC4046 pin 15 to ADC0. 1nF NPO + 10M to ground on ADC0
  16bit PWM DAC with two-pole lowpass filter from D3 (39k+4.7uF+39k+4.7uF) and D11 (10M)
  Put a LED + resistor on D13 to show Lock status
  Optional temperature sensors on ADC2 (used for temperature compensation) and ADC1 (just indication)
  ADC3 can be read and used for whatever you want
  For UNO a recommendation is to connect one jumper from reset to a 10uf capacitor connected to ground.
  With this jumper shorted the Arduino UNO will not reset every time the serial monitor is started.
  For downloading a new program the jumper need to be taken away.
  
  *** paulv modifications V3:  
  * added interrupt driven decharge of C1 capacitor instead of 10M resistor
  * added optional software probe inside ISR to monitor what's going on
  * print **** instead of unLock to make it better standout
  * needed to swap temp sensors because of pcb layout error
  * changed the temperature labels in the report to a more meaningful OCXO and ambient
  * added a low-pass filter for the temperaturs
  * added a fan controller that uses the ambient temp sensor as input to drive a fan
  * 
  * My changes start with paulv: in comments so they are easy to locate
  * 
*/

#include <EEPROM.h>

// paulv: added a decharge for C1 after the ADC read cycle
const int decharge = 10; // Decharge pin for C1. Port is toggled between hi-Z input and output driven low
const int probe = 12; // create a sw probing signal within the ISR to help visualizing what is going on

// Fan controller
const int fan_pin = 6;    // Fan PWM output @ 490Hz : pin 9 does NOT work!
float ambient_t;          // ambient temperature
int duty_cycle;           // PWM value for the fan
float integral;           // used in PID function to save the previous value
float prev_error;         // used in the PID function to store the previous error
int cool_baseline = 36;   // start cooling from this temp in Celcius onwards.
int pwm_baseline = 40;    // low PWM to keep the fan running and baseline
int max_pwm = 255;        // maximum PWM value
// gain factors. They work but could use a bit of further tuning depending on your hardware
float Kp = 10;
float Ki = 0.6;
float Kd = 10;


int warmUpTime = 300; // gives five minutes hold during eg OCXO or Rb warmup. Set to eg 3 for VCTCXO
long dacValueOut = 32768; // 16bit PWM-DAC setvalue=startvalue Max 65535 (if nothing stored in the EEPROM)
long dacValue; // this is also same as "DACvalueOld" Note: is "0-65535" * timeconst
long dacValue2; // had to add this for calculation in PI-loop
long dacValueWithTempComp; // Note: is "0-65535" * timeconst
const int PWMhighPin = 3; // high PWM-DAC pin (connected to 39k)
const int PWMlowPin = 11; // low PWM-DAC pin (connected to 10M)
int valuePWMhigh; // high PWM-DAC byte
int valuePWMlow; // low PWM-DAC byte

volatile int TIC_Value; // analog read 0  - time value. About 1ns per bit with 3.7kohm+1nF
int TIC_ValueOld;//old not filtered TIC_value
int TIC_Offset = 500; // ADC value for Reference time
long TIC_ValueFiltered; // prefiltered TIC value
long TIC_ValueFilteredOld;// old filtered value
long TIC_ValueFilteredForPPS_lock; // prefiltered value just for PPS lock

volatile unsigned int timer1CounterValue; //counts 5MHz clock modulo 50000
long timer1CounterValueOld = 0 ;
unsigned int TCNT1new = 0;//in main loop
unsigned int TCNT1old = 0;
unsigned long overflowCount = 0; // counter for timer1 overflows
long timer_us; // timer1 value in microseconds offset from 1pps
long timer_us_old; // used for diff_ns
long diff_ns; // difference between old and new TIC_Value
long diff_ns_ForPPS_lock; // prefiltered value just for PPS lock

long tempADC1; // paulv: changed to long, analog read 1  - for example for internal NTC if oscillator external to Arduino
long tempADC2; // analog read 2  - for oscillator temp eg LM35 or NTC - used for temperature compensation
long tempADC2_Filtered; // analog read 2  - temp filtered for temp correction
long tempCoeff = 0; // 650 = 1x10-11/°C if 1°C= +10bit temp and 65dac bits is 1x10-11 // set to -dacbits/tempbits*100?
long tempRef = 280; // offset for temp correction calculation - 280 is about 30C for LM35 (set for value at normal room temperature)
unsigned int temperature_Sensor_Type = 0; //

// paulv: IIR filter for the temperature sensors
// avg = avg + ((new_value - avg) / filter_weight)
long tempADC1_IIR;
long tempADC2_IIR;
int IIR_Filter_Weight = 3;  // IIC filter weight value


long timeConst = 32; // Time constant in seconds
long timeConstOld = 32; // old Time constant
int filterDiv = 2; // filterConst = timeConst / filterDiv
long filterConst = 16; // pre-filter time const in secs (TIC-filtering)
long filterConstOld = 16; // old Filter time constant

float I_term; //for PI-loop
float P_term;
long I_term_long;
float I_term_remain;

long gain = 12; //VCO freq DAC bits per TIC bit (65536/VCOrange in ppb (eg. with 1nS/bit and 100ppb DACrange gives gain=655))
float damping = 3.0; //Damping in loop

unsigned long time; //seconds since start
unsigned long timeOld; //last seconds since start
unsigned int missedPPS; // incremented every time pps is missed
unsigned long timeSinceMissedPPS;
volatile boolean PPS_ReadFlag = false; // set true every time pps is received
int lockPPSlimit = 100; // if TIC filtered for PPS within +- this for lockPSfactor * timeConst = PPSlocked
int lockPPSfactor = 5;  // see above
unsigned long lockPPScounter; // counter for PPSlocked
boolean PPSlocked; //digital pin and prints 0 or 1
const int ppsLockedLED = 13; // LED pin for pps locked

int i; // counter for 300secs before storing temp and dac readings average
int j; // counter for stored 300sec readings
int k; // counter for stored 3hour readings
unsigned int StoreTIC_A[144]; //300sec storage
unsigned int StoreTempA[144];
unsigned int StoreDAC_A[144];
long sumTIC;
long sumTIC2;
long sumTemp;
long sumTemp2;
unsigned long sumDAC; 
unsigned long sumDAC2;

unsigned int totalTime3h; // counter for power-up time updated every third hour
unsigned int restarts; // counter for restarts/power-ups
boolean restartFlag = true;

unsigned int ID_Number;

boolean lessInfoDisplayed;
boolean nsDisplayedDecimals;

// for get command
int incomingByte;   // for incoming serial data in getCommand
enum Modes {hold, run};
Modes opMode = run;     //operating mode
Modes newMode = hold;   // used to reset timer_us when run is set and at to many missing PPS
unsigned int holdValue; //DAC value for Hold mode

// for TIC linearization
float TICmin = 12.0;
float TICmax = 1012.0;
float x3 = 0.03;
float x2 = 0.1;
float x1;
float TIC_Scaled;
float TIC_ValueCorr;
float TIC_ValueCorrOld;
float TIC_ValueCorrOffset;


////////////////////////////////////////////////////////////////////////////////////////////////
// Timer1 capture interrupt routine - this runs at rising edge of 1PPS on D8
// paulv: **** modified to add a digital decharge of C1
ISR (TIMER1_CAPT_vect)
{
  timer1CounterValue = ICR1;    // read the captured timer1 200ns counter value
//  digitalWrite(probe, 1);     // *** paulv create a signal to probe the ADC cycle with a scope
  TIC_Value = analogRead(A0);   // ns value
//  digitalWrite(probe, 0);     // *** paulv stop the probe
  pinMode(decharge,OUTPUT);     // *** paulv change from hi-Z to output
  digitalWrite(decharge, 0);    // *** paulv bring it low to start de-charging of C1
  PPS_ReadFlag = true;
  delayMicroseconds(20);        // *** paulv allow sufficient time to fully drain the capacitor
  pinMode(decharge,INPUT);      // *** paulv revert the port back to hi-Z
}


////////////////////////////////////////////////////////////////////////////////////////////////  
void calculation()
{
// set timer1 start value in the beginning  
  if (time < 2 || (time > warmUpTime-2 && time < warmUpTime))
    {
      TCNT1 = 25570 ; // is guessed value to get around 25000 next time
    }
  
// TIC linearization
  //x2 = (((TICmid-TICmin)/(TICmax-TICmin)*1000)- 500.0)/250.0 - 0.05; // just for info
  x1 = 1.0 - x3 - x2;
  TIC_Scaled = ((float)TIC_Offset - TICmin)/(TICmax - TICmin)*1000; // Scaling for TIC_Offset
  TIC_ValueCorrOffset = TIC_Scaled * x1 + TIC_Scaled * TIC_Scaled * x2 / 1000.0 + TIC_Scaled * TIC_Scaled * TIC_Scaled * x3 /1000000.0;
  
  TIC_Scaled = ((float)TIC_Value - TICmin)/(TICmax - TICmin)*1000; // Scaling for TIC_Value
  TIC_ValueCorr = TIC_Scaled * x1 + TIC_Scaled * TIC_Scaled * x2 / 1000.0 + TIC_Scaled * TIC_Scaled * TIC_Scaled * x3 /1000000.0;
  
// timer_us 
  timer_us = timer_us + 50000 - (((timer1CounterValue - timer1CounterValueOld) * 200 + TIC_Value - TIC_ValueOld)+50000500)/1000;

  if (newMode == run)     // reset timer_us if change from hold mode to run mode
    {
      timer_us = 0 ;
      timer_us_old = 0 ;
      TIC_ValueFilteredOld = TIC_Offset * filterConst;
      newMode = hold;
    }
    
  if (time < 3 || (time > warmUpTime-1 && time < warmUpTime+1)) // reset in the beginning and end of warmup
    {
      timer_us = 0 ;
    } 

  if ((abs(timer_us)- 2) > timeConst * 65536 / gain / 1000 && opMode == run && time > warmUpTime )
    {
      timer_us = 0 ;
      timer_us_old = 0 ;
      TIC_ValueFilteredOld = TIC_Offset * filterConst;
    }
    
  if (TIC_ValueOld == 1023)     // reset if 10MHz was missing
    {
      timer_us = 0 ;
      timer_us_old = 0 ;
      TIC_ValueFilteredOld = TIC_Offset * filterConst;
    } 

    
// Diff_ns
  if (TIC_ValueCorr > TIC_ValueCorrOld)
    {
    diff_ns = (timer_us - timer_us_old)*1000 + long (TIC_ValueCorr - TIC_ValueCorrOld + 0.5); // = Frequency in ppb if updated every second! Note: TIC linearized
    }
    else
      {
      diff_ns = (timer_us - timer_us_old)*1000 + long (TIC_ValueCorr - TIC_ValueCorrOld -0.5); 
      }
// time - is supposed to be approximately seconds since start
  time = time + (overflowCount + 50)/100;
  overflowCount = 0;
  
// missedPPS
  if (time - timeOld > 1) 
    {
      missedPPS = missedPPS + 1;
      timeSinceMissedPPS = 0;
    }
     else 
       {
       timeSinceMissedPPS = timeSinceMissedPPS + 1;
       }

////// PPS locked

 // Low Pass Filter of TIC_Value for PPS lock  // /16 is used as 500ns error and /16 is about 30ns that seems reasonable
  TIC_ValueFilteredForPPS_lock = TIC_ValueFilteredForPPS_lock + (TIC_Value * 16 - TIC_ValueFilteredForPPS_lock) / 16;
                                                                             
 // Low Pass Filter of diff_ns for PPS lock                                                                             
  diff_ns_ForPPS_lock = diff_ns_ForPPS_lock + (diff_ns * 16 - diff_ns_ForPPS_lock) / 16;
  
  lockPPScounter = lockPPScounter + 1;
  
  if (abs(TIC_ValueFilteredForPPS_lock / 16 - TIC_Offset) > lockPPSlimit)
    {
      lockPPScounter = 0;
    }
  if (abs(diff_ns_ForPPS_lock/16) > 20)// if freq more than 20ppb wrong (had to add this to avoid certain combinations not covered by above)
    {
      lockPPScounter = 0;
    }
  
  if (lockPPScounter > timeConst * lockPPSfactor)
     {
      PPSlocked = 1;
     } 
     else
     {
      PPSlocked = 0;
     }
 
 // turn on LED 13 if "locked"    
  digitalWrite(ppsLockedLED,PPSlocked);
  
//////
 
// read ADC1 and 2 - temperature 
// ***** paulv: swap the two sensors due to PCB layout error
// ***** below is the original code
//  int dummyreadADC = analogRead(A1); //without this ADC1 is influenced by ADC0
//  tempADC1 = analogRead(A1);
//  dummyreadADC = analogRead(A2); //without this ADC2 is influenced by ADC1
//  tempADC2 = analogRead(A2);
//  dummyreadADC = analogRead(A0); //without this TIC_Value (ADC0) is influenced by ADC2
//
// and here the fixed code swapping A1 with A2
  int dummyreadADC = analogRead(A2); //without this ADC1 is influenced by ADC0
  tempADC1 = analogRead(A2);
  dummyreadADC = analogRead(A1); //without this ADC2 is influenced by ADC1
  tempADC2 = analogRead(A1);
  dummyreadADC = analogRead(A0); //without this TIC_Value (ADC0) is influenced by ADC2

  
// set filter constant
  filterConst = timeConst / filterDiv;
  filterConst = constrain (filterConst, 1,1024);
  if (PPSlocked == 0 || opMode == hold) filterConst = 1;
  
// recalculation of value  
  if(timeConst != timeConstOld)
    {
  dacValue = dacValue / timeConstOld * timeConst;  
    }
    
  if(filterConst != filterConstOld)
    {
  TIC_ValueFilteredOld = TIC_ValueFilteredOld / filterConstOld * filterConst;
  TIC_ValueFiltered = TIC_ValueFiltered / filterConstOld * filterConst;
    }
     
// Low Pass Filter for TICvalue (Phase Error)
// Remember that TIC_ValueFiltered is multiplied by filterConst
  
  // Don´t update if outlier. Accepts diff_ns less than same ns as vco range in ppb + 200ns
  if abs(diff_ns <6500)// First check to avoid overflow in next calculation (also max VCO range is about 6500ns/s)
  {
  if( abs(diff_ns * gain) < (65535 + 200 * gain))
  {
  TIC_ValueFiltered = TIC_ValueFiltered + ((timer_us*1000 + TIC_Value) * filterConst - TIC_ValueFiltered + (filterConst/2)) / filterConst; 
  }
  }
   
  if (time > warmUpTime && opMode == run) // Don't change DAC-value during warm-up time or if in hold mode
   {
////// PI-loop //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
     P_term = (TIC_ValueFiltered - TIC_Offset * filterConst) / float(filterConst) * gain; // remember /timeConst is done before dacValue is sent out
     I_term = P_term / damping / float(timeConst)  + I_term_remain; 
     I_term_long = long(I_term); 
     I_term_remain = I_term - I_term_long;
     dacValue += I_term_long;
     dacValue2 = dacValue + P_term;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
   }
   else (dacValue2 = dacValue); // No change

  // Low Pass Filter for temperature
  tempADC2_Filtered = tempADC2_Filtered + (tempADC2 * 100 - tempADC2_Filtered) / 100;
    
  // Temperature correction for DAC value
  dacValueWithTempComp = dacValue2 + ((tempRef * 100 - tempADC2_Filtered) * tempCoeff / 10000 * timeConst); 

  // Check that dacvalue is within limits
  if (dacValue < 0) 
   { dacValue = 0;}
  if (dacValue > (65535 * timeConst))
   { dacValue = (65535 * timeConst);}
  
  dacValueOut = dacValueWithTempComp / timeConst; // PWM-DAC value
  if (dacValueOut < 0)   
   { dacValueOut = 0;}
  if (dacValueOut > 65535)
   { dacValueOut = 65535;}

  // manual set of dacvalue if in hold and not 0, if zero hold old value
  if (holdValue > 0 && opMode == hold) 
   {dacValueOut = holdValue ;}

  // Set "16bit DAC" 
  valuePWMhigh = highByte(dacValueOut);
  valuePWMlow = lowByte(dacValueOut);
  analogWrite(PWMhighPin,valuePWMhigh);
  analogWrite(PWMlowPin,valuePWMlow);

// Increment restart at time 100 (100 chosen arbitrary)
if (time > 100 && restartFlag == true)
  { 
  restarts = restarts + 1;
  EEPROM.write(991, highByte(restarts));
  EEPROM.write(992, lowByte(restarts));
  restartFlag = false;
  }
  
//////////////////////////////////////////////////////////////////////////////////////
//Storage of average readings that is later printed

  
  sumTIC = sumTIC + (TIC_Value * 10);
  sumTemp = sumTemp + (tempADC2 * 10);
  sumDAC = sumDAC + dacValueOut;
  i = i + 1;
  if (i == 300) // 300sec
  {
    if (opMode == run) 
       {StoreTIC_A[j]= sumTIC / i;}
    else
       {StoreTIC_A[j]= TIC_Value;}
     
    sumTIC2 = sumTIC2 + sumTIC / i;
    sumTIC = 0;
    StoreTempA[j]= sumTemp / i;
    sumTemp2 = sumTemp2 + sumTemp / i;
    sumTemp = 0;
    if (opMode == run) 
       {StoreDAC_A[j]= sumDAC / i;}
    else
       {StoreDAC_A[j]= (49999 - timer1CounterValue);}
       
    sumDAC2 = sumDAC2 + sumDAC / i;
    sumDAC = 0;
    i = 0;
    j = j + 1;
    if (j % 36 == 0) // store every 36 x 300sec (3 hours)
    {
      sumTIC2 = sumTIC2 / 36;
      if (opMode == run) 
      {
      EEPROM.write(k, highByte(sumTIC2));
      EEPROM.write(k+144, lowByte(sumTIC2));
      }
      else
      {
      EEPROM.write(k, highByte(TIC_Value));
      EEPROM.write(k+144, lowByte(TIC_Value));
      }
      sumTIC2 = 0;
      
      sumTemp2 = sumTemp2 / 36;
      if (opMode == run) 
      {
        sumTemp2 = sumTemp2 + 20480 ;
        if (lockPPScounter > 10800)
        {
        sumTemp2 = sumTemp2 + 20480 ;
        }
      }
      
      if (time < 20000) // first after start
      {
        sumTemp2 = sumTemp2 + 10240 ;
      }
        
      EEPROM.write(k+576, highByte(sumTemp2));
      EEPROM.write(k+720, lowByte(sumTemp2));
      sumTemp2 = 0;
      
      sumDAC2 = sumDAC2 / 36;
      
      if (opMode == run) 
      {
      EEPROM.write(k+288, highByte(sumDAC2));
      EEPROM.write(k+432, lowByte(sumDAC2));
      }
      else
      {
      EEPROM.write(k+288, highByte(49999 - timer1CounterValue));
      EEPROM.write(k+432, lowByte(49999 - timer1CounterValue));
      }
      
      if (opMode == run && lockPPScounter > 10800) 
      {
      EEPROM.write(1017, highByte(sumDAC2));
      EEPROM.write(1018, lowByte(sumDAC2));
      }
      
      sumDAC2 = 0;
      
      if (j == 144) // 144 x 300sec (12 hours)
        {
        j = 0;
        }
      k = k + 1;
      if (k == 144) // 144 x 10800sec (18 days)
      {
        k = 0;
      }
      EEPROM.write(1023, k); // store present k (index of 3 hour average, used in setup)
      
      totalTime3h = totalTime3h + 1;
      EEPROM.write(993, highByte(totalTime3h));
      EEPROM.write(994, lowByte(totalTime3h));
      
     
    }
  }

// storage of old parameters
  timer1CounterValueOld = timer1CounterValue;
  TIC_ValueOld = TIC_Value;  
  TIC_ValueCorrOld = TIC_ValueCorr;  
  timer_us_old = timer_us; 
  timeConstOld = timeConst;
  filterConstOld = filterConst; 
  timeOld = time;
  TIC_ValueFilteredOld = TIC_ValueFiltered;

}

////////////////////////////////////////////////////////////////////////////////////////////////
void getCommand()
{
  char ch;
  long zz; // *** paulv Lars used "z" as the name, but I wanted to use it. Sorry Lars
  
  enum Command {                      // this is the command set
    a = 'a', A = 'A',                 // set damping
    b = 'b', B = 'B',                 // set reference temperature 
    c = 'c', C = 'C',                 // set temperature coefficient
    d = 'd', D = 'D',                 // set dacvalue (followed by a value)
    e = 'e', E = 'E',                 // erase (followed by a value)
    f = 'f', F = 'F',                 // help (followed by a value)
    g = 'g', G = 'G',                 // gain (followed by new value)
    h = 'h', H = 'H',                 // hold (followed by a DAC value note: 0 will set only hold)
    i = 'i', I = 'I',                 // toggles less or more info
    j = 'j', J = 'J',                 // set temperature sensor type
//  k
    l = 'l', L = 'L',                 // set TIC linearization parameters min max square
//  m
    n = 'n', N = 'N',                 // set ID number
    o = 'o', O = 'O',                 // TIC_Offset (followed by new value)
    p = 'p', P = 'P',                 // set prefilter div
//  q
    r = 'r', R = 'R',                 // run
    s = 's', S = 'S',                 // save (followed by a value)
    t = 't', T = 'T',                 // time const (followed by new value)
    // *** paulv 
    u = 'u', U = 'U',                 // mimimum PWM setting for the fan
    v = 'v', V = 'V',                 // Ambient set temp for fan regulation
    
    w = 'w', W = 'W',                 // set warmup time (to allow for warm up of oscillator)
    // *** paulv PID gain settings
    x = 'x', X = 'X',                 // Kp gain
    y = 'y', Y = 'Y',                 // Ki gain
    z = 'z', Z = 'Z'                  // Kd gain
   
  }; 
  
  if (Serial.available() > 0) {       //process if something is there
    ch = Serial.read();
    
    switch(ch) {
   	
      case a:				    // set damping command
      case A:
        zz = Serial.parseInt();  //needs new line or carriage return set in Arduino serial monitor
        if (zz >=50 && zz <= 1000)
          {
            damping = zz / 100.0; 
            Serial.print(F("Damping "));
            Serial.println(damping);
          }
          else { Serial.println(F("Not a valid damping value - Shall be between 50 and 1000"));}
 	break;	
            
         	
      case b:				    // set temperature offset command
      case B:
        zz = Serial.parseInt();
        if (zz >=1 && zz <= 1023)
          {
            tempRef = zz; 
            Serial.print(F("Temperature offset "));
            Serial.println(tempRef);
          }
          else { Serial.println(F("Not a valid temperature offset value - Shall be between 1 and 1023"));}
 	break;	
      
         	
      case c:				    // Set temperature coefficient
      case C:
        zz = Serial.parseInt();
        if (zz >=0 && zz <= 10000)
          {
            tempCoeff = zz; 
            Serial.print(F("Temperature Coefficient "));
            Serial.println(tempCoeff);
          }
          
          else if (zz >=10001 && zz <= 20000)
            {
            tempCoeff = (10000-zz);
            Serial.print(F("Temperature Coefficient "));
            Serial.println(tempCoeff);
      	    }
          
          else { Serial.println(F("Not a valid temperature coefficient value - Shall be between 0 and 20000"));}
 	break;
 	
      
      case d:				// set dacValue command
      case D:
        zz = Serial.parseInt();
        if (zz >=1 && zz <= 65535)
          {
            dacValue = zz * timeConst; 
            Serial.print(F("dacValue "));
            Serial.println(zz);
          }
          else { Serial.println(F("Not a valid dacValue - Shall be between 1 and 65535"));}
 	break;
 	
  
      case e:				// erase command
      case E:  
        zz = Serial.parseInt();      
        switch (zz) {
          
          case 1:
          Serial.println(F("Erase 3h storage in EEPROM "));	
          for (int i = 0; i < 864; i++)
            {EEPROM.write(i, 0);}          
          EEPROM.write(1023, 0);
          k = 0 ; //reset 3hours counter
      	  break;
          
          case 22:          
          Serial.println(F("Erase all EEPROM to zero"));	
          for (int i = 0; i < 1024; i++)
            {EEPROM.write(i, 0);}
          k = 0 ; //reset 3hours counter
          break;
          
          case 33:          
          Serial.println(F("Erase all EEPROM to -1"));	
          for (int i = 0; i < 1024; i++)
            {EEPROM.write(i, 255);}
          k = 0 ; //reset 3hours counter
          break;
                       
          default:
          Serial.println(F("Not a valid value for erase - Shall be 1 or 22"));
          }          
          
      break;	
  
      case f:				// help command
      case F:      
        zz = Serial.parseInt();
        
        switch (zz) {
          
          case 1:          
          Serial.println("");
          Serial.println(F("Info and help - To get values for gain etc type f2 <enter>, f3 <enter> reads ADC3 and f4 <enter> EEPROM"));	
          printHeader1_ToSerial();
          Serial.print("\t");
          printHeader2_ToSerial();
          Serial.println("");
          Serial.println("");
          Serial.println(F("Typing a<value><enter> will set a new damping between between 0.50 and 10.00 set 50 to 1000"));
          Serial.println(F("Typing b<value><enter> will set a new tempRef between 1 and 1023"));
          Serial.println(F("Typing c<value><enter> will set a new tempCoeff set between 0 and 10000. Adding 10000 gives negative tc"));
          Serial.println(F("Typing d<value><enter> will set a new dacValue between 1 and 65535"));
          Serial.println(F("Typing e<value><enter> will erase the 3 hour storage in EEPROM if value 1 and all EEPROM if 22 (33 sets all EEPROM to FF)"));
          Serial.println(F("Typing g<value><enter> will set a new gain between 10 and 65535")); 
          Serial.println(F("  gain = (65536/settable VCOrange in ppb) (eg. 100ppb DACrange gives gain=655)"));
          Serial.println(F("Typing h<value><enter> will set hold mode and the entered dacValue if not h0 that uses the old"));
          Serial.println(F("Typing i<value><enter> with value 1 will toggle ns decimal point else will toggle amount of information "));
          Serial.println(F("Typing j<value><enter> Set temp sensor type 0=raw 1=LM35 2=10kNTC+68k 3=10kNTC+47k (second digit=adc1 eg 3x)"));
          Serial.println(F("Typing l<enter> will set TIC linearization parameters min max square"));
          Serial.println(F("  values 1-500 sets min to 0.1-50, values 800-1023 sets max, values 1024-1200 sets square to 0.024-0.200"));
          Serial.println(F("Typing n<value><enter> will set ID number 0-65535 that is displayed "));
          Serial.println(F("Typing o<value><enter> will set a new TIC_Offset between 10 and 1020 ns"));
          Serial.println(F("Typing p<value><enter> will set a new prefilter div between 2 and 4"));
          Serial.println(F("Typing r<enter> will set run mode"));
          Serial.println(F("Typing s<value><enter> will save gain etc to EEPROM if value 1 and dacvalue if 2"));
          Serial.println(F("Typing t<value><enter> will set a new time constant between 4 and 32000 seconds"));
          Serial.println(F("Typing u<value><enter> will set a new minimum PWM for the fan"));          
          Serial.println(F("Typing v<value><enter> will set a new max ambient temp for the fan in degrees celsius"));
          Serial.println(F("Typing w<value><enter> will set a new warmup time between 2 and 1000 seconds"));
          Serial.println(F("Typing x<value><enter> will set a new Kp gain x 10 for the PID fan regulation"));
          Serial.println(F("Typing y<value><enter> will set a new Ki gain x 10 for the PID fan regulation"));
          Serial.println(F("Typing z<value><enter> will set a new Kd gain x 10 for the PID fan regulation"));          
          Serial.println("");
          printHeader3_ToSerial();
          break;
         
          case 2:
          Serial.println("");
          Serial.print(F("Gain    ")); Serial.print("\t"); Serial.print(gain); Serial.print("\t");                   
          Serial.print(F("Damping ")); Serial.print("\t"); Serial.print(damping); Serial.print("\t");
          Serial.print(F("TimeConst "));  Serial.print("\t"); Serial.print(timeConst); Serial.print("\t");
          Serial.print(F("FilterDiv "));  Serial.print("\t"); Serial.print(filterDiv); Serial.print("\t");
          Serial.print(F("TIC_Offset "));  Serial.print("\t"); Serial.println(TIC_Offset);
          Serial.print(F("TempRef "));  Serial.print("\t"); Serial.print(tempRef); Serial.print("\t");
          Serial.print(F("TempCoeff "));  Serial.print("\t"); Serial.print(tempCoeff); Serial.print("\t");
          Serial.print(F("TICmin  "));  Serial.print("\t"); Serial.print(TICmin,1); Serial.print("\t");
          Serial.print(F("TICmax  "));  Serial.print("\t"); Serial.print(TICmax,0); Serial.print("\t");
          Serial.print(F("Square comp "));  Serial.print("\t"); Serial.println(x2,3); 
          Serial.print(F("Warm up time "));  Serial.print("\t"); Serial.print(warmUpTime); Serial.print("\t");
          Serial.print(F("LockPPScounter "));  Serial.print("\t"); Serial.print(lockPPScounter); Serial.print("\t");
          Serial.print(F("MissedPPS "));  Serial.print("\t"); Serial.print(missedPPS); Serial.print("\t");
          Serial.print(F("TimeSinceMissedPPS  ")); Serial.println(timeSinceMissedPPS);           
          Serial.print(F("ID_Number  "));  Serial.print("\t"); Serial.print(ID_Number); Serial.print("\t");
          Serial.print(F("Restarts  "));  Serial.print("\t"); Serial.print(restarts); Serial.print("\t");
          Serial.print(F("Total hours"));  Serial.print("\t"); Serial.println(totalTime3h * 3);
          // *** paulv
          Serial.print(F("PWM Baseline ")); Serial.print("\t"); Serial.print(pwm_baseline); Serial.print("\t");
          Serial.print(F("Cool Baseline ")); Serial.print("\t"); Serial.println(cool_baseline);
          Serial.print(F("Kp ")); Serial.print("\t"); Serial.print(Kp); Serial.print("\t");
          Serial.print(F("Ki ")); Serial.print("\t"); Serial.print(Ki); Serial.print("\t");         
          Serial.print(F("Kd ")); Serial.print("\t"); Serial.println(Kd);
                                
          Serial.println("");    
          printHeader3_ToSerial();
          break;
          
          case 3:
          Serial.println ("");
          Serial.print (F("ADC3 = "));
          Serial.println(analogRead(A3));
          Serial.println ("");
          break;
          
          case 4:      
          Serial.println ("");
          Serial.println (F("EEPROM content: "));
          Serial.print (F("restarts = "));
          zz=(EEPROM.read(991)*256 + EEPROM.read(992)); Serial.println((unsigned int)z); 
          Serial.print (F("totalTime3h = "));
          zz=(EEPROM.read(993)*256 + EEPROM.read(994)); Serial.println((unsigned int)z); 
          Serial.print (F("temperature_Sensor_Type =  "));
          zz=(EEPROM.read(995)*256 + EEPROM.read(996)); Serial.println((unsigned int)z); 
          Serial.print (F("ID_Number = "));
          zz=(EEPROM.read(997)*256 + EEPROM.read(998)); Serial.println((unsigned int)z); 
          Serial.print (F("TICmin = "));
          zz=(EEPROM.read(999)*256 + EEPROM.read(1000)); Serial.println((unsigned int)z); 
          Serial.print (F("TICmax = "));
          zz=(EEPROM.read(1001)*256 + EEPROM.read(1002)); Serial.println((unsigned int)z); 
          Serial.print (F("x2 = "));
          zz=(EEPROM.read(1003)*256 + EEPROM.read(1004)); Serial.println((unsigned int)z); 
          Serial.print (F("TIC_Offset = "));
          zz=(EEPROM.read(1005)*256 + EEPROM.read(1006)); Serial.println((unsigned int)z); 
          Serial.print (F("filterDiv = "));
          zz=(EEPROM.read(1007)*256 + EEPROM.read(1008)); Serial.println((unsigned int)z); 
          Serial.print (F("warmUpTime = "));
          zz=(EEPROM.read(1009)*256 + EEPROM.read(1010)); Serial.println((unsigned int)z); 
          Serial.print (F("damping = "));
          zz=(EEPROM.read(1011)*256 + EEPROM.read(1012)); Serial.println((unsigned int)z); 
          Serial.print (F("tempRef = "));
          zz=(EEPROM.read(1013)*256 + EEPROM.read(1014)); Serial.println((unsigned int)z); 
          Serial.print (F("tempCoeff = "));
          zz=(EEPROM.read(1015)*256 + EEPROM.read(1016)); Serial.println((unsigned int)z);        
          Serial.print (F("dacValueOut = "));
          zz=(EEPROM.read(1017)*256 + EEPROM.read(1018)); Serial.println((unsigned int)z); 
          Serial.print (F("gain = "));
          zz=(EEPROM.read(1019)*256 + EEPROM.read(1020)); Serial.println((unsigned int)z); 
          Serial.print (F("timeConst = "));
          zz=(EEPROM.read(1021)*256 + EEPROM.read(1022)); Serial.println((unsigned int)z); 
          Serial.print (F("k = "));
          Serial.println (EEPROM.read(1023));         
          Serial.println ("");                    
          break;
          
          default:
          Serial.println(F("Not a valid value for help - Shall be 1 to 4"));    
          }
      	break;
       
      
      
      case g:				    // gain command
      case G:
        zz = Serial.parseInt();
        if (zz >=10 && z <= 65534)
          {
            gain = zz; 
            Serial.print(F("Gain "));
            Serial.println(zz);
          }
          else { Serial.println(F("Not a valid gain value - Shall be between 10 and 65534"));}
 	break;	
      
      case h:                              // hold command
      case H:
        zz = Serial.parseInt();
        if (zz >=0 && zz <= 65535)
          {
          opMode = hold;
          newMode = hold;
          Serial.print(F("Hold "));
          holdValue = zz ; 
          Serial.println(holdValue);
          }
          else { Serial.println(F("Not a valid holdValue - Shall be between 0 and 65535"));}         
          
        break;
        
      case i:				   // help command
      case I:      
        zz = Serial.parseInt();
        if (zz == 1)
          { nsDisplayedDecimals = !nsDisplayedDecimals ; }
          else
           { lessInfoDisplayed = !lessInfoDisplayed ; }
      	break;

      case j:				    // temperature_Sensor_Type       
      case J:
        zz = Serial.parseInt();
        if (zz >=0 && zz <= 99)
          {
            temperature_Sensor_Type = zz; 
            Serial.print(F("temperature_Sensor_Type "));
            Serial.println(zz);
          }
          else { Serial.println(F("Not a valid temperature_Sensor_Type value - Shall be between 0 and 99"));}
 	break;	
            
      case l:				    // set TIC linearization parameters command
      case L:
        zz = Serial.parseInt();
        if (zz >=1 && zz <= 500)
          {
            TICmin = zz / 10.0; 
            Serial.print(F("TICmin "));
            Serial.println(TICmin);
          }
            else if (zz >=800 && zz <= 1023)
          {
            TICmax = zz; 
            Serial.print(F("TICmax "));
            Serial.println(TICmax);
          }
            else if (zz >=1024 && zz <= 1200)
          {
            x2 = (zz -1000 ) / 1000.0; 
            Serial.print(F("square compensation "));
            Serial.println(x2);
          }
          else { Serial.println(F("Not a valid value"));}
 	break;	
      
      case n:				    // ID_number      
      case N:
        zz = Serial.parseInt();
        if (zz >=0 && zz <= 65534)
          {
            ID_Number = zz; 
            Serial.print(F("ID_Number "));
            Serial.println(zz);
          }
          else { Serial.println(F("Not a valid ID_Number value - Shall be between 0 and 65534"));}
 	break;	
            
      case o:				    // TIC_Offset command
      case O:
        zz = Serial.parseInt();
        if (zz >=10 && zz <= 1020) // *** was 200
          {
            TIC_Offset = zz; 
            Serial.print(F("TIC_Offset "));
            Serial.println(zz);
          }
          else { Serial.println(F("Not a valid TIC_offset - Shall be between 10 and 1020"));}
 	break;	
        
      case p:				    // set prefilter div command
      case P:
        zz = Serial.parseInt();
        if (zz >=2 && zz <= 4)
          {
            filterDiv = zz; 
            Serial.print(F("Prefilter div "));
            Serial.println(zz);
          }
          else { Serial.println(F("Not a valid prefilter value - Shall be between 2 and 4"));}
 	break;	
      
      case r:                                // run command
      case R:
        Serial.println(F("Run"));
        opMode = run;
        newMode = run;
        break;
        
      case s:				     // save command
      case S:        
        zz = Serial.parseInt();
        switch (zz) {
          
          case 1:
          Serial.print(F("Saved Gain and TimeConstant etc "));//
          EEPROM.write(995, highByte(temperature_Sensor_Type));
          EEPROM.write(996, lowByte(temperature_Sensor_Type));
          EEPROM.write(997, highByte(ID_Number));
          EEPROM.write(998, lowByte(ID_Number));
          EEPROM.write(999, highByte(int(TICmin * 10.0)));
          EEPROM.write(1000, lowByte(int(TICmin * 10.0)));
          EEPROM.write(1001, highByte(int(TICmax)));
          EEPROM.write(1002, lowByte(int(TICmax)));
          EEPROM.write(1003, highByte(int(x2 * 1000.0)));
          EEPROM.write(1004, lowByte(int (x2 * 1000.0)));
          EEPROM.write(1005, highByte(TIC_Offset));
          EEPROM.write(1006, lowByte(TIC_Offset));
          EEPROM.write(1007, highByte(filterDiv));
          EEPROM.write(1008, lowByte(filterDiv));
          EEPROM.write(1009, highByte(warmUpTime));
          EEPROM.write(1010, lowByte(warmUpTime));
          EEPROM.write(1011, highByte(int (damping *100)));
          EEPROM.write(1012, lowByte(int (damping *100)));
          EEPROM.write(1013, highByte(tempRef));
          EEPROM.write(1014, lowByte(tempRef));
          EEPROM.write(1015, highByte(tempCoeff));
          EEPROM.write(1016, lowByte(tempCoeff));
          EEPROM.write(1019, highByte(gain));
          EEPROM.write(1020, lowByte(gain));
          EEPROM.write(1021, highByte(timeConst));
          EEPROM.write(1022, lowByte(timeConst));
          Serial.println("");
          break;
                    
          case 2: 
          Serial.print(F("Saved DacValue "));
          EEPROM.write(1017, highByte(dacValueOut));
          EEPROM.write(1018, lowByte(dacValueOut));
          Serial.println(""); 
          break;          
          
          default:
          Serial.println(F("Not a valid value for save - Shall be 1 or 2"));
          }
      	break;	  
        
      case t:				// time constant command
      case T:	      	
        zz = Serial.parseInt();
        if (zz >=4 && zz <= 32000)
          {
            timeConst = zz; 
            Serial.print(F("time constant "));
            Serial.println(zz);
          }
          else { Serial.println(F("Not a valid time constant - Shall be between 4 and 32000"));}
 	    break;	

      case u:       // Set the minimum PWM for the fan
      case U:
        zz = Serial.parseInt();
        if (zz > 0 && zz < 255)
          {
          pwm_baseline = zz; 
          Serial.print(F("PWM Baseline "));
          Serial.println(zz);
          }
        else { Serial.println(F("Not a valid PWM Baseline value - Shall be between 0 and 255"));} 
      break;
      
      case v:       // Set ambient temperature for fan regulation
      case V:
        zz = Serial.parseInt();
        cool_baseline = zz; 
        Serial.print(F("Cool Baseline "));
        Serial.println(zz);            
      break;
            
      case w:				// set warm up time command
      case W:
        zz = Serial.parseInt();
        if (zz >=2 && zz <= 1000)
          {
            warmUpTime = zz; 
            Serial.print(F("Warmup time "));
            Serial.println(zz);
          }
          else { Serial.println(F("Not a valid warmup time - Shall be between 2 and 1000"));}
 	    break;	

      case x:       // Set kP gain /10
      case X:
        zz = Serial.parseInt();
        Kp = zz / 10.0;
        Serial.print(F("Kp "));
        Serial.println(Kp);
      break;

      case y:       // set Ki gain /10
      case Y:
        zz = Serial.parseInt();
        Ki = zz / 10.0;
        Serial.print(F("Ki "));
        Serial.println(Ki);        
      break;

      case z:       // set Kd gain /10
      case Z:
        zz = Serial.parseInt();
        Kd = zz / 10.0;
        ;
        Serial.print(F("Kd "));
        Serial.println(Kd);        
      break;
      
      default:
        Serial.println(F("No valid command"));
        break;
    };
    
    while(Serial.available() > 0) {
      ch = Serial.read();                //flush rest of line
    }
  }
}
 
    
////////////////////////////////////////////////////////////////////////////////////////////////
void printDataToSerial()
{
  Serial.print("\t");   // *** paulv
  Serial.print(duty_cycle);  // *** paulv added pwm duty cycle to report header
  Serial.print("\t");  
  Serial.print((time), DEC);  
  Serial.print("\t");  
  if (TIC_Value == 1023)
    {
    Serial.print(F("Missing 10MHz?"));
    Serial.print("\t");              
    }
    else if (nsDisplayedDecimals == false)
    { 
    Serial.print(((float)timer_us *1000) + TIC_ValueCorr-TIC_ValueCorrOffset, 0);
    Serial.print("\t"); 
    }
      else
      { 
      Serial.print(((float)timer_us *1000) + TIC_ValueCorr-TIC_ValueCorrOffset, 1);
      Serial.print("\t"); 
      }  
  Serial.print(dacValueOut, DEC);
  Serial.print("\t"); 
  if (temperature_Sensor_Type == 0)
    { Serial.print(tempADC2, DEC); }
  else{
    // paulv: IIR Low Pass Filter for temperature sensor readings
    tempADC2_IIR = tempADC2_IIR + ((tempADC2 - tempADC2_IIR) / IIR_Filter_Weight);
    Serial.print(temperature_to_C(tempADC2_IIR,temperature_Sensor_Type%10), 1); 
  }
  Serial.print("\t");  
  if (time > warmUpTime && opMode == run)
  { 
   if (PPSlocked == 0) {Serial.print(F("****"));} // paulv: changed from NoLock to better stand out
     else {Serial.print(F("Locked"));} 
   Serial.print("\t");              
  }
  else if (time > warmUpTime)
    {   
    Serial.print(F("Hold"));
    Serial.print("\t");              
    }
    else
    {   
    Serial.print(F("WarmUp"));
    Serial.print("\t");              
    }
    
 if (lessInfoDisplayed == false)
 {
  Serial.print(diff_ns, DEC);
  Serial.print("\t");
  Serial.print(TIC_ValueFiltered * 10 / filterConst, DEC);
  Serial.print("\t");
  Serial.print(timeConst, DEC);
  Serial.print("\t");
  Serial.print(filterConst, DEC);
  Serial.print("\t");  
  Serial.print(((49999-timer1CounterValue)), DEC);
  Serial.print("\t");

  // paulv: temp1 = Ambient temp
  if (temperature_Sensor_Type/10 == 0)
    { Serial.print(tempADC1, DEC); }
  else{
    // paulv: IIR Low Pass Filter for temperature sensor readings
    tempADC1_IIR = tempADC1_IIR + ((tempADC1 - tempADC1_IIR) / IIR_Filter_Weight);
    Serial.print(temperature_to_C(tempADC1_IIR,temperature_Sensor_Type/10), 1); 
  }
  Serial.print("\t"); 
       
  if (i == 1)
  {
  Serial.print(F("Five minute averages: TIC+DAC+temp"));
  Serial.print("\t");              
  }
  if (i == 2)
  {
  Serial.print(F("Now acquiring value: "));
  Serial.print(j);
  Serial.print("\t");              
  }
  if ((i >= 4) && (i <= 147))
  {
  Serial.print((i-4), DEC);
  Serial.print("\t");              
  Serial.print((StoreTIC_A[i-4]), DEC);
  Serial.print("\t");              
  Serial.print((StoreDAC_A[i-4]), DEC);
  Serial.print("\t");              
  unsigned int x = StoreTempA[i-4];
  if (temperature_Sensor_Type == 0)
    { Serial.print(x, DEC); }
  else
    { Serial.print(temperature_to_C(x/10, temperature_Sensor_Type%10), 1); }
  Serial.print("\t");               
  }
  if (i == 148)
  {
  Serial.print(F("Three hour averages: TIC+DAC+temp"));
  Serial.print("\t");              
  }
  if (i == 149)
  {
  Serial.print(F("Now acquiring value: "));
  Serial.print(k);
  Serial.print("\t");              
  }
 if ((i >= 150) && (i <=293))
  {
  Serial.print((i-150+1000), DEC);
  Serial.print("\t");              
  Serial.print((EEPROM.read(i-150+0)*256 + EEPROM.read(i-150+144)), DEC);
  Serial.print("\t");   
  unsigned int x = EEPROM.read(i-150+288)*256 + EEPROM.read(i-150+432);
  Serial.print(x, DEC);
  Serial.print("\t");                
  x = EEPROM.read(i-150+576)*256 + EEPROM.read(i-150+720);
  if ((x > 0) && (x < 65535))
    {
      if (temperature_Sensor_Type == 0)
        { Serial.print(x%10240, DEC); }
      else
        { Serial.print(temperature_to_C((x%10240)/10, temperature_Sensor_Type%10), 1); }
    } 
    else 
      { Serial.print(x, DEC); }
      
  Serial.print("\t");
  int y = x/10240;
  switch (y) {
    case 0:
    if (x > 0){Serial.print(F("Hold"));}    
    break;
    case 1:
    Serial.print(F("Restarted+hold"));    
    break;
    case 2:
    Serial.print(F("****"));  // paulv: changed from noLock to better stand out  
    break;
    case 3:
    Serial.print(F("Restarted"));    
    break;
    case 4:
    Serial.print(F("Locked"));    
    break;
    }
   Serial.print("\t");              
  } 
 if (i == 295)
  {
  Serial.print(F("TimeConst = "));
  Serial.print(timeConst);
  Serial.print(F(" sec "));
  Serial.print("\t");              
  }
 if (i == 296)
  {
  Serial.print(F("Prefilter = "));
  Serial.print(filterConst);
  Serial.print(F(" sec "));
  Serial.print("\t");              
  }
 if (i == 297)
  {
  Serial.print(F("Damping = "));
  Serial.print(damping);
  Serial.print(F(" Gain = "));
  Serial.print(gain);
  Serial.print("\t");              
  } 
 if (i == 298)
  {
  Serial.print(F("Type f1<enter> to get help+info"));
  Serial.print("\t");              
  }
 if (i == 299)
  {
  printHeader2_ToSerial();
  }

} // end of If (lessInfoDisplayed) 
  Serial.println("");     
  
}

////////////////////////////////////////////////////////////////////////////////////////////////
void printHeader1_ToSerial()
{
Serial.print(F("\n\r\n\rArduino GPSDO with 1ns TIC by Lars Walenius")); // paulv: added two new lines in front
}

////////////////////////////////////////////////////////////////////////////////////////////////
void printHeader2_ToSerial()
{
  // Serial.print(F("Rev. 3.0 170801")); paulv: original
  Serial.print(F("Rev. 3.6.0 paulv mods")); // paulv: added my mod indication
  if ((ID_Number >= 0) && (ID_Number < 65535))
    {
    Serial.print(F("  ID:"));
    Serial.print(ID_Number);
    Serial.print("\t");   
    }    
}

////////////////////////////////////////////////////////////////////////////////////////////////
void printHeader3_ToSerial()
{ 
  Serial.print("\t");    // *** paulv
  Serial.print("pwm");   // *** paulv added pwm duty cycle to report
  Serial.print("\t");
  Serial.print(F("time"));
  Serial.print("\t");               
  Serial.print(F("ns"));
  Serial.print("\t");              
  Serial.print(F("dac"));
  Serial.print("\t");              
  Serial.print(F("OCXO")); // *** paulv changed from temp
  Serial.print("\t");  
  Serial.print(F("status"));
  Serial.print("\t");              
  Serial.print(F("diff_ns filtX10")); // doesn't need a tab seperator
  Serial.print("\t");
  Serial.print(F("tc"));
  Serial.print("\t");              
  Serial.print(F("filt"));
  Serial.print("\t"); 
  Serial.print(F("timer1"));  
  Serial.print("\t");                           
  Serial.print(F("ambient")); // *** paulv: changed from temp1
  Serial.print("\n\r"); // *** paulv: new line  
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////

float temperature_to_C(int RawADC, int sensor) 
{
  float TempC; 
  float floatADC = RawADC;
  switch (sensor) {

    case 1: //LM35
    TempC = RawADC * 1100.0 / 1024.0 *0.1;
    break;
    case 2: //10k NTC beta 3950 + 68k (15-60C)
    TempC = floatADC * floatADC  * 0.0002536 - floatADC  * 0.2158 + 88.48 - floatADC  * floatADC  * floatADC  * 0.0000001179;   
    break;
    case 3: //10k NTC beta 3950 + 47k (15-65C)
    TempC = floatADC  * floatADC  * 0.0001564 - floatADC  * 0.1734 + 92.72 - floatADC  * floatADC  * floatADC  * 0.00000005572 ;  
    break;
    case 4: //10k NTC beta 3950 + 39k (25-70C)
    TempC = floatADC  * floatADC  * 0.0001667 - floatADC  * 0.181 + 99.21 - floatADC  * floatADC  * floatADC  * 0.00000006085 ;  
    break;
    case 5: //22k NTC beta 3950 + 120k (15-65C)
    TempC = floatADC  * floatADC  * 0.0001997 - floatADC  * 0.1953 + 92.11 - floatADC  * floatADC  * floatADC  * 0.00000008010 ;  
    break;
    case 8: //LM35 if Aref is low
    TempC = RawADC * 1070.0 / 1024.0 *0.1;
    break;
    case 9: //LM35 fahrenheit
    TempC = RawADC * 1100.0 / 1024.0 *0.1;
    TempC = TempC * 1.8 + 32; 
    break;
    default:
    TempC = RawADC;    
    }
    if (RawADC == 0){TempC = 0;}
  return TempC; // Return the Temperature in C or raw
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////
void setup() 
{
  pinMode(ppsLockedLED,OUTPUT);

  // paulv: additional declarations
  pinMode(decharge,INPUT);  // setup the port that will decharge C1
  pinMode(probe,OUTPUT);    // setup the port to allow a sw probe inside de ISR
    
  Serial.begin(9600);
  
// Print info and header in beginning
  printHeader1_ToSerial();
  Serial.print("\t");      // prints a tab
  
// Read data from EEPROM to variables
  unsigned int y; 
  y = EEPROM.read(991)*256 + EEPROM.read(992);
  if ((y > 0) && (y < 65535)) // set last stored xx if not 0 or 65535
    { 
    restarts = y ;
    }  
  y = EEPROM.read(993)*256 + EEPROM.read(994);
  if ((y > 0) && (y < 65535)) // set last stored xx if not 0 or 65535
    { 
    totalTime3h = y ;
    }
  y = EEPROM.read(995)*256 + EEPROM.read(996);
  if ((y > 0) && (y < 65535)) // set last stored xx if not 0 or 65535
    { 
    temperature_Sensor_Type = y ;    
    } 
  y = EEPROM.read(997)*256 + EEPROM.read(998);
  if ((y > 0) && (y < 65535)) // set last stored xx if not 0 or 65535
    { 
    ID_Number = y ;
    }  
  y = EEPROM.read(999)*256 + EEPROM.read(1000);
  if ((y > 0) && (y < 65535)) // set last stored xx if not 0 or 65535
    { 
    TICmin = y / 10.0 ;
    } 
  y = EEPROM.read(1001)*256 + EEPROM.read(1002);
  if ((y > 0) && (y < 65535)) // set last stored xx if not 0 or 65535
    { 
    TICmax = y;
    } 
  y = EEPROM.read(1003)*256 + EEPROM.read(1004);
  if ((y > 0) && (y < 65535)) // set last stored xx if not 0 or 65535
    { 
    x2 = y / 1000.0 ;
    } 
  y = EEPROM.read(1005)*256 + EEPROM.read(1006);
  if ((y > 0) && (y < 65535)) // set last stored xx if not 0 or 65535
    { 
    TIC_Offset = y;
    } 
  y = EEPROM.read(1007)*256 + EEPROM.read(1008);
  if ((y > 0) && (y < 65535)) // set last stored xx if not 0 or 65535
    { 
    filterDiv = y;
    } 
  y = EEPROM.read(1009)*256 + EEPROM.read(1010);
  if ((y > 0) && (y < 65535)) // set last stored xx if not 0 or 65535
    { 
    warmUpTime = y;
    } 
  y = EEPROM.read(1011)*256 + EEPROM.read(1012);
  if ((y > 0) && (y < 65535)) // set last stored xx if not 0 or 65535
    { 
    damping = y / 100.0;
    } 
  y = EEPROM.read(1013)*256 + EEPROM.read(1014);
  if ((y > 0) && (y < 65535)) // set last stored xx if not 0 or 65535
    { 
    tempRef = y;
    }  
  y = EEPROM.read(1015)*256 + EEPROM.read(1016);
  if ((y > 0) && (y < 65535)) // set last stored xx if not 0 or 65535
    { 
    tempCoeff = y;
    }     
  y = EEPROM.read(1017)*256 + EEPROM.read(1018);
  if ((y > 0) && (y < 65535)) // set last stored dacValueOut if not 0 or 65535
    { 
    dacValueOut = y;
    }   
  y = EEPROM.read(1019)*256 + EEPROM.read(1020);
  if ((y >= 10) && (y <= 65534)) // set last stored gain if between 10 and 65534
    { 
    gain = y;
    } 
     
  y = EEPROM.read(1021)*256 + EEPROM.read(1022);
  if ((y >= 4) && (y <= 32000)) // set last stored timeConst if between 4 and 32000
    { 
    timeConst = y;
    }   
  k = EEPROM.read (1023); // last index of stored 3 hour average
   if ((k > 143)|| (k < 0)) k = 0; //reset if k is invalid (eg with a new processor)
 
// Set "16bit DAC"  
  valuePWMhigh = highByte(dacValueOut); 
  valuePWMlow = lowByte(dacValueOut);
  analogWrite(PWMhighPin,valuePWMhigh);
  analogWrite(PWMlowPin,valuePWMlow);
  
// Set initial values  
  dacValue = dacValueOut * timeConst;
  timeConstOld = timeConst;
  filterConstOld = filterConst;
  TIC_ValueFilteredOld = TIC_Offset * filterConst;
  TIC_ValueFiltered = TIC_Offset * filterConst;
  TIC_ValueFilteredForPPS_lock = TIC_Offset * 16;
  tempADC2_Filtered = tempRef * 100;
  
  tempADC1_IIR = tempRef-100; // paulv: setup initial filter value for ambient temps
  tempADC2_IIR = tempRef;     // paulv: setup initial filter value for OCXO temps

// Set analog ref to about 1.1 Volt  
  analogReference(INTERNAL);
  TIC_Value = analogRead(A0);// just a dummy read
  

// Setup Timer 1 - counts events on pin D5. Used with 5MHz external clock source (needs to be less than 8MHz). Clock on rising edge. 
  
  TCCR1A = 0;    // reset Timer 1            
  TCCR1B = 0;
  
  TCCR1B |= (1 << WGM12); //configure timer1 for CTC mode

  OCR1A = 49999; // timer1 counts 50000 counts instead of 65536 to give same TCNT1 value every second
    
  TIMSK1 |= (1 << ICIE1); //interrupt on Capture (1PPS)
   
  TCCR1B |= ((1 << CS10) | (1 << CS11) | (1 << CS12)| (1 << ICES1) | (1 << ICNC1)); // start Timer 1 and interrupt with noise cancel
  

// Print info and header in beginning
  printHeader2_ToSerial();
  Serial.println("");      // prints a carriage return  
  Serial.println(F("Type f1 <enter> to get help+info"));
  printHeader3_ToSerial(); 

//clear  PPS flag and go on to main loop  
  PPS_ReadFlag = false; 
}


// *** paulv The fan controller
void control_fan() {
  // Use the low-pass filtered ambient temperature sensor value.
  // Do not mess with the original code so convert the value to Celsius here again.
  ambient_t = temperature_to_C(tempADC1_IIR,temperature_Sensor_Type/10);
  // PID calculation
  float temp_error = ambient_t - cool_baseline;
  integral = integral + temp_error;
  float derivative = (temp_error - prev_error);
  duty_cycle = pwm_baseline + (Kp * temp_error) + (Ki * integral) + (Kd * derivative);
  prev_error = temp_error;

  if (duty_cycle > max_pwm) { duty_cycle = max_pwm; } // max = 100% = 255
  if (duty_cycle < pwm_baseline) { duty_cycle = pwm_baseline;} // set minimum

/* for debugging: 
  Serial.print("Kp = ");
  Serial.print(Kp * temp_error);
  Serial.print("\tKi = ");
  Serial.print(Ki * integral);
  Serial.print("\tKd = ");
  Serial.print(Kd * derivative);  
  Serial.print("\tambient temp = ");
  Serial.print(ambient_t);
  Serial.print("\tduty cycle = ");
  Serial.println(duty_cycle);
*/           
  analogWrite(fan_pin, duty_cycle);
}


void loop() 
{
  if (PPS_ReadFlag == true) // set by capture of PPS on D8
  {
    calculation();
    control_fan(); // *** paulv
    getCommand();
    printDataToSerial();
    delay(100); // delay 100 milliseconds to give the PPS locked LED some time on if turned off in next step
    if ((dacValueOut < 3000 || dacValueOut > 62535) && opMode == run)
     { 
      digitalWrite(ppsLockedLED,false); // turn off (flash)LED 13 if DAC near limits  
     }
    PPS_ReadFlag = false;    
  }
  
// timer1 overflow counter // if no 10MHz time will not increment as no overflows will be registered
  TCNT1old = TCNT1new;  
  TCNT1new = TCNT1; 
  if (TCNT1new < TCNT1old)   // if just got an overflow on TCNT1 may miss some during calc+print etc 
  {
     overflowCount++; // normally will increment every 10ms (50000x200ns) used for time since start(seconds)
     if (overflowCount > 31 && (overflowCount - 30)  % 100 == 0) // sense if more than 1sec since last pps pulse
      {
      Serial.println(F(" No PPS"));         
      digitalWrite(ppsLockedLED,false); // blink the LED two times if no PPS
      delay(50);
      digitalWrite(ppsLockedLED,true);
      delay(20);
      digitalWrite(ppsLockedLED,false);
      delay(100);
      digitalWrite(ppsLockedLED,true);
      delay(20);
      digitalWrite(ppsLockedLED,false); 
      if (overflowCount > 2000 ) newMode = run; // resets timer_us etc after 20s without PPS in calculation function
      if (overflowCount > 20000 )lockPPScounter = 0; // resets locked after about 200secs without PPS
      getCommand(); 
      if (time < warmUpTime)// avoid incrementing time before pps comes first time
        {
         overflowCount = 0 ;
        }
     }
  }
}
