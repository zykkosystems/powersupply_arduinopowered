
/**
 * Simple power supply monitored by arduino
 * - We have an INA219 monitoring voltage and current
 * - Basic relay (with protection diode) disconnecting output when active
 * - Nokia 5110 screen printing out voltage and amps
 *
 */
/**
    Digital Pins:
    -------------
        D2 : LoadOn LED,  output
        D3 : Fan PWM command output
        D4 : Load switch , input
        D5 : Relay command, output

        D6/D7 : Rotary encoder

        D8/D12 : LCD
 
    Analog  Pins:
    -------------
        A1    : max current set (maxCurrent=value*10-40,output)
        A2    : Current limiting mode (input,active low)
        A4/A5 : I2C


 */


#include <Wire.h>
#include "simpler_INA219.h"
#include "power_screen.h"
#include "Rotary.h"
#include "wav_irotary.h"
#include "pow_currentControl.h"
#include "U8g2lib.h"
//#define TESTMODE
//#define NO_LOW_SIDE
//#define CALIBRATION

#define SHUNT_VALUE       20 // 20 mOhm
#define WIRE_RESISTANCE   100 // dafuq ?
#define SIGN  1. //-1. // in case you inverted vin+ and vin*
#define ANTIBOUNCE 10

static void setRelayState(bool state);

powerSupplyScreen   *screen=NULL;
simpler_INA219      *voltageSensor=NULL; // Declare and instance of INA219 : High side voltage
simpler_INA219      *currentSensor=NULL; // Declare and instance of INA219 : Low side current
MaxCurrentControl   *maxCurrentControl=NULL;

bool connected=false; // is the relay disconnecting voltage ? false => connected

const int ccModePin    = A2;
const int maxAmpPin    = A1;     // A1 is the max amp pin voltage driven
const int relayPin     = 5;      // D6 : L
const int buttonPin    = 4;   // choose the input pin (for a pushbutton)
const int buttonLedPin = 2;   // Led in on/off button (the above button)

const int rotaryA      = 6;
const int rotaryB      = 7;

int bounce=0;


#define XXSTEP(i,st) { Serial.println(st);screen->printStatus(i,st);delay(20);}

/**
 *
 *
 *
 */
void mySetup(void)
{
  pinMode(ccModePin, INPUT_PULLUP);  // cc mode pin, active low
  pinMode(relayPin,  OUTPUT);        // declare relay as output
  pinMode(buttonPin, INPUT_PULLUP);  // declare pushbutton as input
  pinMode(buttonLedPin,OUTPUT);      // declare button led as ouput
  digitalWrite(buttonLedPin,0); 

  Serial.begin(115200);
  Serial.println("Board Start"); 
  delay(100);
  // D3 is PWM for fan
  pinMode(3, OUTPUT);  // D3
  TCCR2A = _BV(COM2A1)| _BV(WGM21) | _BV(WGM20)| _BV(COM2B1) ;
  OCR2B = 120;

  Serial.println("Setting up screen");
  delay(100);
  screen=new powerSupplyScreen;
  Serial.println("Screen Setup done");
  XXSTEP(0,"*Init PSU*");  
  XXSTEP(1,"-Init Low");  
#ifndef TESTMODE
#ifndef NO_LOW_SIDE
  currentSensor=new simpler_INA219(0x40,SHUNT_VALUE);   // 22 mOhm low side current sensor
  currentSensor->setMultiSampling(2); // average over 4 samples
  XXSTEP(2,"Low Start");
#endif
  XXSTEP(1,"Init High");  
  voltageSensor=new simpler_INA219 (0x44,100); // we use that one only for high side voltage  
  XXSTEP(2,"High Start");
#endif
  XXSTEP(1,"Max Current");
  
#if 1
  // If you use rotarty encoder + external DAC
  maxCurrentControl=rotaryCurrentControl_instantiate(maxAmpPin,rotaryA,rotaryB,0x60);  
#else
  // if you use a simple POT
  maxCurrentControl=potCurrentControl_instantiate(maxAmpPin);
#endif
  setRelayState(false);
  
  XXSTEP(2,"All Ok");  
}
/**
 * drive relay
 * If state is on; the relay disconnects the output
 * if state if off, output is connected to power supply
 */
void setRelayState(bool state)
{
  if(state)
  {
    // Auto zero the low side ina
    currentSensor->autoZero();
    digitalWrite(buttonLedPin,HIGH);
    digitalWrite(relayPin, HIGH);
  }
  else
  {
    digitalWrite(buttonLedPin,LOW);
    digitalWrite(relayPin, LOW);
  }
}
/**
 *    Check for button press
      a high enough value of NEXT_CYLE should get ride of the bumps on the button (> ~ 20 ms)
 */
bool buttonPressed()
{
  if(bounce)
  {
    bounce--;
    return false;
  }
  int r=digitalRead(buttonPin);
  if(!r)
  {
     Serial.print("Press\n");
     bounce=ANTIBOUNCE;
     return true;
  }
  return false;
}
/**
 * \fn myRun
 * \brief infinite runloop
 */
void myRun(void)
{
  float busVoltage = 0, busVoltageLowSide=0;
  float current = 0; // Measure in milli amps
  float currentInMa=0;

  static int refresh=20;
  int maxCurrent;

#ifndef TESTMODE
  busVoltage = voltageSensor->getBusVoltage_V();
#ifndef NO_LOW_SIDE
  busVoltageLowSide=currentSensor->getBusVoltage_V();
  currentInMa=SIGN*currentSensor->getCurrent_mA(); // it is inverted (?)
#endif
#endif
  if(currentInMa<0)  // clamp noise
      currentInMa=0;

  maxCurrentControl->run();
  maxCurrent=maxCurrentControl->getMaxCurrentMa();

  if(buttonPressed()) // button pressed
  {
    connected=!connected;
    setRelayState(connected); // when relay is high, the output is disconnected
  }  

  
  // limit screen update (but run rotary every time)
  refresh++;
  if(refresh<15)
  {
      delay(10);
      return;
  }
  refresh=0;


  bool err=false;
#ifndef TESTMODE
  if(busVoltage>30.) // cannot read
  {
      Serial.print("Voltage overflow\n");
      screen->printStatus(1,"Err HS");
      err=true;
  }
   if(busVoltageLowSide>30.) // cannot read
  {
      Serial.print("Low side HS\n");
      screen->printStatus(2,"Err LS");
      err=true;
  }
  if(err)
  {
      return;
  }
#endif



  bool ccmode=false;
  int  cpin=analogRead(ccModePin);

  if(cpin<400) ccmode=true;

  // After checking with my multimeter
  // there is a 20 mv offset + a drift related to wires
  // correct it
  busVoltage=busVoltage+0.040-(currentInMa*(SHUNT_VALUE+WIRE_RESISTANCE))/1000000.; // compensate for voltage drop on the shunt
  screen->setVoltage(busVoltage*1000);
#ifndef CALIBRATION
  screen->setCurrent(currentInMa,maxCurrent,connected);
#else
  screen->setCurrentCalibration(currentInMa,maxCurrent,connected);
#endif
  
  screen->setLimitOn(ccmode);
  screen->refresh();
}
// EOF
