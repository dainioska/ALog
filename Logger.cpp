/*
Data logger library
Written by Andy Wickert
September-October 2011

# LICENSE: GNU GPL v3

Logger.cpp is part of Logger, an Arduino library written by Andrew D. Wickert.
Copyright (C) 2011-2013, Andrew D. Wickert

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

////////////////////////////////////////////////////
// INCLUDE HEADER FILE AND ITS INCLUDED LIBRARIES //
////////////////////////////////////////////////////

#include <Logger.h>

// Breaking things up at first, but will try to just put all of the initialize / 
// setup stuff in the constructor eventually (or maybe just lump "initialize" 
// and "setup")


/////////////////////////////////////////////////////
// STATIC MEMBERS OF CLASS, SO ACCESSIBLE ANYWHERE //
/////////////////////////////////////////////////////

// MAYBE PUT UNDERSCORES BEFORE ALL OF THESE VARS, IF I THINK THERE IS RISK OF 
// RE-DEFINING THEM IN SKETCH

// DEFINE BOARD BASED ON MCU TYPE

// First, give integer values to the different board types
const int bottle_logger=0;
const int big_log=1;
const int log_mega=2; // In development

// Then define _model
#if defined(__AVR_ATmega168__) || defined(__AVR_ATmega328P__) || defined(__AVR_ATmega8__) || defined(__AVR_ATmega88__)
  const int _model = bottle_logger;
  const char _model_name[] = "bottle_logger";
#elif defined(__AVR_ATmega644__) || defined(__AVR_ATmega644P__)
  const int _model = big_log;
  const char _model_name[] = "big_log";
#elif defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
  const int _model = log_mega;
  const char _model_name[] = "log_mega";
#endif

// DECLARE PINS
// Should do the full declaration here with some "if's" so I can do "const int"

/////////////////
// ASSIGN PINS //
/////////////////

#if(_model == bottle_logger)
  // SD card: CSpin and protected pins
  const int SCKpin = 13;
  const int MISOpin = 12;
  const int MOSIpin = 11;
  const int CSpin = 10;
  // Protected I2C pins
  const int SDApin = A4;
  const int SCLpin = A5;
  // Digital pins
  const int SensorPowerPin = 4; // Activates voltage regulator to give power to sensors
  const int SDpowerPin = 8; // Turns on voltage source to SD card
  const int ClockPowerPin = 6; // Activates voltage regulator to power the RTC (otherwise is on backup power from VCC or batt)
  const int LEDpin = 9; // LED to tell user if logger is working properly
  const int wakePin = 2; // interrupt pin used for waking up via the alarm
  const int interruptNum = wakePin-2; // =0 for pin 2, 1 for pin 3
  const int manualWakePin = 5; // Wakes the logger with a manual button - overrides the "wait for right minute" commands
#elif(_model == big_log)
  // SD card: CSpin and protected pins
  const int SCKpin = 7;
  const int MISOpin = 6;
  const int MOSIpin = 5;
  const int CSpin = 4;
  // Protected I2C pins
  const int SDApin = 23;
  const int SCLpin = 22;
  // Digital pins
  const int SensorPowerPin = 21; // Activates voltage regulator to give power to sensors
  const int SDpowerPin = 22; // Turns on voltage source to SD card
  const int LEDpin = 23; // LED to tell user if logger is working properly
  const int wakePin = 10; // interrupt pin used for waking up via the alarm
  const int interruptNum = 0; // =0 for pin 2, 1 for pin 3
#endif

/////////////////////////////////////////////////
// GLOBAL VARIABLES DEFINED IN INITIALIZE STEP //
/////////////////////////////////////////////////

// Logging interval - wake when minutes == this
//int log_minutes; //**chad delete when new alarm works
int dayInterval;
int hourInterval;
int minInterval;
int secInterval;
// Alarm counters
int _days;
int _hours;
int _minutes;
int _seconds;
// Use the sleep mode?
bool _use_sleep_mode = true; // Defaults to true

bool CAMERA_IS_ON = false; // for a video camera

// IS_LOGGING tells the logger if it is awake and actively logging
// Prevents being put back to sleep by an event (e.g., rain gage bucket tip)
// if it is in the middle of logging, so it will return to logging instead.
bool IS_LOGGING = false;

// Filename and logger name
// Filename is set up as 8.3 filename:
//char filename[12];
char* filename;
char* logger_name;

// For interrupt from sensor
bool extInt;
bool NEW_RAIN_BUCKET_TIP = false; // flag
bool LOG_ON_BUCKET_TIP; // Defaults to False, true if you should log every 
                        // time an event (e.g., rain gage bucket tip) happens
unsigned int rotation_count = 0;


/////////////////////////
// INSTANTIATE CLASSES //
/////////////////////////

// Both for same clock, but need to create instances of both
// classes in library (due to my glomming of two libs together)
RTClib RTC;
DS3231 Clock;

// SD CLASSES
SdFat sd;
SdFile datafile;
SdFile otherfile; // for rain gage, camera timing, and anything else that 
                  // doesn't follow the standard logging cycle / regular timing

DateTime now;

///////////////////////////////////
///////////////////////////////////
//// LOGGER LIBRARY COMPONENTS ////
///////////////////////////////////
///////////////////////////////////

// Constructor
Logger::Logger(){}

void Logger::initialize(char* _logger_name, char* _filename, int _dayInterval, int _hourInterval, int _minInterval, int _secInterval, bool _ext_int, bool _LOG_ON_BUCKET_TIP){
  /*
  Model automatically determined from the MCU type and is used to modify 
  pinout-dependent functions. There may be a need to add a board version in the 
  future, and I don't see a way around passing that via a human-coded variable. 
  */
  
  MCUSR = MCUSR & B11110111;  // Clear the reset flag, the WDRF bit (bit 3) of MCUSR for watchdog timer.
  wdt_disable();  //Disable Watch dog timer

  ///////////////////
  // SLEEP COUNTER //
  ///////////////////
  
  // Assign the global variables (not intended to change) to the input values
  logger_name = _logger_name;
  filename = _filename;
  dayInterval = _dayInterval;
  hourInterval = _hourInterval;
  minInterval = _minInterval;
  secInterval = _secInterval;
  
  // If all logging intervals are 0, then this means that we don't go to sleep:
  // continuous logging!
  // (If all were set to 0, logger would try to log continuously anyway; this
  // just makes things easier by avoiding the sleep function
  if ((dayInterval || hourInterval || minInterval || secInterval) == false){
    _use_sleep_mode = false;
    IS_LOGGING = true; // is always logging, in this case!
  }
  
  // Assign the global and changable variables to input values
  LOG_ON_BUCKET_TIP = _LOG_ON_BUCKET_TIP;

  //////////////////////////////////////////
  // EXTERNAL INTERRUPT (E.G., RAIN GAGE) //
  //////////////////////////////////////////
  
  // Specific for the bottle logger!
  extInt = _ext_int;
  if (extInt){
    pinMode(extInt, INPUT);
    digitalWrite(3, HIGH); // enable internal 20K pull-up
    //pinMode(6, INPUT);
    //digitalWrite(6, LOW);
  }
  
  ////////////
  // SERIAL //
  ////////////

  Serial.begin(38400);

  /////////////////////////////
  // Logger models and setup //
  /////////////////////////////

  if (_model == 0 || _model == 1 || _model == 2){
    Serial.print(F("Logger model = "));
    Serial.println(_model_name);
  }
  else{
    Serial.println(F("Error: model name must be ""bottle"" or ""big""."));
    Serial.println(F("Stopping execution."));
    LEDwarn(100); // 100 quick flashes of the LED
    sleepNow(); // Do nothing until reset
    }
  
  // From weather station code For power savings
  // http://jeelabs.net/projects/11/wiki/Weather_station_code
  #ifndef cbi
  #define cbi(sfr, bit) (_SFR_BYTE(sfr) &= ~_BV(bit))
  #endif
  #ifndef sbi
  #define sbi(sfr, bit) (_SFR_BYTE(sfr) |= _BV(bit))
  #endif
  
  //////////////////////
  // LOGGER FILE NAME //
  //////////////////////
  
  delay(10);
  
  Serial.print(F("Filename: "));
  Serial.println(filename);
  
  Serial.println(F("Logger done initializing."));
  delay(10);
  
}

void Logger::setupLogger(){

  Serial.println(F("Beginning logger setup."));

  // We use a 3.3V regulator that we can switch on and off (to conserve power) 
  // to power the instruments. Therefore, we set the analog reference to 
  // "EXTERNAL". Do NOT set it to the internal 1.1V reference voltage or to 
  // "DEFAULT" (VCC), UNLESS you are  absolutely sure that you need to and the
  // 3.3V regulator connected to the AREF pin is off. Otherwise, you will short
  // 1.1V (or VCC) against 3.3V and likely damage the MCU / fry the ADC(?)
  analogReference(EXTERNAL); 

  //////////////
  // SET PINS //
  //////////////

  pinMode(wakePin,INPUT); // Interrupt to wake up
  digitalWrite(wakePin,HIGH); // enable internal 20K pull-up
  // Set the rest of the pins
  pinMode(CSpin,OUTPUT);
  pinMode(SensorPowerPin,OUTPUT);
  pinMode(LEDpin,OUTPUT);
  pinMode(SDpowerPin,OUTPUT);
  pinMode(ClockPowerPin,OUTPUT);
  // Manual wake pin
  if (_model == bottle_logger){
    Serial.println(F("Setting manualWakePin"));
    pinMode(manualWakePin,INPUT);
    digitalWrite(manualWakePin,HIGH); // enable internal 20K pull-up
  }
  //Start out with SD, Sensor pins set LOW
  digitalWrite(SDpowerPin,LOW);
  digitalWrite(SensorPowerPin,LOW);

////////////
// SERIAL //
////////////

Serial.begin(38400);

announce_start(); // Announce start

///////////////////
// WIRE: I2C RTC //
///////////////////

Wire.begin();

SDpowerOn();
RTCon();

/////////////////
// CHECK CLOCK //
/////////////////

// Includes check whether you are talking to Python terminal
startup_sequence();

///////////////////
// SD CARD SETUP //
///////////////////

// Initialize SdFat or print a detailed error message and halt
// Use half speed like the native library.
// change to SPI_FULL_SPEED for more performance.

name();

delay(5);
Serial.print(F("Initializing SD card..."));
if (!sd.begin(CSpin, SPI_HALF_SPEED)){
  Serial.println(F("Card failed, or not present"));
  LEDwarn(20); // 20 quick flashes of the LED
  sd.initErrorHalt();
}

Serial.println(F("card initialized."));
Serial.println();
LEDgood(); // LED flashes peppy happy pattern, indicating that all is well

start_logging_to_otherfile("StartTimes.txt");
otherfile.print(now.unixtime());
otherfile.print(",");
end_logging_to_otherfile();

start_logging_to_datafile();

name();
Serial.println(F("Logger initialization complete! Ciao bellos."));

delay(10);

Clock.checkIfAlarm(1); //Clear alarm flags
Clock.checkIfAlarm(2); //Clear alarm flags

    bool Century, h12 = false;
    bool PM;
    _days = Clock.getDoW();
    _hours = Clock.getHour(h12, PM);
    _minutes = Clock.getMinute();
    _seconds = Clock.getSecond()+5;  //Set first alarm to activate in 5 seconds.
    if(_seconds > 59){_seconds = _seconds - 60; _minutes++;}
    if(_minutes > 59){_minutes = _minutes - 60; _hours++;}
    if(_hours > 23){_hours = _hours - 24; _days++;}
    if(_days > 7){_days = _days - 7;} 

alarm( _days, _hours, _minutes, _seconds);  //Set first alarm.
checkTime();  // Verify time
displayAlarms();  // Verify Alarms
delay(10);
if (_use_sleep_mode){
  SDpowerOff();
  RTCsleep();
  }
}

/////////////////////////////////////////////
// GETTERS AND SETTERS: ADD MORE AS NEEDED //
/////////////////////////////////////////////

  bool Logger::get_use_sleep_mode(){
    return _use_sleep_mode;
  }


