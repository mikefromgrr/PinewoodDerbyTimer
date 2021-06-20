#ifndef TimerScreen_h
#define TimerScreen_h

#if defined(ARDUINO) && ARDUINO >= 100
#include "Arduino.h"
#else
#include "WProgram.h"
#endif

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

class TimerScreen
{
 // Note : this is private as it migh change in the futur
private:
//   static const uint8_t DEBOUNCED_STATE = 0b00000001;

private:
//   inline void changeState();
//   inline void setStateFlag(const uint8_t flag)       {state |= flag;}
//   inline void unsetStateFlag(const uint8_t flag)     {state &= ~flag;}
//   inline void toggleStateFlag(const uint8_t flag)    {state ^= flag;}
//   inline bool getStateFlag(const uint8_t flag) const {return((state & flag) != 0);}

protected:
    //Adafruit_SSD1306 display;

public:
	TimerScreen();
    //void interval(uint16_t interval_millis);
    void setup();
	void print(String s);
    void displayResults(String s);
    void displaySensorReadout(String s);
};



#endif