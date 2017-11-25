
/**
 * Simple power supply monitored by arduino
 * - We have an INA219 monitoring voltage and current
 * - Basic relay (with protection diode) disconnecting output when active
 * - Nokia 5110 screen printing out voltage and amps
 * 
 * Gettings the current limit is a bit tricky as it is done in anolog world through a resistor
 */
/**
    Pin Out : 
        D2 : Load On LED,  output
        D3 : Fan PWM command output
        D4 : Load switch , input
        D5 : Relay command, output
 
        D8/D12 LCD
  
        A1    : max current input
        A4/A5 : I2C
        
 
 */


#include <Wire.h>
#include "simpler_INA219.h"
#include "power_screen.h"

#define SHUNT_VALUE 20 // 22 mOhm
#define SIGN  1. //-1. // in case you inverted vin+ and vin*

#define DEBUG 

#if 0
#define MARK(x) screen->printStatus("." #x ".")
#else
#define MARK(x) {}
#endif


powerSupplyScreen *screen;
simpler_INA219 *voltageSensor; // Declare and instance of INA219 : High side voltage
simpler_INA219 *currentSensor; // Declare and instance of INA219 : Low side current

float currentBias=300./1000.; // to correct current bias

bool connected=false; // is the relay disconnecting voltage ? false => connected

int relayPin = 5;      // D6 : L
int buttonPin = 4;   // choose the input pin (for a pushbutton)
int buttonLedPin = 2;   // Led in on/off button (the above button)
int maxAmpPin=A1;     // A1 is the max amp pin voltage driven
int bounce=0;
#define ANTIBOUNCE 2
#define NEXT_CYCLE() delay(250)
//#define VERBOSE 1
//#define TESTMODE

#define MAX_EVAL_POINTS 10
const int extrapol[MAX_EVAL_POINTS][2]=
{
  {100,2},
  {250,40},
  {500,100},
  {750,163},
  {1000,256},
  {1250,350},
  {1500,450},
  {2000,630},
  {2300,760},
  {3400,1024}  
};
/**
 * 
 */
int evaluatedMaxAmp(int measure)
{
  if(measure<6) return 100;
  if(measure>1024) return 3500;

  for(int i=0;i<MAX_EVAL_POINTS-1;i++)
  {
    if(measure>=extrapol[i][1] && measure<extrapol[i+1][1])
    {
            float scale=measure-extrapol[i][1];
            scale=scale/(float)(extrapol[i+1][1]-extrapol[i][1]);

            float r=extrapol[i+1][0]-extrapol[i][0];
            r=r*scale;
            r+=extrapol[i][0];
            return (int)(r);
    }
  }
  return 3500;
}

/**
 * 
 * 
 * 
 */
void setup(void) 
{

  pinMode(relayPin, OUTPUT);  // declare relay as output
  pinMode(buttonPin, INPUT_PULLUP);    // declare pushbutton as input
  pinMode(buttonLedPin,OUTPUT); // declare button led as ouput 
  digitalWrite(buttonLedPin,0);
 // D3 is PWM for fan
  pinMode(3, OUTPUT);  // D3
  TCCR2A = _BV(COM2A1)| _BV(WGM21) | _BV(WGM20)| _BV(COM2B1) ;
  //TCCR2B = _BV(CS22);
  //OCR2A = 180;
  OCR2B = 120;

  Serial.begin(9600);   
  Serial.print("Start\n"); 

  Serial.print("Setting up screen\n");
  screen=new powerSupplyScreen;
  Serial.print("Screen Setup\n");
  Serial.print("Setup done\n");

  screen->printStatus("Low side");  
  currentSensor=new simpler_INA219(0x40,SHUNT_VALUE);   // 22 mOhm low side current sensor
  currentSensor->setMultiSampling(2); // average over 4 samples
  currentSensor->begin();
  screen->printStatus("High side");  
  voltageSensor=new simpler_INA219 (0x44,100); // we use that one only for high side voltage
  voltageSensor->begin();
  setRelayState(false);
  delay(150);
  screen->printStatus(".1.");
  
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
 * 
 */
void loop(void) 
{
  float busVoltage = 0, busVoltageLowSide=0;
  float current = 0; // Measure in milli amps
  float power = 0;
  MARK(3);

#ifdef TESTMODE
  int maxAmp=analogRead(maxAmpPin);
  char amp[16];
  sprintf(amp,"READ : %04d",maxAmp);
//  u8g.drawStr(3,48,amp);
   Serial.print(amp);

#else

  busVoltage = voltageSensor->getBusVoltage_V();
  busVoltageLowSide=currentSensor->getBusVoltage_V();  
  current = currentSensor->getCurrent_mA();
 
  
  float currentInMa=SIGN*current; // it is inverted (?)
  if(currentInMa<0)  // clamp noise
      currentInMa=0;
  
  
  
  MARK(4);

  
  int maxMeasure=analogRead(maxAmpPin);
  int maxAmp=evaluatedMaxAmp(maxMeasure);
#if 1
  if(busVoltage>30.) // cannot read
  {
      Serial.print("Voltage overflow\n");
      screen->printStatus("Err HS");
      NEXT_CYCLE();       
      return;
  }
   if(busVoltageLowSide>30.) // cannot read
  {
      Serial.print("Low side HS\n");
      screen->printStatus("Err LS");
      NEXT_CYCLE();       
      return;
  }
#endif  
  if(!connected)
  {
//    strcpy(stA,"-- DISC --");
  }
  MARK(5);
  
  
#if 1 //def VERBOSE
  Serial.print(busVoltage);
  Serial.print("-----------------\n");
#endif  
  
#if 1
  busVoltage=busVoltage-(currentInMa*SHUNT_VALUE)/1000000.; // compensate for voltage drop on the shunt  
#endif  
  screen->displayFull(busVoltage*1000,currentInMa,maxAmp,maxMeasure,connected);

#endif
  if(buttonPressed()) // button pressed
  {
    connected=!connected;
    setRelayState(connected); // when relay is high, the output is disconnected    
  }
  
  NEXT_CYCLE();   
  MARK(6);
}
// EOF