/////////////////////////////////////////////////////
// PRIVATE FUNCTIONS: UTILITIES FOR LOGGER LIBRARY //
/////////////////////////////////////////////////////

  void Logger::pinUnavailable(int pin){
    int _errorFlag = 0;

    char* _pinNameList_crit[9] = {"CSpin", "SensorPowerPin", "SDpowerPin", "ClockPowerPin", "LEDpin", "wakePin"};
    int _pinList_crit[9] = {CSpin, SensorPowerPin, SDpowerPin, ClockPowerPin, LEDpin, wakePin};

    char* _pinNameList[9] = {"MISOpin", "MOSIpin", "SCKpin", "SDApin", "SCLpin"};
    int _pinList[9] = {MISOpin, MOSIpin, SCKpin, SDApin, SCLpin};
    
    for (int i=0; i<9; i++){
      if (pin == _pinList[i]){
        _errorFlag++;
        Serial.print(F("Error: trying to alter the state of Pin "));
        Serial.println(_pinList[i]);
        Serial.print(F("This pin is assigned in a system-critical role as: "));
        Serial.println(_pinNameList[i]);
        // Note: numbers >13 in standard Arduino are analog pins
      }
    }
    
    bool SPI_or_I2C_flag = false;
    for (int i=0; i<9; i++){
      if (pin == _pinList_crit[i]){
        SPI_or_I2C_flag = true;
        break;
      }
    }
    if (SPI_or_I2C_flag){
      Serial.println(F("You are using the SPI or I2C bus; take care that this does not clash"));
      Serial.println(F("with the SD card interface (SPI) or the clock interface (I2C)."));
    }
    
    for (int i=0; i<9; i++){
      if (pin == _pinList_crit[i]){
        _errorFlag++;
        Serial.print(F("Error: trying to alter the state of Pin "));
        Serial.println(_pinList_crit[i]);
        Serial.print(F("This pin is assigned in a system-critical role as: "));
        Serial.println(_pinNameList_crit[i]);
        // Note: numbers >13 in standard Arduino are analog pins
      }
    }
    
    if (_errorFlag){
      Serial.println(F("Error encountered."));
      Serial.println(F("Stalling program: cannot reassign critical pins to sensors, etc."));
      LEDwarn(50); // 50 quick flashes of the LED
      // Do nothing until reset
      while(1){}
    }
  }
  
  void Logger::sleepNow()         // here we put the arduino to sleep
  {
    IS_LOGGING = false;           // Definitely not logging anymore

      /* Now is the time to set the sleep mode. In the Atmega8 datasheet
       * http://www.atmel.com/dyn/resources/prod_documents/doc2486.pdf on page 35
       * there is a list of sleep modes which explains which clocks and 
       * wake up sources are available in which sleep mode.
       *
       * In the avr/sleep.h file, the call names of these sleep modes are to be found:
       *
       * The 5 different modes are:
       *     SLEEP_MODE_IDLE         -the least power savings 
       *     SLEEP_MODE_ADC
       *     SLEEP_MODE_PWR_SAVE
       *     SLEEP_MODE_STANDBY
       *     SLEEP_MODE_PWR_DOWN     -the most power savings
       *
       * For now, we want as much power savings as possible, so we 
       * choose the according 
       * sleep mode: SLEEP_MODE_PWR_DOWN
       * 
       */  
      set_sleep_mode(SLEEP_MODE_PWR_DOWN);   // sleep mode is set here

  //    setPrescaler(6); // Clock prescaler of 64, slows down to conserve power
      cbi(ADCSRA,ADEN);                    // switch Analog to Digitalconverter OFF

      sleep_enable();          // enables the sleep bit in the mcucr register
                               // so sleep is possible. just a safety pin 

      /* Now it is time to enable an interrupt. We do it here so an 
       * accidentally pushed interrupt button doesn't interrupt 
       * our running program. if you want to be able to run 
       * interrupt code besides the sleep function, place it in 
       * setup() for example.
       * 
       * In the function call attachInterrupt(A, B, C)
       * A   can be either 0 or 1 for interrupts on pin 2 or 3.   
       * 
       * B   Name of a function you want to execute at interrupt for A.
       *
       * C   Trigger mode of the interrupt pin. can be:
       *             LOW        a low level triggers
       *             CHANGE     a change in level triggers
       *             RISING     a rising edge of a level triggers
       *             FALLING    a falling edge of a level triggers
       *
       * In all but the IDLE sleep modes only LOW can be used.
       */

      if (dayInterval && hourInterval && minInterval && secInterval == -1 && extInt == false){
        Serial.println(F("All inputs to wake from sleep disabled! Reprogram, please!"));
      }
        //Serial.print(F("interrupt"));  delay(10);
      if (dayInterval || hourInterval || minInterval || secInterval != -1){
        attachInterrupt(interruptNum, wakeUpNow, LOW); // wakeUpNow when wakePin goes LOW 
        //Serial.println(F(" attached")); delay(10);
      }

      if (extInt){
        attachInterrupt(1, wakeUpNow_tip, LOW);
      }

      sleep_mode();            // here the device is actually put to sleep!!
                
                              // THE PROGRAM CONTINUES FROM HERE AFTER WAKING UP.  

      sleep_disable();         // first thing after waking from sleep:
                               // disable sleep...

      // detachInterrupt(1); // crude, but keeps interrupts from clashing. Need to improve this to allow both measurements types!
      // 06-11-2015: The above line commented to allow the rain gage to be read
      // at the same time as other readings
      // Maybe move this to specific post-wakeup code?
      detachInterrupt(interruptNum);      // disables interrupt so the 
                                          // wakeUpNow code will not be executed 
                                          // during normal running time.
  }

  // Must be defined outside of Logger class
  void wakeUpNow()        // here the interrupt is handled after wakeup
  {
    // execute code here after wake-up before returning to the loop() function
    // timers and code using timers (serial.print and more...) will not work here.
    // we don't really need to execute any special functions here, since we
    // just want the thing to wake up
    IS_LOGGING = true;                   // Currently logging
                                         //    this will allow the logging 
                                         //    to happen even if the logger is
                                         //    already awake to deal with a 
                                         //    rain gauge bucket tip
   }

  void wakeUpNow_tip()        // here the interrupt is handled after wakeup
  {
    // execute code here after wake-up before returning to the loop() function
    // timers and code using timers (serial.print and more...) will not work here.
    // we don't really need to execute any special functions here, since we
    // just want the thing to wake up
    //sleep_disable();         // first thing after waking from sleep:
                               // disable sleep...
    NEW_RAIN_BUCKET_TIP = true;
    // If the logger is already logging, run
    // !!!!!!!!!! WHAT WAS SUPPOSED TO GO INSIDE HERE?
    if (IS_LOGGING){
      
    }
  }

void Logger::alarm(int _days ,int _hours, int _minutes, int _seconds){

/* Alarm bit info:
 * A1Dy true makes the alarm go on A1Day = Day of Week,
 * A1Dy false makes the alarm go on A1Day = Date of month.
 *
 * byte AlarmBits sets the behavior of the alarms:
 *	Dy	A1M4	A1M3	A1M2	A1M1	Rate
 *	X	1		1		1		1		Once per second
 *	X	1		1		1		0		Alarm when seconds match
 *	X	1		1		0		0		Alarm when min, sec match
 *	X	1		0		0		0		Alarm when hour, min, sec match
 *	0	0		0		0		0		Alarm when date, h, m, s match
 *	1	0		0		0		0		Alarm when DoW, h, m, s match
 *
 *	Dy	A2M4	A2M3	A2M2	Rate
 *	X	1		1		1		Once per minute (at seconds = 00)
 *	X	1		1		0		Alarm when minutes match
 *	X	1		0		0		Alarm when hours and minutes match
 *	0	0		0		0		Alarm when date, hour, min match
 *	1	0		0		0		Alarm when DoW, hour, min match
 

const int ALRM1_MATCH_EVERY_SEC  0b1111  // once a second
const int ALRM1_MATCH_SEC        0b1110  // when seconds match
const int ALRM1_MATCH_MIN_SEC    0b1100  // when minutes and seconds match
const int ALRM1_MATCH_HR_MIN_SEC 0b1000  // when hours, minutes, and seconds match
const int ALRM1_MATCH_HR_MIN_SEC 0b0000  // when hours, minutes, and seconds match
    byte ALRM1_SET = ALRM1_MATCH_HR_MIN_SEC;

const int ALRM2_ONCE_PER_MIN     0b111   // once per minute (00 seconds of every minute)
const int ALRM2_MATCH_MIN        0b110   // when minutes match
const int ALRM2_MATCH_HR_MIN     0b100   // when hours and minutes match
const int ALRM2_DATE_TIME        0b000   // when hours and minutes match
    byte ALRM2_SET = ALRM2_DISABLE;

      // Set AlarmBits, ALRM2 first, followed by ALRM1
      AlarmBits = ALRM2_SET;
      AlarmBits <<= 4;
      AlarmBits |= ALRM1_SET;
*/

    RTCon();

    bool Century, h12, A12h =false;
    bool ADy = true;
    bool PM, Apm;

    byte AlarmBits = 0b00000000;
    Clock.turnOffAlarm(1); //Turn off alarms before setting.
    Clock.turnOffAlarm(2);

    Clock.checkIfAlarm(1); //Clear alarm flags, do I need to do this here?
    Clock.checkIfAlarm(2); //Clear alarm flags

    Clock.setA1Time(_days, _hours, _minutes, _seconds, AlarmBits, true, false, false);
    delay(2);
    int _days_backup = _days;
    int _hours_backup = _hours;
    int _minutes_backup = _minutes+2;
  
    if(_minutes_backup > 59){_minutes_backup = _minutes_backup - 60; _hours_backup++;}
    if(_hours_backup > 23){_hours_backup = _hours_backup - 24; _days_backup++;}
    if(_days_backup > 7){_days_backup = _days_backup - 7;} 
    Clock.setA2Time(_days_backup, _hours_backup, _minutes_backup, AlarmBits, true, false, false);  //setting as backup wake function
    delay(2);
    Clock.turnOnAlarm(1); //Turn on alarms.
    delay(1);
    Clock.turnOnAlarm(2);
    delay(1);

//displayAlarms(); delay(10);  //Uncomment to see alarms on serial monitor.

}


void Logger::displayAlarms(){
  bool ADy, A12h, Apm;
  byte ADay, AHour, AMinute, ASecond, AlarmBits;
  Serial.print("Alarm 1: ");
	Clock.getA1Time(ADay, AHour, AMinute, ASecond, AlarmBits, ADy, A12h, Apm);
	Serial.print(ADay, DEC);
	if (ADy) {
		Serial.print(" DoW");
	} else {
		Serial.print(" Date");
	}
	Serial.print(' ');
	Serial.print(AHour, DEC);
	Serial.print(' ');
	Serial.print(AMinute, DEC);
	Serial.print(' ');
	Serial.print(ASecond, DEC);
	Serial.print(' ');
	if (A12h) {
		if (Apm) {
			Serial.print("pm");
		} else {
			Serial.print("am");
		}
	}
	if (Clock.checkAlarmEnabled(1)){
		Serial.print("enabled");
  } else{
    Serial.print("not enabled");
    }
	
	Serial.print('\n');
	// Display Alarm 2 information
	Serial.print("Alarm 2: ");
	Clock.getA2Time(ADay, AHour, AMinute, AlarmBits, ADy, A12h, Apm);
	Serial.print(ADay, DEC);
	if (ADy){
		Serial.print(" DoW");
	} 
    else{
    Serial.print(" Date");
	  } 
	Serial.print(' ');
	Serial.print(AHour, DEC);
	Serial.print(' ');
	Serial.print(AMinute, DEC);
	Serial.print(' ');
	if (A12h) {
		if (Apm) {
			Serial.print("pm ");
		} else {
			Serial.print("am ");
		}
	}
	if (Clock.checkAlarmEnabled(2)) {
		Serial.print("enabled");
	} else{
    Serial.print("not enabled");
  }
	// display alarm bits
//	Serial.print('\n');
//	Serial.println(AlarmBits, BIN);
//  Serial.println(Clock.getSecond());

checkTime();
}

void Logger::checkAlarms(){
	Clock.checkIfAlarm(1);
	
	if (_use_sleep_mode){
	  if (Clock.checkIfAlarm(2)) {
		  Serial.println("Alarm missed! Reset logger.");
      datafile.close();
      delay(30);

      if (!sd.begin(CSpin, SPI_HALF_SPEED)) {
        Serial.println(F("Error initializing SD card for writing"));
      }
      start_logging_to_otherfile("Alarm_miss.txt");

      bool ADy, A12h, Apm;
      byte ADay, AHour, AMinute, ASecond, AlarmBits;
      otherfile.print(now.unixtime());
      otherfile.print(",");
      otherfile.print("Alarm: ");
	    Clock.getA1Time(ADay, AHour, AMinute, ASecond, AlarmBits, ADy, A12h, Apm);
	    otherfile.print(ADay, DEC);
	    otherfile.print(" DoW");
	    otherfile.print(' ');
	    otherfile.print(AHour, DEC);
	    otherfile.print(' ');
	    otherfile.print(AMinute, DEC);
	    otherfile.print(' ');
	    otherfile.print(ASecond, DEC);
	    otherfile.print(' ');
	    if (A12h) {
		    if (Apm) {
			    otherfile.print("PM");
		    }
		    else {
			    otherfile.print("AM");
		    }
	    }
      end_logging_to_otherfile();
      delay(20000); // Wait until WDT resets logger (<= 8 seconds)
	  }
  }
}

void Logger::checkTime(){
//Get current time:
bool Century=false;
bool h12 = false;
bool PM;
  Serial.print("GMT DATE/TIME: "); delay(5);
	Serial.print(Clock.getYear(), DEC);
	Serial.print(' ');
	// then the month
	Serial.print(Clock.getMonth(Century), DEC);
	Serial.print(' ');
	// then the date
	Serial.print(Clock.getDate(), DEC);
	Serial.print(' ');
	// and the day of the week
	//Serial.print(Clock.getDoW(), DEC);
	//Serial.print(' ');
	// Finally the hour, minute, and second
	Serial.print(Clock.getHour(h12, PM), DEC);
	Serial.print(' ');
	Serial.print(Clock.getMinute(), DEC);
	Serial.print(' ');
	Serial.print(Clock.getSecond(), DEC);
	// Add AM/PM indicator
	if (h12) {
		if (PM) {
			Serial.println(" PM ");
		} else {
			Serial.println(" AM ");
		}
	} else {
		Serial.println(" 24h GMT");
	}
delay(2);
}

  void Logger::LEDwarn(int nflash)
  {
    // Flash LED quickly to say that the SD card (and therefore the logger)
    // has not properly initialized upon restart
    for(int i=0;i<=nflash;i++){
      digitalWrite(LEDpin,HIGH);
      delay(50);
      digitalWrite(LEDpin,LOW);
      delay(50);
    }
  }

  void Logger::LEDgood()
  {
    // Peppy blinky pattern to show that the logger has successfully initialized
    digitalWrite(LEDpin,HIGH);
    delay(1000);
    digitalWrite(LEDpin,LOW);
    delay(300);
    digitalWrite(LEDpin,HIGH);
    delay(100);
    digitalWrite(LEDpin,LOW);
    delay(100);
    digitalWrite(LEDpin,HIGH);
    delay(100);
    digitalWrite(LEDpin,LOW);
  }

  void Logger::LEDtimeWrong(int ncycles)
  {
    // Syncopated pattern to show that the clock has probably reset to January
    // 1st, 2000
    for(int i=0;i<=ncycles;i++)
    {
      digitalWrite(LEDpin,HIGH);
      delay(250);
      digitalWrite(LEDpin,LOW);
      delay(100);
      digitalWrite(LEDpin,HIGH);
      delay(100);
      digitalWrite(LEDpin,LOW);
      delay(100);
    }
  }

  void Logger::unixDatestamp(){

    now = RTC.now();
    datafile.print(now.unixtime());
    datafile.print(F(","));

    // Echo to serial
    Serial.print(now.unixtime());
    Serial.print(F(","));
  }

  void Logger::endLine(){
    // Ends the line in the file; do this at end of recording instance
    // before going back to sleep
    datafile.println();
    Serial.println();
    }

float Logger::_vdivR(int pin, float Rref, uint8_t adc_bits, \
                     bool Rref_on_GND_side, bool oversample_debug){
  // Same as public vidvR code, but returns value instead of 
  // saving it to a file
  float _ADC;
  float _R;
  _ADC = analogReadOversample_Debug(pin, adc_bits, 1, oversample_debug);
  float _ADCnorm = _ADC/1023.0; // Normalize to 0-1
  if(Rref_on_GND_side){
    // Standard case for the provided slots for reference resistors
    // This is the default.
    _R = Rref/_ADCnorm - Rref; // R1 = (R2*Vin)/Vout - R2
  }
  else {
    // This could happen if an external sensor has a different setup for
    // its known and unknown resistors; in this case, place the reference
    // resistor between the analog pin and 3V3. (The sensor, internally, 
    // has its thermistor connected to GND.)
    _R = Rref * (1. / ((1./_ADCnorm) - 1.)); // R2 = R1* (1 / ((Vin/Vout) - 1))
  }
  return _R;
}

void Logger::RTCon(){
  // Turn on power clock
  pinMode(SDpowerPin,OUTPUT);
  digitalWrite(SDpowerPin,HIGH); //Chad -- one model's pull-ups attached to SDpowerPin
  digitalWrite(ClockPowerPin,HIGH);
  delay(2);
}

void Logger::RTCsleep(){
  // Turn off power clock
  // At this point, it runs on VCC (if logger is powered... which it is
  // if this program is running) via the backup battery power supply.
  // This "tricks" it into turning off its I2C bus and saves power on the
  // board, but keeps its alarm functionality on.
  // (Idea to do this courtesy of Gerhard Oberforcher)
  digitalWrite(SDpowerPin,LOW); //Chad -- one model's pull-ups attached to SDpowerPin 
  digitalWrite(ClockPowerPin,LOW);
  delay(2);
}

void Logger::SDpowerOn(){
  digitalWrite(SDpowerPin,HIGH);
}

void Logger::SDpowerOff(){
  digitalWrite(SDpowerPin,LOW);
}

////////////////////////////////////////////////////////////
// PUBLIC UTILITY FUNCTIONS TO IMPLEMENT LOGGER IN SKETCH //
////////////////////////////////////////////////////////////


/*void Logger::sleep(){
  // Maintain backwards compatibility with code that requires "log_minutes"
  // to be defined separately from initialize step. (A bad idea, right? Will
  // remove this compatibility once it seems to no longer be a problem.)
  sleep(log_minutes);
}*/

void Logger::sleep(){

  IS_LOGGING = false; // not logging when sleeping!

  wdt_disable();  //Disable the watchdog timer

  sleepNow();
}

void Logger::startLogging(){
  // Wake up

  // Set the WDCE bit (bit 4) and the WDE bit (bit 3) 
  // of WDTCSR. The WDCE bit must be set in order to 
  // change WDE or the watchdog prescalers. Setting the 
  // WDCE bit will allow updtaes to the prescalers and 
  // WDE for 4 clock cycles then it will be reset by 
  // hardware.
  WDTCSR = WDTCSR | B00011000; 

  // Set the watchdog timeout prescaler value to 1024 K 
  // which will yeild a time-out interval of about 8.0 s.
  WDTCSR = B00100001;

  // Enable the watchdog timer interupt.
  WDTCSR = WDTCSR | B01000000;
  MCUSR = MCUSR & B11110111;
  
  // Turn power on
  SDpowerOn();
  RTCon();

  checkAlarms(); //Check and clear flag 

  // First, check if there was a bucket tip from the rain gage, if present
  if (NEW_RAIN_BUCKET_TIP){
    TippingBucketRainGage();
    if (LOG_ON_BUCKET_TIP){
      // If we want data recorded when the bucket tips, we don't want it to 
      // interrupt a current logging step.
      // And IS_LOGGING will just stay true if we're interrupting that.
      IS_LOGGING = true; // First switch the IS_LOGGING flag because we're
                         // doing it now.
    }
  }
  // Then check if a logging event is supposed to occur
  // that is not part of a new bucket tip
  else if (IS_LOGGING){
    // Check if the logger has been awakend by someone pushing the button
    // If so, bypass everything else
    if (_model == bottle_logger && (digitalRead(manualWakePin) == LOW)){
      // Brief light flash to show that logging is happening
      digitalWrite(LEDpin, HIGH);
      delay(5); // to make sure tips aren't double-counted
      digitalWrite(LEDpin, LOW);
    }
  }

  pinMode(SDpowerPin,OUTPUT); // Seemed to have forgotten between loops... ?

  // Initialize logger
  if (!sd.begin(CSpin, SPI_HALF_SPEED)) {
    // Just use Serial.println: don't kill batteries by aborting code 
    // on error
    Serial.println(F("Error initializing SD card for writing"));
  }
  delay(10);
  // Datestamp the start of the line
  unixDatestamp();
}

void Logger::endLogging(){
  // Ends line, turns of SD card, and resets alarm: ready to sleep
  endLine();
  //SDpowerOn();
  // close the file: (This does the actual sync() step too - writes buffer)
  datafile.sync();
  delay(30);
  // THIS DELAY IS ***CRITICAL*** -- WITHOUT IT, THERE IS NOT SUFFICIENT
  // TIME TO WRITE THE DATA TO THE SD CARD!
  // Check right before going back to sleep if there has been a rain
  // gauge bucket tip while it has been on
  // This is a temporary solution!
  // (May be able to reduce delay if not going back to sleep -- i.e., write
  //  to card while logging next step.)
  if (NEW_RAIN_BUCKET_TIP){
    TippingBucketRainGage();
  }

  if (_use_sleep_mode){
    //Calculate for next alarm
    _days = _days+dayInterval;
    _hours = _hours+hourInterval;
    _minutes = _minutes+minInterval;
    _seconds = _seconds+secInterval;   
    if(_seconds > 59){_seconds = _seconds - 60; _minutes++;}
    if(_minutes > 59){_minutes = _minutes - 60; _hours++;}
    if(_hours > 23){_hours = _hours - 24; _days++;}
    if(_days > 7){_days = _days - 7;} 

    alarm(_days, _hours, _minutes, _seconds);  //Set new alarms.

    delay(2);
    RTCsleep();
    delay(2);
  }
  // After this step, since everything is in the loop() part of the Arduino
  // sketch, the sketch will cycle back back to sleep(...)
}

void Logger::startAnalog(){
  // Turn on power to analog sensors
  digitalWrite(SensorPowerPin,HIGH);
  sbi(ADCSRA,ADEN);        // switch Analog to Digitalconverter ON
  delay(2);
}

void Logger::endAnalog(){
  // Turn off power to analog sensors
  digitalWrite(SensorPowerPin,LOW);
  delay(2);
}

////////////////////////////////
// SENSOR INTERFACE FUNCTIONS //
////////////////////////////////

// Read analog pin
//////////////////////////////

float Logger::readPin(int pin){

  /**
   * This function returns the analog to digital converter value (0 - 1023). 
   * Results are displayed on the serial monitor and saved onto the SD card.
   * 
   * \b pin is the analog pin number to be read.
   * 
   * Example:
   * ```
   * logger.readPin(2);
   * ```
   * 
   * 
  */

  float pinValue = analogRead(pin);

  ///////////////
  // SAVE DATA //
  ///////////////

  // SD write
  datafile.print(pinValue, 1);
  datafile.print(",");
  
  // Echo to serial
  Serial.print(pinValue, 1);
  Serial.print(",");

  return pinValue;

}

float Logger::readPinOversample(int pin, int bits){

  /**
   * This function incorporates oversampling to extend the ADC precision
   * past ten bits by taking more readings and statistically combing them.
   * Results are displayed on the serial monitor and saved onto the SD card.
   * 
   * \b pin is the analog pin number to be read.
   * 
   * \b adc_bits is the reading precision in bits (2^adc_bits).
   * The ATMega328 (Arduino Uno and ALog BottleLogger core chip)
   * has a base ADC precision of 10 bits (returns values of 0-1023)
   * A reasonable maximum precision gain is (base_value_bits)+6, so
   * 16 bits is a reasonable maximum precision for the ALog BottleLogger.
   * 
   * Example:
   * ```
   * logger.readPinOversample(2, 12);
   * ```
   * 
   * Output values will range from 0-1023, but be floating-point.
   * 
   * Readings that require more bits of precision will take longer.
   * 
   * 
  */

  float pinValue = analogReadOversample(pin, bits);

  ///////////////
  // SAVE DATA //
  ///////////////

  // SD write
  datafile.print(pinValue,4);
  datafile.print(",");
  
  // Echo to serial
  Serial.print(pinValue,4);
  Serial.print(",");

  return pinValue;

}

// Thermistor - with b-value
//////////////////////////////

float Logger::thermistorB(float R0, float B, float Rref, float T0degC, \
                          int thermPin, uint8_t ADC_resolution_nbits,
                          bool Rref_on_GND_side, bool oversample_debug){

  /**
   * This function measures temperature using a thermistor characterised with 
   * the B (or β) parameter equation, which is a simplification of the 
   * Steinhart-Hart equation
   * 
   * The function compares the thermistor risistance with the reference 
   * resistor using a voltage divider.
   * 
   * It returns a float of the temperature in degrees celsius.
   * Results are displayed on the serial monitor and saved onto the SD 
   * card to four decimal places.
   * 
   * \b R0 is the resistance of the thermistor at the known temperature
   * \b T0degC
   * 
   * \b B is the β parameter of the thermistor
   * 
   * \b Rref is the resistance of the corresponding reference resistor for \
   * the analog pin set by \b ThermPin (below).
   * 
   * \b T0degC is the temperature at which \b R0 was calibrated.
   * 
   * \b thermPin is the analog pin number to be read.
   * 
   * \b ADC_resolution_nbits (10-16 for the ALog BottleLogger) is the 
   * number of bits of ADC resolution used (oversampling for >10)   * 
   * 
   * \b Rref_on_GND-Side indicates the configuration of the voltage divider.  
   * True if using Alog provided Reference resistor terminals. If false, 
   * the reference resitor must be instead connected via the screw terminals.
   * This is set true for external sensors that are built to require a
   * VCC-side reference resistor.
   * 
   * \b oversample_debug is true if you want a separate file, "Oversample.txt", 
   * to record every individual reading used in the oversampling.
   * 
   * Example:
   * ```
   * // Contherm from Digikey, 14-bit precision
   * logger.thermistorB(10000, 3950, 30000, 25, 2, 14);
   * // EPCOS, DigiKey # 495-2153-ND, 14-bit precision
   * logger.thermistorB(10000, 3988, 13320, 25, 1, 14);
   * ```
   * 
  */

  // Voltage divider
  float Rtherm = _vdivR(thermPin, Rref, ADC_resolution_nbits, \
                        Rref_on_GND_side, oversample_debug);
  }
   * "Debug" in this function name means that there is a file called
   * "Oversample.txt" that contains all of the values read during the
   * oversampling.
  
  // B-value thermistor equations
  float T0 = T0degC + 273.15;
  float Rinf = R0*exp(-B/T0);
  float T = B / log(Rtherm/Rinf);
  
  // Convert to celsius
  T = T - 273.15;
  
  ///////////////
  // SAVE DATA //
  ///////////////

  // SD write
  datafile.print(T, 4);
  datafile.print(F(","));
  
  // Echo to serial
  Serial.print(T, 4);
  Serial.print(F(","));

  return T;

}

// HTM2500LF Humidity and Temperature Sensor
// by TE Connectivity Measurement Specialties
///////////////////////////////////////////////

void Logger::HTM2500LF_humidity_temperature(int humidPin, int thermPin, \
                                float Rref_therm, uint8_t ADC_resolution_nbits){

  /**
   * This function measures the relative humidity of using a HTM2500
   * tempurature and relative humidity module.
   * The relative humidity and temperature is measured using a 14 bit
   * oversampling method.
   * Results are displayed on the serial monitor and saved onto the SD 
   * card to four decimal places.
   * 
   * \b humidPin is the analog pin connected to the humidity output voltage 
   * of the module.
   * 
   * \b thermPin is the analog pin connected to the tempurature output voltage 
   * of the module.
   * 
   * \b Rref_therm is the value of the reference resistor that you use
   * with the built-in thermistor (reference resistor supplied separately,
   * placed in appropriate slot in header)
   *
   * \b ADC_resolution_nbits (10-16 for the ALog BottleLogger) is the 
   * number of bits of ADC resolution used (oversampling for >10)
   * 
   * Example:
   * ```
   * logger.HTM2500LF_humidity_temperature(1, 2, ?);
   * ```
   *
   * This function is designed for ratiometric operation -- that is, the 
   * humidity sensor must be powered by the same voltage regulator that is
   * connected to the the analog reference pin -- for the ALog v2.0, this is
   * a high-precision 3V3 regulator.
  */

  // First, measure these pins
  // This will fully calculate and write the temperature data, too.
  // Normalized to 0-1 range
  float V_humid_norm = analogReadOversample(humidPin, ADC_resolution_nbits) \
                                                                 /1023.; // 0-1
  // Commenting out low and high temperature readings;
  // can calculate these after the fact, if needed
  // float Tmin = thermistorB(10000, 3347, Rref_therm, 25, thermPin, false);
  float Ttyp = thermistorB(10000, 3347, Rref_therm, 25, thermPin, false);
  //float Tmax = thermistorB(10000, 3413, Rref_therm, 25, thermPin, false);
  // Then, convert the normalized voltage into a humidity reading
  // The calibration is created for a 5V input, but the data sheet says it
  // is ratiometric, so I think I will just renormalize the voltage to
  // pretend that it is 5V input in order to get the right input values
  // for the equation. Just multiply by 5, and then I can use the equation
  // that is designed for 5V!
  
  // T error is small, and has a small effect on humidity -- much smaller 
  // than published error (see data sheet) -- maybe eventually code error
  // into this function. So just use typical thermistor values.
  float Vh = 5000 * V_humid_norm; // mV
  //float Vh_real = 3300 * V_humid_norm; // switching 3.3V basis
  
  // RH in percent
  // Must use the pow(base, int) function or do multiplication the long way...
  float RH = ( (-1.9206E-9 * Vh*Vh*Vh) + (1.437E-5 * Vh*Vh) + \
               (3.421E-3 * Vh) - 12.4 ) \
             / (1 + (Ttyp - 23) * 2.4E-3);
  
  ///////////////
  // SAVE DATA //
  ///////////////

  // SD write
  datafile.print(RH, 4);
  datafile.print(F(","));
  
  // Echo to serial
  Serial.print(RH, 4);
  Serial.print(F(","));

}


// HTM1500LF Humidity and Temperature Sensor
// by TE Connectivity Measurement Specialties
///////////////////////////////////////////////

void Logger::HM1500LF_humidity_with_external_temperature(int humidPin, \
             float R0_therm, float B_therm, float Rref_therm, \
             float T0degC_therm, int thermPin_therm, \
             uint8_t ADC_resolution_nbits){

  /**
   * This function measures the relative humidity of using a HTM1500 relative 
   * humidity sensor and an external thermistor.
   * The relative humidity and temperature is measured using a 14 bit 
   * oversampling method.
   * Results are displayed on the serial monitor and saved onto the SD card 
   * to four decimal places.
   * 
   * \b humidPin is the analog pin connected to the humidity output voltage 
   * of the module.
   * 
   * \b R0 is a thermistor calibration.
   * 
   * \b B is the β parameter of the thermistor.
   * 
   * \b Rref is the resistance of the corresponding reference resistor for 
   * that analog pin.
   * 
   * \b T0degC is a thermistor calibration.
   * 
   * \b thermPin is the analog pin connected to the tempurature output voltage 
   * of the module.
   * 
   * \b ADC_resolution_nbits (10-16 for the ALog BottleLogger) is the 
   * number of bits of ADC resolution used (oversampling for >10)
   * 
   * Example:
   * ```
   * logger.HTM2500LF_humidity_temperature(1,?,10000,3950,10000,25,2);
   * ```
   * 
  */

  // First, measure these pins
  // This will fully calculate and write the temperature data, too.
  float V_humid_norm = analogReadOversample(humidPin, 14)/1023.; // 0-1
  // Will write temperature to file here
  float T = thermistorB(R0, B, Rref, T0degC, thermPin);

  // Then, convert the normalized voltage into a humidity reading
  // The calibration is created for a 5V input, but the data sheet says it
  // is ratiometric, so I think I will just renormalize the voltage to
  // pretend that it is 5V input in order to get the right input values
  // for the equation. Just multiply by 5!
  
  // T error is small, and has a small effect on humidity -- much smaller 
  // than published error (see data sheet) -- maybe eventually code error
  // into this function. So just use typical thermistor values.
  float Vh = 5000 * V_humid_norm; // mV
  //float Vh_real = 3300 * V_humid_norm; // switching 3.3V basis
  
  // RH in percent
  // Got to use the pow(base, int) function or do multiplication the long way...
  float RH_no_T_corr = (-1.91E-9 * Vh*Vh*Vh) + \
                       (1.33E-5 * Vh*Vh) + (9.56E-3 * Vh);
  float RH = RH_no_T_corr + 0.05 * (T - 23);
  
  ///////////////
  // SAVE DATA //
  ///////////////

  // Print normalized 0-1 voltage in case my 5V conversion doesn't work the way
  // I think it will -- though if it is ratiometric, I think it should.
 
  // SD write
  //datafile.print(V_humid_norm);
  //datafile.print(F(","));
  datafile.print(RH, 4);
  datafile.print(F(","));
  
  // Echo to serial
  //Serial.print(V_humid_norm);
  //Serial.print(F(","));
  Serial.print(RH, 4);
  Serial.print(F(","));

}


// MaxBotix ruggedized standard size ultrasonic rangefinder: 
// 1 cm = 1 10-bit ADC interval
//////////////////////////////////////////////////////////////

void Logger::ultrasonicMB_analog_1cm(int nping, int EX, int sonicPin, bool writeAll){

  /**
   * This function measures the distance between the ultrasonic sensor and an 
   * acustically reflective surface, typically water or snow.
   * Measures distance in centimeters.
   * Results are displayed on the serial monitor and saved onto the SD card.
   *
   * This is for the older MaxBotix sensors, whose maximum precision is
   * in centimeters.
   * 
   * \b nping is the number of range readings to take (number of pings).  
   * The mean range will be calculated and output to the serial monitor and 
   * SD card followed by the standard deviation.
   * 
   * \b EX is a digital output pin used for an excitation pulse.  If maxbotix 
   * sensor is continuously powered a reading will be taken when this pin is 
   * flashed high.
   * Set to '99' if excitation pulse is not needed.
   * 
   * \b sonicPin is the analog input channel hooked up to the maxbotix sensor.
   * 
   * \b writeAll will write each reading of the sensor (each ping) to the 
   * serial monitor and SD card.
   * 
   * Example:
   * ```
   * logger.ultrasonicMB_analog_1cm(10, 99, 2, 0);
   * ```
   * Note that sensor should be mounted away from supporting structure.
   * For a mast that is 5 meters high (or higher) the sensor should be 
   * mounted at least 100cm away from the mast.
   * For a mast that is 2.5 meters high (or lower) the sensor should be 
   * at least 75cm away from the mast.
  */
  
  float range; // The most recent returned range
  float ranges[nping]; // Array of returned ranges
  float sumRange = 0; // The sum of the ranges measured
  float meanRange; // The average range over all the pings

//  Serial.println();
  // Get range measurements
  // Get rid of any trash; Serial.flush() unnecessary; main thing that 
  // is important is getting the 2 pings of junk out of the way
  Serial.flush();
  for (int i=1; i<=2; i++){
    if(EX != 99){
      digitalWrite(EX,HIGH);
        delay(1);
      digitalWrite(EX,LOW);
      }
    delay(100);
    }
  for(int i=1;i<=nping;i++){
    if(EX != 99){
      digitalWrite(EX,HIGH);
        delay(1);
      digitalWrite(EX,LOW);
      }
    delay(100);
    range = analogRead(sonicPin);
    ranges[i-1] = range; // 10-bit ADC value = range in cm
                         // C is 0-indexed, hence the "-1"
    if (writeAll){
      Serial.print(range);
      Serial.print(F(","));
      //SDpowerOn();
      datafile.print(range);
      datafile.print(F(","));
      //SDpowerOff();
    }
  sumRange += range;
  }
 
  // Find mean of range measurements from sumRange and nping
  meanRange = sumRange/nping;
  
  // Find standard deviation
  float sumsquares = 0;
  float sigma;
  for(int i=0;i<nping;i++){
    // Sum the squares of the differences from the mean
    sumsquares += square(ranges[i]-meanRange);
  }
  // Calculate stdev
  sigma = sqrt(sumsquares/nping);
    
  ///////////////
  // SAVE DATA //
  ///////////////
  delay(10);
  datafile.print(meanRange);
  datafile.print(F(","));
  datafile.print(sigma);
  datafile.print(F(","));

  // Echo to serial
  Serial.print(meanRange);
  Serial.print(F(","));
  Serial.print(sigma);
  Serial.print(F(","));

}

void Logger::maxbotixHRXL_WR_analog(int nping, int sonicPin, int EX, \
                                    bool writeAll \
                                    uint8_t ADC_resolution_nbits){

  // Returns distance in mm, +/- 5 mm
  // Each 10-bit ADC increment corresponds to 5 mm.
  // set EX=99 if you don't need it (or leave it clear -- this is default)
  // Default nping=10 and sonicPin=A0
  // Basically only differs from older MB sensor function in its range scaling
  // and its added defaults.
  
  /**
   * This function measures the distance between the ultrasonic sensor and an \
   * acoustically-reflective surface, typically water or snow.
   * Measures distance in milimeters.
   * Results are displayed on the serial monitor and saved onto the SD card.
   * 
   * \b nping is the number of range readings to take (number of pings).  
   * The mean range will be calculated and output to the serial monitor 
   * and SD card followed by the standard deviation.
   * 
   * \b sonicPin is the analog input channel hooked up to the maxbotix sensor.
   * 
   * \b EX is a digital output pin used for an excitation pulse.  *
   * If maxbotix sensor is continuously powered, a reading will be taken when 
   * this pin is flashed high. Set to '99' if excitation pulse is not needed.
   * 
   * \b writeAll will write each reading of the sensor (each ping) 
   * to the serial monitor and SD card.
   * 
   * \b ADC_resolution_nbits (10-16 for the ALog BottleLogger) is the 
   * number of bits of ADC resolution used (oversampling for >10)   * 
   * 
   * Example:
   * ```
   * logger.ultrasonicMB_analog_1cm(10, 99, 2, 0);
   * ```
   * Note that sensor should be mounted away from supporting structure.
   * * For a mast that is 5 meters high (or higher) the sensor should be 
   *   mounted at least 100cm away from the mast.
   * * For a mast that is 2.5 meters high (or lower) the sensor should be at 
   *   least 75cm away from the mast.
   * However, in our tests, the sensors with filtering algorithms function
   * perfectly well even close to the mask, and this increases rigidity of
   * the installation.
   *
  */

  float range; // The most recent returned range
  float ranges[nping]; // Array of returned ranges
  float sumRange = 0; // The sum of the ranges measured
  float meanRange; // The average range over all the pings
  int sp; // analog reading of sonic pin; probably unnecessary, but Arduino warns against having too many fcns w/ artihmetic, I think

  // Get range measurements
  // Get rid of any trash; Serial.flush() unnecessary; main thing that is important is
  // getting the 2 pings of junk out of the way
  Serial.flush();

  for (int i=1; i<=2; i++){
    if(EX != 99){
      digitalWrite(EX,HIGH);
        delay(1);
      digitalWrite(EX,LOW);
      }
    delay(100);
    }
  sp = analogRead(sonicPin);
  for(int i=1;i<=nping;i++){
    if(EX != 99){
      digitalWrite(EX,HIGH);
        delay(1);
      digitalWrite(EX,LOW);
      }
    delay(100);
    sp = analogRead(sonicPin);
    range = (sp + 1) * 5;
    ranges[i-1] = range; // 10-bit ADC value (1--1024) * 5 = range in mm
                         // C is 0-indexed, hence the "-1"
    if (writeAll){
      Serial.print(range, 0);
      Serial.print(F(","));
      //SDpowerOn();
      datafile.print(range, 0);
      datafile.print(F(","));
      //SDpowerOff();
    }  
  sumRange += range;
  }
 
  // Find mean of range measurements from sumRange and nping
  meanRange = sumRange/nping;
  
  // Find standard deviation
  float sumsquares = 0;
  float sigma;
  for(int i=0;i<nping;i++){
    // Sum the squares of the differences from the mean
    sumsquares += square(ranges[i]-meanRange);
  }
  // Calculate stdev
  sigma = sqrt(sumsquares/nping);

  ///////////////
  // SAVE DATA //
  ///////////////
  datafile.print(meanRange);
  datafile.print(F(","));
  datafile.print(sigma);
  datafile.print(F(","));

  // Echo to serial
  Serial.print(meanRange);
  Serial.print(F(","));
  Serial.print(sigma);
  Serial.print(F(","));

}

float Logger::maxbotixHRXL_WR_Serial(int Ex, int Rx, int npings, bool writeAll, int maxRange, bool RS232){
  // Stores the ranging output from the MaxBotix sensor
  int myranges[npings]; // Declare an array to store the ranges [mm] // Should be int, but float for passing to fcns
  // Get nodata value - 5000 or 9999 based on logger max range (in meters)
  // I have also made 0 a nodata value, because it appears sometimes and shouldn't
  // (minimum range = 300 mm)
  int nodata_value;
  if (maxRange == 5){ 
    nodata_value = 5000;
  }
  else if (maxRange == 10){
    nodata_value = 9999;
  }
  // Put all of the range values in the array 
  for (int i=0; i<npings; i++){
    // myranges[i] = maxbotix_soft_Serial_parse(Ex, Rx, RS232);
    myranges[i] = maxbotix_Serial_parse(Ex);
  }
  // Then get the mean and standard deviation of all of the data
  int npings_with_nodata_returns = 0;
  unsigned long sum_of_good_ranges = 0;
  int good_ranges[npings];
  int j=0;
  for (int i=0; i<npings; i++){
    if (myranges[i] != nodata_value && myranges[i] != 0){
      sum_of_good_ranges += myranges[i];
      good_ranges[j] = myranges[i];
      j++;
    }
    else{
      npings_with_nodata_returns ++;
    }
  }
  float npings_with_real_returns = npings - npings_with_nodata_returns;
  float mean_range;
  float standard_deviation;
  // Avoid div0 errors
  if (npings_with_real_returns > 0){
    mean_range = sum_of_good_ranges / npings_with_real_returns;
    standard_deviation = standard_deviation_from_array(good_ranges, npings_with_real_returns, mean_range);
  }
  else {
    mean_range = -9999;
    standard_deviation = -9999;
  }
  // Write all values if so desired
  if (writeAll){
    for (int i=0; i<npings; i++){
      datafile.print(myranges[i]);
      datafile.print(F(","));
      // Echo to serial
      Serial.print(myranges[i]);
      Serial.print(F(","));
    }

  }
  // Always write the mean, standard deviation, and number of good returns
  datafile.print(mean_range);
  datafile.print(F(","));
  datafile.print(standard_deviation);
  datafile.print(F(","));
  datafile.print(npings_with_real_returns);
  datafile.print(F(","));

  // Echo to serial
  Serial.print(mean_range);
  Serial.print(F(","));
  Serial.print(standard_deviation);
  Serial.print(F(","));
  Serial.print(npings_with_real_returns);
  Serial.print(F(","));
  
  // return mean range for functions that need it, e.g., to trigger camera
  return mean_range;
}

float Logger::standard_deviation_from_array(float values[], int nvalues, float mean){
  float sumsquares = 0;
  for (int i=0; i<nvalues; i++){
    sumsquares += square(values[i] - mean);
  }
  return sqrt(sumsquares/nvalues);
}

float Logger::standard_deviation_from_array(int values[], int nvalues, float mean){
  float sumsquares = 0;
  for (int i=0; i<nvalues; i++){
    sumsquares += square(values[i] - mean);
  }
  return sqrt(sumsquares/nvalues);
}

int Logger::maxbotix_Serial_parse(int Ex){
  // Excites the MaxBotix sensor and receives its ranging output
  char range[7]; // R####<\r>, so R + 4 chars + carriage return + null
  Serial.end(); // End 38400 bps computer comms
  Serial.begin(9600); // Start 9600 bps logger comms
  //Excite the sensor to produce a pulse
  pinMode(Ex, OUTPUT);
  digitalWrite(Ex, HIGH);
  delay(1);
  digitalWrite(Ex, LOW);
  // Record the result of the ranging
  int i=0; // counter
  // Not sure if this will work - maybe loop around to the other end of the array?
  while (range[i-1] != 13){
    if (Serial.available()){
      range[i] = Serial.read();
      i++;
    }
  }
  Serial.end();
  Serial.begin(38400);
  // Convert to integer
  char r2[4]; // range stripped of all of its extra characters
  for (int i=1; i<5; i++){
    r2[i-1] = range[i];
  }
  int r3 = atol(r2);
  return r3;
}

// CURRENTLY NOT USED -- ADW DOES NOT TRUST SOFTWARE SERIAL TO MAINTAIN
// TIMING (AND NOT HANG)
int Logger::maxbotix_soft_Serial_parse(int Ex, int Rx, bool RS232){
  // Excites the MaxBotix sensor and receives its ranging output
  char range[7]; // R####<\r>, so R + 4 chars + carriage return + null
  SoftwareSerial mySerial(Rx, -1, RS232); // RX, TX, inverse logic - RS232 true, TTL false; defaults to TTL (false)
  mySerial.begin(9600);
  //Excite the sensor to produce a pulse
  pinMode(Ex, OUTPUT);
  digitalWrite(Ex, HIGH);
  delay(1);
  digitalWrite(Ex, LOW);
  // Record the result of the ranging
  int i=0; // counter
  // Not sure if this will work - maybe loop around to the other end of the array?
  while (range[i-1] != 13){
    if (mySerial.available()){
      range[i] = mySerial.read();
      i++;
    }
  }
  //Serial.print(range);
  // Convert to integer
  char r2[4]; // range stripped of all of its extra characters
  for (int i=1; i<5; i++){
    r2[i-1] = range[i];
  }
  int r3 = atol(r2);
  //Serial.print(", ");
  //Serial.print(r3);
  //Serial.print("; ");
  return r3;
  //return atol(r2); // Return integer values in mm; no parsing of error values
}

void Logger::Inclinometer_SCA100T_D02_analog_Tcorr(int xPin, int yPin, \
                                                   float V_ADC, float VDD, \
                                                   float R0, float B, \
                                                   float Rref, float T0degC, \
                                                   int thermPin, \
                                                   float ADC_resolution_nbits){
  // +/- 90 degree inclinometer, measures +/- 1.0g
  // Needs 4.75--5.25V input
  // This function works with the analog outputs
  // Turned on and off by a switching 5V charge pump or boost converter
  
  float Vout_x = (analogReadOversample(xPin, ADC_resolution_nbits) / 1023.) * V_ADC;
  float Vout_y = (analogReadOversample(yPin, ADC_resolution_nbits) / 1023.) * V_ADC;
  
  float Offset = VDD/2.;
  float Sensitivity = 2.;

  // Temperature correction
  float T = thermistorB(R0, B, Rref, T0degC, thermPin);
  // Sensitivity correction for Scorr
  float Scorr = -0.00011 * T*T + 0.0022 * T + 0.0408;
  
  float Sensitivity_compensated = Sensitivity * ( 1 + Scorr/100.);
  
  float angle_x_radians = asin( (Vout_x - Offset)/Sensitivity_compensated );
  float angle_y_radians = asin( (Vout_y - Offset)/Sensitivity_compensated );
  
  float angle_x_degrees = 180./3.14159 * angle_x_radians;
  float angle_y_degrees = 180./3.14159 * angle_y_radians;
  
  ///////////////
  // SAVE DATA //
  ///////////////

  // SD write
  datafile.print(Vout_x);
  datafile.print(F(","));
  datafile.print(Vout_y);
  datafile.print(F(","));
  datafile.print(angle_x_degrees);
  datafile.print(F(","));
  datafile.print(angle_y_degrees);
  datafile.print(F(","));
  
  // Echo to serial
  //int a = analogRead(xPin) - 512;
  //int b = analogRead(yPin) - 512;
  //Serial.print(a);
  //Serial.print(F(","));
  //Serial.print(b);
  //Serial.print(F(","));
  //Serial.print(VDD);
  //Serial.print(F(","));
  Serial.print(Vout_x);
  Serial.print(F(","));
  Serial.print(Vout_y);
  Serial.print(F(","));
  Serial.print(angle_x_degrees);
  Serial.print(F(","));
  Serial.print(angle_y_degrees);
  Serial.print(F(","));

}

void Logger::Anemometer_reed_switch(int interrupt_number, \
                                    unsigned long \
                                      reading_duration_milliseconds, \
                                    float meters_per_second_per_rotation){
  // Meters per second per rotation:
  // Inspeed anemometer that we have: 2.5 mph/Hz
  //                                  = 1.1176 (m/s)/Hz

  // I plan for no more than 40 Hz (100 mph), so will have a delay of
  // 10 ms between rotations to debounce the input. This should allow
  // up to 250 mph, at which point larger problems likely exist...

  // Look up: http://arduino.stackexchange.com/questions/12587/how-can-i-handle-the-millis-rollover

  rotation_count = 0; // Global variable
  //int rotation_count_local;
  float rotation_Hz;
  float wind_speed_meters_per_second;
  float reading_duration_seconds = reading_duration_milliseconds / 1000.;

  pinMode(3, INPUT);
  digitalWrite(3, HIGH);
  
  unsigned long millis_start = millis();
  attachInterrupt(1, _anemometer_count_increment, FALLING);

  // Avoid rollovers by comparing unsigned integers with the 
  // same number of bits
  while (millis() - millis_start <= reading_duration_milliseconds){
    //rotation_count_local = rotation_count;
    //sleepNow_nap();
    //rotation_count ++;
  }
  detachInterrupt(1);
  
  rotation_Hz = rotation_count / reading_duration_seconds;
  wind_speed_meters_per_second = rotation_Hz * meters_per_second_per_rotation;
  
  ///////////////
  // SAVE DATA //
  ///////////////

  // SD write
  datafile.print(rotation_count);
  datafile.print(F(","));
  datafile.print(rotation_Hz, 4);
  datafile.print(F(","));
  datafile.print(wind_speed_meters_per_second, 4);
  datafile.print(F(","));
  
  // Echo to serial
  Serial.print(rotation_count);
  Serial.print(F(","));
  Serial.print(rotation_Hz, 4);
  Serial.print(F(","));
  Serial.print(wind_speed_meters_per_second, 4);
  Serial.print(F(","));
  
}

void Logger::Wind_Vane_Inspeed(int vanePin){
  // Resistance changes with rotation
  // This is for the eVane2
  // Connect one wire to power supply, one wire to analog pin, one wire to GND
  // From documentation:
  // 5 - 95% of power supply input voltage = 0 to 360 degrees of rotation
  // Uses Hall Effect Sensor
  // Don't forget to use set screw to zero wind sensor before starting!
  float Vin_normalized = (analogRead(vanePin) / 1023.);
  float Vin_stretched = (Vin_normalized - 0.05) / 0.9;
  float Wind_angle = Vin_stretched * 360.; // Degrees -- azimuth
  
  ///////////////
  // SAVE DATA //
  ///////////////

  // SD write
  datafile.print(Wind_angle);
  datafile.print(F(","));
  
  // Echo to serial
  Serial.print(Wind_angle);
  Serial.print(F(","));
}

void Logger::Pyranometer(int analogPin, float raw_mV_per_W_per_m2, \
                         float gain, float V_ref, \
                         uint8_t ADC_resolution_nbits){
  /**
   * Pyranometer wtih instrumentation amplifier
   * 
   * Pyranomiter is from Kipp and Zonen
   * 
   * nominal raw_output_per_W_per_m2_in_mV = 10./1000.; // 10 mV at 1000 W/m**2
   * 
   * Actual raw output is based on calibration.
   * 
   * \b analogPin is the pin that receives the amplified voltage input
   * 
   * \b raw_mV_per_W_per_m2 is the conversion factor of the pyranometer:
   * number of millivolts per (watt/meter^2).
   * This does not include amplification!
   * 
   * \b gain is the amplification factor
   * 
   * \b V_ref is the reference voltage of the ADC; on the ALog, this is
   * a precision 3.3V regulator (unless a special unit without this regulator
   * is ordered; the regulator uses significant power)
   * 
   * \b ADC_resolution_nbits (10-16 for the ALog BottleLogger) is the 
   * number of bits of ADC resolution used (oversampling for >10)   * 
   */
   
  // V
  // Vref V --> mV
  float Vin = (analogReadOversample(analogPin, ADC_resolution_nbits) / 1023.) \
               * V_ref * 1000.;
  //float Vin = V_ref * 1000. * analogRead(analogPin) / 1023.; // No oversampling
  float Radiation_W_m2 = Vin / (raw_mV_per_W_per_m2 * gain);
  
  ///////////////
  // SAVE DATA //
  ///////////////

  // SD write
  datafile.print(Radiation_W_m2, 4);
  datafile.print(F(","));
  
  // Echo to serial
  Serial.print(Radiation_W_m2, 4);
  Serial.print(F(","));
}

float Logger::analogReadOversample(int pin, uint8_t adc_bits, int nsamples,
                                   bool debug){
  /**
   * This function incorporates oversampling to extend the ADC precision
   * past ten bits by taking more readings and statistically combing them.
   *
   * Returns a floating point number between 0 and 1023 in order to be 
   * intechangable with the Arduino core AnalogRead() function
   * 
   * It is often used within other sensor functinons to increase measurement
   * precision.
   * 
   * \b pin is the analog pin number
   * 
   * \b adc_bits is the reading precision in bits (2^adc_bits).
   * The ATMega328 (Arduino Uno and ALog BottleLogger core chip)
   * has a base ADC precision of 10 bits (returns values of 0-1023)
   * A reasonable maximum precision gain is (base_value_bits)+6, so
   * 16 bits is a reasonable maximum precision for the ALog BottleLogger.
   * 
   * \b nsamples is the number of times you want to poll the particular 
   * sensor and write the output to file.
   * 
   * \b debug is a flag that, if true, will write all of the values read during
   * the oversampling to "Oversample.txt".
   * 
   * Example:
   * ```
   * // 12-bit measurement of Pin 2
   * // Leaves nsamples at its default value of 1 (single reading of sensor)
   * logger.analogReadOversample(2, 12);
   * ```
   * 
   * Readings that require more bits of precision will take longer.
   * 
   * For analog measurements that do not require more than 10 bits of precision, 
   * use logger.readpin(int pin) or the standard Arduino "AnalogRead" function.
   * 
   * Based on eRCaGuy_NewAnalogRead::takeSamples(uint8_t analogPin)
   * 
   * 
  */
  
  if(debug){
    start_logging_to_otherfile("Oversample.txt");
  }
  float analog_reading;

  uint8_t n = adc_bits - 10; //"rightshift" value, AKA: "n"
  // Bit shift to calculate 4^n
  // (Best and fastest method to calculate 4 to a power)
  unsigned long oversample_num = 1UL<<(2*n);
  // Bit shift to calculate 2^n
  unsigned int divisor = 1<<n;

  //outer loop: get the number of samples to avg
  unsigned long reading_sum = 0;
  for (unsigned long i=0; i<nsamples; i++)
  {
    //inner loop: do oversampling, per AVR121 Application Note,
    // in order to enhance resolution of 10-bit ADC
    unsigned long inner_sum = 0;
    for (unsigned long j=0; j<oversample_num; j++)
    {
      inner_sum += analogRead(pin); //take a 10-bit reading on the Arduino ADC
      if(debug){
        otherfile.print(analogRead(pin));
        otherfile.print(F(","));
      }
    }
    //Convert these many 10-bit samples to a single higher-resolution sample:
    //Standard Method:
    //unsigned int reading = inner_sum >> n; //See AVR121 Application Note
    //Rounding Method (to nearest integer):
    unsigned long reading = (inner_sum + (unsigned long)divisor/2UL) >> n;
    reading_sum += reading;
  }
  float avg_reading = (float)reading_sum/(float)nsamples;

  // Normalize to 10 bits for all of the stuff here that expects that
  float precision_above_ten = pow(2., adc_bits - 10.);
  analog_reading = avg_reading / precision_above_ten; // 0-1023, but float

  return analog_reading;
}

/*
void Logger::Barometer_BMP180(){
  // Borrowed/modified from "sensorapi"
  // Commented out portions to add temperature support
  //float temperature;
  float pressure;
  Adafruit_BMP085_Unified bmp = Adafruit_BMP085_Unified(10085);

  sensors_event_t event;
  bmp.getEvent(&event);
  
  if (event.pressure){
    //temperature = bmp.getTemperature(&temperature);
    pressure = event.pressure * 100.; // hPa to Pa
  }
  else{
    //temperature = -9999;
    pressure = -9999;
  }

  ///////////////
  // SAVE DATA //
  ///////////////

  // SD write
  //SDpowerOn();
  //datafile.print(temperature);
  //datafile.print(F(","));
  datafile.print(pressure);
  datafile.print(F(","));
  //SDpowerOff();
  
  // Echo to serial
  //Serial.print(temperature);
  //Serial.print(F(","));
  Serial.print(pressure);
  Serial.print(F(","));
}
*/
  
void Logger::sleepNow_nap()         // here we put the arduino to sleep between interrupt readings
{
    set_sleep_mode(SLEEP_MODE_STANDBY);   // sleep mode is set here

    cbi(ADCSRA,ADEN);                    // switch Analog to Digitalconverter OFF

    sleep_enable();          // enables the sleep bit in the mcucr register
                             // so sleep is possible. just a safety pin 
    sleep_mode();            // here the device is actually put to sleep!!
                             // THE PROGRAM CONTINUES FROM HERE AFTER WAKING UP

    // After waking, run sleep mode function, and then remainder of this function (below)
    sleep_disable();         // first thing after waking from sleep:
                             // disable sleep...
    // detachInterrupt(1); // crude, but keeps interrupts from clashing. Need to improve this to allow both measurements types!
    // 06-11-2015: The above line commented to allow the rain gage to be read
    // at the same time as other readings
                        // Maybe move this to specific post-wakeup code?
}

// Must be defined outside of Logger class
void _ISR_void(){
}

void _anemometer_count_increment(){
  rotation_count ++;
  detachInterrupt(1);
  delay(20); // debounce
  attachInterrupt(1, _anemometer_count_increment, FALLING);
}

void Logger::HackHD(int control_pin, bool want_camera_on){
//void Logger::HackHD(int control_pin, int indicator_pin, bool want_camera_on){
  // 0.2 mA quiescent current draw
  // 600 mA while recording
  // Use audio outut pin as indicative of whether the camera is on or not:
  // HIGH on, LOW off. This is "indicator_pin".
  // Drop control_pin to GND to turn camera on or off
  //startAnalog();
  //delay(5000);
  // Analog functionality is off unless I explicity turn on the 3V3 regulator
  // Indicator seems to be 909 for off, and 1023 for on.
  //int indicator = 0
  //int indicator_pin = A1;
  //int indicator = analogRead(indicator_pin);
  //Serial.print(indicator);
  //endAnalog();
  // So I check if it is > 1000
  //bool CAMERA_IS_ON = (indicator > 1000);
  //bool CAMERA_IS_ON = (indicator > 0); // unnecessary holdover, and just if(indicator)
  Serial.print(F("C"));
  Serial.print(F("Camera is on"));
  Serial.print(want_camera_on);
  // Turn camera on or off if needed
  //if ( (CAMERA_IS_ON == 0 && want_camera_on == 1) || (CAMERA_IS_ON == 1 && want_camera_on == 0)){
  if (CAMERA_IS_ON != want_camera_on){
    pinMode(control_pin, OUTPUT);
    digitalWrite(control_pin, LOW);
    delay(200);
    pinMode(control_pin, INPUT);
    digitalWrite(control_pin, HIGH);
    CAMERA_IS_ON = 1 - CAMERA_IS_ON; // flips it from true to false and vice versa
    // Use this to get times of camera on/off
    start_logging_to_otherfile("camera.txt");

    now = RTC.now();
    // SD
    otherfile.print(now.unixtime());
    otherfile.print(",");

    // Echo to serial
    Serial.print(now.unixtime());
    Serial.print(F(","));

    if (want_camera_on == 1){
      otherfile.print("ON");
    }
    else if (want_camera_on == 0){
      otherfile.print("OFF");
    }
    end_logging_to_otherfile();
  }
  // Otherwise, these conditions match and we are in good shape.  
}

void Logger::AtlasScientific(char* command, int softSerRX, int softSerTX, uint32_t baudRate, bool printReturn, bool saveReturn){
  // * "command" is the command sent to the Atlas Scientific product.
  //   see the data sheet as well as the above quick lists
  // SerialNumber and baudRate default to 0 and 38400, respectively.
  // SerialNumber could be changed for Arduino Mega.
  // baudRate will stay constant insofar as Atlas sensors' baud rates do.
  // getReturn determines whether you care about the Serial response, or whether
  // you would just like to clear the buffer.
  //

  // Check if re-instantiating class causes problems -- maybe this is what 
  // happened with Nick's loggers (but then why different times before
  // failure?
  // Same name, so maybe overwrite memory in same place?
  SoftwareSerial mySerial(softSerRX, softSerTX);

  mySerial.begin(baudRate);
  //Serial2.println("L,0\r");
  
  /*
  uint32_t start_millis_clear_buffer = millis();
  Serial2.write(13); // Clear buffer -- carriage return
  while ( (millis() - start_millis_clear_buffer < 500) || Serial2.available() ){
    Serial2.read();
  }
  */

  String sensorString = ""; // a char array to hold the data from the Atlas Scientific product
                         // Atlas' example indicates that they expect all returns to be <= 30 bytes
  sensorString.reserve(30); // Sets aside memory for the Sring
  char inChar; // incoming characters on the serial port

  // "println" prints <values>\r\n. Atlas uses println to send data in its 
  // Arduino sample code, so I presume the "\r" is taken as the cutoff (its 
  // sensors terminate with a carriage return) and the "\n" is discarded.
  // Still, I wonder if it would be better to "Serial.print(...\n);"
  // (or ASCII 13, however Arduino does it) explicitly
  mySerial.print(command); // send the command to the Atlas Scientific product
  mySerial.write(13); // carriage return to signal the end of the command
  
  mySerial.flush(); // Wait for transmit to finish
  
  delay(100);

  //delay();
  
  
  uint32_t start_millis = millis(); // Safety guard in case all data not received -- won't hang for > 30 seconds
  bool endflag = false;
  
  // "5000" just to ensure that there is no hang if signal isn't good
  while ((millis() - start_millis < 5000) && !endflag){
    // used to have if (printReturn || saveReturn) here, but
    // should read return either way
    if (mySerial.available()){
      inChar = mySerial.read();
      // Serial.print(inChar); // uncomment for debugging input from sensor
      if (inChar != '\r'){
        //sensorString[i] = inChar;
        sensorString += inChar;
      }
      else{
        endflag = true;
      }
    }
  }
  // save iff both getReturn and saveReturn are true
  // Currently also echoes the return to serial port if it will
  // also be saved (sent to SD card)
  if (saveReturn){
    datafile.print(sensorString); // Should work without clock's CSpinRTC -- digging into object that is already made
    datafile.print(F(","));

  }
  // Echo to serial
  if (saveReturn || printReturn){
    Serial.print(sensorString);
    Serial.print(F(","));
  }
  /*
  else if (printReturn){ // so !savereturn
    Serial.println();
    Serial.print("Sensor returns: ");
    Serial.println(sensorString);
    Serial.println();
  }
  */
  
  // Get rid of the incoming Serial stream
  //while (_tx_buffer->head != _tx_buffer->tail); // get rid of the rest of the buffer after Line 1
  while( mySerial.read() != -1 );

  mySerial.end();

}

void Logger::TippingBucketRainGage(){

  // Uses the interrupt to read a tipping bucket rain gage.
  // Then prints date stamp

  detachInterrupt(1);

  pinMode(SDpowerPin,OUTPUT); // Seemed to have forgotten between loops... ?
  // might want to use a digitalread for better incorporation into normal logging cycle
  //SDpowerOn();
  if (!sd.begin(CSpin, SPI_HALF_SPEED)) {
    // Just use Serial.println: don't kill batteries by aborting code 
    // on error
    Serial.println(F("Error initializing SD card for writing"));

  }
  delay(10);
  start_logging_to_otherfile("b_tips.txt");
  now = RTC.now();

  // SD
  otherfile.print(now.unixtime());
  otherfile.print(",");

  // Echo to serial
  Serial.print(now.unixtime());
  Serial.print(F(","));
  end_logging_to_otherfile();

  // START TEMPORARY CODE TO NOTE BUCKET TIP RESPONSE
  pinMode(LEDpin, OUTPUT);
  digitalWrite(LEDpin, HIGH);
  // END TEMPORARY CODE TO NOTE BUCKET TIP RESPONSE
  Serial.println(F("Tip!"));
  delay(50); // to make sure tips aren't double-counted
  // START TEMPORARY CODE TO NOTE BUCKET TIP RESPONSE
  digitalWrite(LEDpin, LOW);
  pinMode(LEDpin, INPUT);
  // END TEMPORARY CODE TO NOTE BUCKET TIP RESPONSE
  NEW_RAIN_BUCKET_TIP = false;
  
  // Sets flag to log data if the "LOG_ON_BUCKET_TIP" flag is set "TRUE"
  if (LOG_ON_BUCKET_TIP){
    IS_LOGGING = true;
  }
  
  //delay(2000);
  
  attachInterrupt(1, wakeUpNow_tip, LOW);

  // Then based on whether we are already logging or if we are supposed to 
  // start logging here, we can continue with the logging process, or just 
  // go back to sleep
  if (_use_sleep_mode){
    // Nested recursion to next-level-up function, hopefully doesn't
    // chew through too much memory!
    if (!IS_LOGGING){
      sleep();
    }
  }
}

// call back for file timestamps
void Logger::_internalDateTime(uint16_t* date, uint16_t* time) {
  char timestamp[30];
  DateTime now = RTC.now();
  // return date using FAT_DATE macro to format fields
  *date = FAT_DATE(now.year(), now.month(), now.day());
  // return time using FAT_TIME macro to format fields
  *time = FAT_TIME(now.hour(), now.minute(), now.second());
}

void Logger::start_logging_to_datafile(){
  // Callback to set date and time
  // Following: https://forum.arduino.cc/index.php?topic=348562.0
  // See: https://github.com/NorthernWidget/Logger/issues/6
  SdFile::dateTimeCallback(_internalDateTime);
  // Open the file for writing
  if (!datafile.open(filename, O_WRITE | O_CREAT | O_AT_END)) {   
    Serial.print(F("Opening "));
    Serial.print(filename);
    Serial.println(F(" for write failed"));
  delay(10);
  }
}

void Logger::start_logging_to_otherfile(char* filename){
  // Callback to set date and time
  // Following: https://forum.arduino.cc/index.php?topic=348562.0
  // See: https://github.com/NorthernWidget/Logger/issues/6
  SdFile::dateTimeCallback(_internalDateTime);
  // open the file for write at end like the Native SD library
  if (!otherfile.open(filename, O_WRITE | O_CREAT | O_AT_END)) {
    // Just use Serial.println: don't kill batteries by aborting code 
    // on error
    Serial.print(F("Opening "));
    Serial.print(filename);
    Serial.println(F(" for write failed"));
    delay(10);
  }
}

void Logger::end_logging_to_otherfile(){
  // Ends line and closes otherfile
  // Copied from endLine function
  otherfile.println();
  Serial.println();
  // close the file: (This does the actual sync() step too - writes buffer)
  otherfile.close();
  delay(10);
}

//reads a 5tm soil moisture probe and prints results to Serial
// Modified from Steve Hicks' code for an LCD reader by Andy Wickert

/*
void Logger::decagon5TE(int excitPin, int dataPin){

  NewSoftSerial mySerial(excitPin, dataPin);  //5tm's red wire (serial data out) connected to pin 5, pin 6 goes nowhere
  int Epsilon_Raw, Sigma_Raw, T_Raw;   //temporary integer variables to store the 3 parts of the incoming serial stream from the 5TM
  char dataStream[14];   // Max 14 characters: 4x3 + 2 spaces
  int startflag=1;
  int endflag=0;
  int i=0;
  unsigned int startMillis; // same comment as right below
  unsigned int elapsed = 0; // shouldn't overflow on the time scales I'm using

  if(startflag){
    digitalWrite(excitPin,HIGH);
    startMillis = millis();
    startflag=0;
    Serial.println(startMillis);
  }

  // OK if it takes longer, so long as data stream is continuous
  // so we don't break out of inner while loop, and we start 
  // receiving before 200 ms is up
  while (elapsed < 200){
    elapsed = millis() - startMillis;
    Serial.print(F("ms elapsed = "));
    Serial.println(elapsed);
    //  code keeps looping until incoming serial data appears on the mySerial pin
    while (mySerial.available()) {
      //Serial.println("Getting data:");
        delay(1);  
      if (mySerial.available() >0) {
    		endflag=1;
        char c = mySerial.read();  //gets one byte from serial buffer
          Serial.println(c);
        if((c>='0' and c<='9') || c==' ') {
         dataStream[i] = c; //makes the string readString 
         i++;
        }
      }
    }
  }
    
  if(endflag==1){

    digitalWrite(excitPin,LOW);
    Serial.println(dataStream);
    endflag=0;

    // parse the array into 3 integers  (for the 5TM, y is always 0)
    sscanf (dataStream, "%d %d %d", &Epsilon_Raw, &Sigma_Raw, &T_Raw);     

    // Change measured values into real values, via equations in Decagon 5TE
    // manual

    // Dielectric permittivity [-unitless-]
    if (Epsilon_Raw == 4095){
      // Error alert!
      char Epsilon_a[6] = "ERROR";
    }
    else {
      float Epsilon_a = Epsilon_Raw/50.;
    }
    // Electrical Conductivity [dS/m]
    if (Sigma_Raw == 1023){
      // Error alert!
      char EC[6] = "ERROR";
    }
    else if (Sigma_Raw <= 700){
      float EC = Sigma_Raw/100.;
    }
    else {
      // (i.e. Sigma_Raw > 700, but no elif needed so long as input string
      // parses correctly... hmm, should maybe protect against that)
      float EC = (700. + 5.*(Sigma_Raw- 700.))/100.;
    }
    // Temperature [degrees C]
    // Combined both steps of the operation as given in the manual
    if (T_Raw == 1023){
      // Error alert!
      char T[6] = "ERROR";
    }
    else if (T_Raw <= 900){
      float T = (T_Raw - 400.) / 10.;
    }
    else {
      // (i.e. T_Raw > 900, but no elif needed so long as input string
      // parses correctly... hmm, should maybe protect against that)
      float T = ((900. + 5.*(T_Raw-900.) - 400.)) / 10.;
    }
    
*/    ///////////////
    // SAVE DATA //
    ///////////////
/*
    digitalWrite(SDpowerPin,HIGH);    
    datafile.print(Epsilon_a);
    datafile.print(F(","));
    datafile.print(EC);
    datafile.print(F(","));
    datafile.print(T);
    datafile.print(F(","));
    digitalWrite(SDpowerPin,LOW);
    // Echo to serial
    Serial.print(Epsilon_a);
    Serial.print(F(","));
    Serial.print(EC);
    Serial.print(F(","));
    Serial.print(T);
    Serial.print(F(","));
*/
//  }
//}

void Logger::DecagonGS1(int pin, float Vref, uint8_t ADC_resolution_nbits){
  /**
   * Ruggedized Decagon Devices soil moisture sensor
   * 
   * \b pin Analog pin number
   * 
   * \b Vref is the reference voltage of the ADC; on the ALog, this is
   * a precision 3.3V regulator (unless a special unit without this regulator
   * is ordered; the regulator uses significant power)
   * 
   * \b ADC_resolution_nbits (10-16 for the ALog BottleLogger) is the 
   * number of bits of ADC resolution used (oversampling for >10)   * 
   */
  
  // Vref in volts
  float _ADC;
  float voltage;
  float volumetric_water_content;
  _ADC = analogReadOversample(pin, ADC_resolution_nbits);
  voltage = Vref * _ADC / 1023.;
  
  // Standard Decagon equation -- linear, for up to 60% VWC
  volumetric_water_content = 0.494 * voltage - 0.554;
  
  ///////////////
  // SAVE DATA //
  ///////////////

  // SD write
  datafile.print(voltage, 4);
  datafile.print(F(","));
  datafile.print(volumetric_water_content, 4);
  datafile.print(F(","));
  
  // Echo to serial
  Serial.print(voltage, 4);
  Serial.print(F(","));
  Serial.print(volumetric_water_content, 4);
  Serial.print(F(","));

}

// Honeywell_HSC_analog
//////////////////////////////

float Logger::Honeywell_HSC_analog(int pin, float Vsupply, float Vref, \
                                   float Pmin, float Pmax, \
                                   int TransferFunction_number, int units, \
                                   uint8_t ADC_resolution_nbits){
  /**
   * Cost-effective pressure sensor from Honeywell
   * 
   * Datasheet: http://sensing.honeywell.com/index.php?ci_id=151133
   * 
   * \b pin Analog pin number
   * 
   * \b Vsupply Supply voltage to sensor
   * 
   * \b Vref is the reference voltage of the ADC; on the ALog, this is
   * a precision 3.3V regulator (unless a special unit without this regulator
   * is ordered; the regulator uses significant power)
   * 
   * \b Pmin Minimum pressure in range of sensor
   * 
   * \b Pmax Maximum pressure in range of sensor
   * 
   * \b Pmax Maximum pressure in range of sensor
   * 
   * \b TransferFunction_number: 1, 2, 3, or 4: which transfer function is 
   * used to convert voltage to pressure
   * 
   * \b Units: the units of the sensor
   * 
   * \b ADC_resolution_nbits (10-16 for the ALog BottleLogger) is the 
   * number of bits of ADC resolution used (oversampling for >10)   * 
   */
   
  //   

  // Read pin voltage
  float reading = analogReadOversample(pin, ADC_resolution_nbits);
  float Vout = reading/1023*3.3;
  
  // Apply transfer function 
  float P;

  if(TransferFunction_number == 1){
  P = (Vout - 0.1*Vsupply) * ((Pmax-Pmin)/(0.8*Vsupply)) + Pmin;
  }
  if(TransferFunction_number == 2){
  P = (Vout - 0.05*Vsupply) * ((Pmax-Pmin)/(0.9*Vsupply)) + Pmin;
  }  
  if(TransferFunction_number == 3){
  P = (Vout - 0.05*Vsupply) * ((Pmax-Pmin)/(0.8*Vsupply)) + Pmin;
  }
  if(TransferFunction_number == 4){
  P = (Vout - 0.04*Vsupply) * ((Pmax-Pmin)/(0.9*Vsupply)) + Pmin;
  }

  char* _units[]={"mbar", "bar", "Pa", "KPa", "Mpa", "inH2O", "PSI", "why"};

  ///////////////
  // SAVE DATA //
  ///////////////

  // SD write
  datafile.print(P, 4);
  datafile.print(F(" "));
  datafile.print(_units[units]);
  datafile.print(F(","));
  
  // Echo to serial
  Serial.print(P, 4);
  Serial.print(F(" "));
  Serial.print(_units[units]);
  Serial.print(F(","));

  return P;

}

void Logger::vdivR(int pin, float Rref, uint8_t ADC_resolution_nbits, bool Rref_on_GND_side){
  /**
   * Resistance from a simple voltage divider
   * 
   * \b pin Analog pin number
   * 
   * \b Rref Resistance value of reference resistor [ohms]
   * 
   * \b ADC_resolution_nbits (10-16 for the ALog BottleLogger) is the 
   * number of bits of ADC resolution used (oversampling for >10)   * 
   * 
   * \b Rref_on_GND-Side indicates the configuration of the voltage divider.  
   * True if using Alog provided Reference resistor terminals. If false, 
   * the reference resitor must be instead connected via the screw terminals.
   * This is set true for external sensors that are built to require a
   * VCC-side reference resistor.
   * 
   */
  
  float _R = _vdivR(pin, Rref, ADC_resolution_nbits, Rref_on_GND_side);

  ///////////////
  // SAVE DATA //
  ///////////////
  
  datafile.print(_R);
  datafile.print(F(","));

  // Echo to serial
  Serial.print(_R);
  Serial.print(F(","));

}

/*
SENSOR DOES NOT STABILIZE (FLEXFORCE SENSOR)
void Logger::flex(int flexPin, float Rref, float calib1, float calib2){
  float _Rflex = _vdivR(flexPin, Rref);
  // FINISH WRITING CODE
}
*/

void Logger::linearPotentiometer(int linpotPin, float Rref, float slope, \
                                 float intercept, \
                                 uint8_t ADC_resolution_nbits, \
                                 bool Rref_on_GND_side){
  /**
   * Distance based on resistance in a sliding potentiometer whose resistance
   * may be described as a linear function
   * 
   * \b linpotPin Analog pin number
   * 
   * \b Rref Resistance value of reference resistor [ohms]
   *
   * \b slope Slope of the line (distance = (slope)R + R0)
   *
   * \b intercept (R0) of the line (distance = (slope)R + R0)
   * 
   * \b ADC_resolution_nbits (10-16 for the ALog BottleLogger) is the 
   * number of bits of ADC resolution used (oversampling for >10)   * 
   * 
   * \b Rref_on_GND-Side indicates the configuration of the voltage divider.  
   * True if using Alog provided Reference resistor terminals. If false, 
   * the reference resitor must be instead connected via the screw terminals.
   * This is set true for external sensors that are built to require a
   * VCC-side reference resistor.
   * 
   * The output units will be whatever you have used to create your linear 
   * calibration equation
   * 
   */

  float _Rpot = _vdivR(linpotPin, Rref, ADC_resolution_nbits, Rref_on_GND_side);
  float _dist = slope*_Rpot + intercept;
  
  ///////////////
  // SAVE DATA //
  ///////////////
  
  datafile.print(_dist);
  datafile.print(F(","));

  // Echo to serial
  Serial.print(_dist);
  Serial.print(F(","));

}


// NEW STUFF:

void Logger::name(){
  // Self-identify before talking
  Serial.print(F("<"));
  Serial.print(logger_name);
  Serial.print(F(">: "));
}


void Logger::print_time(){
  boolean exit_flag = 1;
  // Wait for computer to tell logger to start sending its time
  char go;
  while( exit_flag ){
    go = Serial.read();
    if( (go == 'g') && exit_flag ){
      exit_flag = 0; // Exit loop once this is done
      // Print times before setting clock
      for (int i=0; i<5; i++){
        now = RTC.now();
        Serial.println(now.unixtime());
        if ( i<4 ){
          // No need to delay on the last time through
          delay(1000);
        }
      }
    }
  } // This is the end of the while loop for clock setting and passing the old
    // time over to the computer.
}

void Logger::set_time_main(){
  // Now set clock and returns 5 more times as part of that function
  // First thing coming in should be the time from the computer
  boolean exit_flag = 1;
  while( exit_flag ){
    if ( Serial.available() ){
      clockSet();
      exit_flag = 0; // Totally out of loop now
    }
  }
}

void Logger::announce_start(){
  Serial.println();
  name();
  Serial.println(F(" = this logger's name."));
  Serial.println();
  delay(100);
  Serial.println(\
    F("********************** Logger initializing. **********************"));
}

void Logger::startup_sequence(){
  boolean comp = false;
  char handshake[4];
  char handshake_test[5] = "alog"; // 5 chars, incl. termination
  int ntrue = 0;
  int i;
  unsigned long unixtime_at_start;
  
  // Check if connected to computer with ALogTalk running to set the clock
  int millisthen = millis();
  while ( (millis() - millisthen) < 2000 && (comp == 0)){
    if ( Serial.available() ){
      i = 0;
      while( Serial.available() && (i < 4)){
        handshake[i] = Serial.read();
        i++;
      }
	    for (int i=0; i<4; i++){
        if (handshake[i] == handshake_test[i]){
          ntrue++;
        }
      }
      if (ntrue == 4){
        comp = true;
      }
    }
  }
  
  // Run through startup sequence, including clock setting if comp is true
  name();
  Serial.println(F("HELLO, COMPUTER."));
  delay(50);
  //if ( Serial.available() ){ // To allow clock setting, uncomment this and comment the above section that sets "comp"
  if ( comp ){
    delay(1000); // Give Python time to print
    name();
    Serial.print(F("LOGGING TO FILE ["));
    Serial.print(filename);
    Serial.println(F("]"));
    delay(1500);
    name();
    Serial.print(F("UNIX TIME STAMP ON MY WATCH IS: "));
    now = RTC.now();
    unixtime_at_start = now.unixtime();
    Serial.println(unixtime_at_start);
    delay(1500);
    if(unixtime_at_start < 1000000000){
      name();
      Serial.println(F("Uh-oh: that doesn't sound right! Clock reset to 1/1/2000?"));
      LEDtimeWrong(3);
      print_time();
      set_time_main();
      delay(2000);
      name();
      Serial.println(F("Thanks, computer! I think I'm all set now."));
    }
    else{
      name();
      //Serial.println(F("Clock is probably fine"));
      ///*
      Serial.println(F("How does that compare to you, computer?"));
      delay(1500);
      print_time();
      delay(1500);
      name();
      Serial.println(F("Would you like to set the logger's clock to the computer's time? (y/n)"));
      boolean waiting = 1;
      char yn;
      while ( waiting ){
        if ( Serial.available() ){
          yn = Serial.read();
          if ( yn == 'y' || yn == 'Y'){
            waiting = 0;
            set_time_main();
          }
          else if ( yn == 'n' || yn == 'N'){
            waiting = 0;
            name();
            Serial.println(F("Not selecting time; continuing."));
            delay(1500);
          }
          else{
            name();
            Serial.println(F("Please select <y> or <n>."));
          }
        }
      }
    }
  }
  else{
    // No serial; just blink
    now = RTC.now();
    unixtime_at_start = now.unixtime();
    // Keep Serial just in case computer is connected w/out Python terminal
    Serial.print(F("Current UNIX time stamp according to logger is: "));
    Serial.println(unixtime_at_start);

    if(unixtime_at_start < 1000000000){
      LEDtimeWrong(3);
    }
  }
  if ( comp ){
    delay(1500);
    name();
    Serial.println(F("Now beginning to log."));
    delay(1000);
  }
  
}

void Logger::clockSet(){

  byte Year;
  byte Month;
  byte Date;
  byte DoW;
  byte Hour;
  byte Minute;
  byte Second;

  bool Century=false;
  bool h12;
  bool PM;

  DateTime nowPreSet = RTC.now();

	GetDateStuff(Year, Month, Date, DoW, Hour, Minute, Second);

	Clock.setClockMode(false);	// set to 24h
	//setClockMode(true);	// set to 12h

	Clock.setYear(Year);
	Clock.setMonth(Month);
	Clock.setDate(Date);
	Clock.setDoW(DoW);
	Clock.setHour(Hour);
	Clock.setMinute(Minute);
	Clock.setSecond(Second);

	// Give time at next five seconds
	// Should use a DateTime object for this, b/c rollover is a potential
	// problem, but this display is not mission-critical
	for (int i=0; i<5; i++){
	    delay(1000);
	    Serial.print(Clock.getYear(), DEC);
	    Serial.print(F("-"));
	    Serial.print(Clock.getMonth(Century), DEC);
	    Serial.print(F("-"));
	    Serial.print(Clock.getDate(), DEC);
	    Serial.print(F(" "));
	    Serial.print(Clock.getHour(h12, PM), DEC); //24-hr
	    Serial.print(F(":"));
	    Serial.print(Clock.getMinute(), DEC);
	    Serial.print(F(":"));
	    Serial.println(Clock.getSecond(), DEC);

	}
  delay(1000);
  unsigned long unixtime_at_receive_string = nowPreSet.unixtime();
  Serial.print(F("Logger's UNIX time at which it received the new time string: "));
  Serial.println(unixtime_at_receive_string);
  Serial.println(F("Clock set!"));
}


void Logger::GetDateStuff(byte& Year, byte& Month, byte& Day, byte& DoW, 
		byte& Hour, byte& Minute, byte& Second) {
	// Call this if you notice something coming in on 
	// the serial port. The stuff coming in should be in 
	// the order YYMMDDwHHMMSS, with an 'x' at the end.
	boolean GotString = false;
	char InChar;
	byte Temp1, Temp2;
	char InString[20];

	byte j=0;
	while (!GotString) {
		if (Serial.available()) {
			InChar = Serial.read();
			InString[j] = InChar;
			j += 1;
			if (InChar == 'x') {
				GotString = true;
			}
		}
	}
	Serial.println(InString);
	// Read Year first
	Temp1 = (byte)InString[0] -48;
	Temp2 = (byte)InString[1] -48;
	Year = Temp1*10 + Temp2;
	// now month
	Temp1 = (byte)InString[2] -48;
	Temp2 = (byte)InString[3] -48;
	Month = Temp1*10 + Temp2;
	// now date
	Temp1 = (byte)InString[4] -48;
	Temp2 = (byte)InString[5] -48;
	Day = Temp1*10 + Temp2;
	// now Day of Week
	DoW = (byte)InString[6] - 48;		
	// now Hour
	Temp1 = (byte)InString[7] -48;
	Temp2 = (byte)InString[8] -48;
	Hour = Temp1*10 + Temp2;
	// now Minute
	Temp1 = (byte)InString[9] -48;
	Temp2 = (byte)InString[10] -48;
	Minute = Temp1*10 + Temp2;
	// now Second
	Temp1 = (byte)InString[11] -48;
	Temp2 = (byte)InString[12] -48;
	Second = Temp1*10 + Temp2;
}


