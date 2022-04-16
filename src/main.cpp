#include <Arduino.h>
#include <Bounce2.h>
#include <SPI.h>
#include <Wire.h>
#include <TimerScreen.h>

// ESP32 WROOM32 Lolin Pinout: https://www.mischianti.org/wp-content/uploads/2020/11/ESP32-WeMos-LOLIN32-pinout-mischianti.png
// More Details: https://www.mischianti.org/2021/07/30/esp32-wemos-lolin32-lite-high-resolution-pinout-and-specs/
#define TRIGGER_PIN 2                   //connect to GND
#define RACING_LED 17                   //connect negative/cathode lead to GND
#define RACING_LED_CHANNEL 1
#define GATESTATUS_LED 16               //connect negative/cathode lead to GND
#define GATESTATUS_LED_CHANNEL 0        //for PWM Dimming
#define MODESWITCH_PIN 12               //connect to GND
#define EXTRABUTTON_PIN 14              //future use, connect to GND
#define SENSOR_THRESHOLD 3500
#define MAX_RACE_TIME_IN_SECONDS 9.9999
#define BRIGHTNESS_LOW 10
#define BRIGHTNESS_OFF 0

// https://grandprix-software-central.com/index.php/software/faq/faq/95-gprm-custom - Details for GPRM
#define COMMAND_GATE_CHECK 'G'                         //This command will ask the timer to check if the start gate is open or not.
#define COMMAND_TIMER_RESET 'R'                        //This command will tell the timer to reset itself so it is ready for the next heat. Any results or indications of finish order should be cleared from the computer screen and the timer's display.
#define COMMAND_FORCE_DATA_SEND 'F'                    //This command is used if you want to halt the timing (e.g. a car doesn't finish or there is an empty lane). If supported by the timer, pressing the Escape key will prompt the timer to return whatever data that it does have. In this case, any cars that had not finished will be given the maximum time. This command is not necessary if the timer sends the data for a lane as soon as the lane has finished timing. If the timer doesn't support this command, then you must wait until the timer times out before the results will be displayed on screen.
const char *RESPONSE_TIMER_READY = "READY";            //This command will tell the timer to reset itself so it is ready for the next heat. Any results or indications of finish order should be cleared from the computer screen and the timer's display.
const char *RESPONSE_GATE_OPEN = "GATE OPEN";          //This is the response that the timer will send back to indicate that the start gate is open.
const char *RESPONSE_GATE_CLOSED = "GATE CLOSED";      //This is the response that the timer will send back to indicate that the start gate is closed.
const char *RESPONSE_TIMER_STARTED_MESSAGE = "RACING"; //This is the message that the timer will send back to indicate that the heat has started (start gate has opened).
const uint8_t LANE_PINS[8] = {33, 32, 39, 36, 34, 35, 25, 26}; // 1st four are right, unsure about the rest
const short SCREENMODE_LANETIMES = 0;
const short SCREENMODE_SENSOR_OUTPUT = 1;
const short SCREENMODE_LANE_SUMMARY = 2;
const short SCREENMODE_SOFTWARE_IS_RACE_READY = 3;
const unsigned long MAX_LONG = 4294967295;
short LED_PIN = LED_BUILTIN;

using namespace std;

enum LaneStatus
{
  Racing,
  Finished,
  TooSlow,
  NotInUse // lane cannot be found during boot
};
enum TriggerStatus // aka Gate
{
  ReadyToRelease,
  Released
};
enum RaceStatus
{
  Idle,
  RaceInProgress,
  RaceDone
};
struct LaneInfo
{
  unsigned int pin;
  int laneNumber;
  LaneStatus status;
  unsigned long finishTime;
  unsigned long raceDuration;
};


// Global Variables
RaceStatus raceStatus;
TriggerStatus triggerStatus;

unsigned long raceBegin = 0;
unsigned long raceEnd = 0;
short NUM_LANES = 4; // Number of lanes the device is capable to monitor.
short screenMode = 0; 

TimerScreen screen;
Bounce trigger = Bounce();
Bounce screenModeButton = Bounce();
Bounce extraButton = Bounce();
vector<LaneInfo> Lanes; // essentially an array of Lanes

// Computes the time in seconds from beginning of race
float getTimeInSeconds(long durationInMicros)
{
  return (durationInMicros / 1000000.0);
}

// Computes the time in seconds from beginning of race
unsigned long computeDuration(unsigned long raceBeginTime, unsigned long endTime)
{
  unsigned long durationInMicros = 0;
  if (endTime < raceBeginTime)
  {
    // We overflowed the micros() limit (about every 50 minutes of ON time)
    durationInMicros = (MAX_LONG - raceBeginTime) + endTime;
  }
  else
  {
    durationInMicros = endTime - raceBeginTime;
  }
  return durationInMicros;
}

RaceStatus checkForTooLongOfARace()
{
  unsigned long currentMicros = micros();
  long currentDuration = computeDuration(raceBegin, currentMicros);
  if (getTimeInSeconds(currentDuration) > MAX_RACE_TIME_IN_SECONDS)
  {
    // Abort race, too long;
    for (auto& lane : Lanes)
    {
      if (lane.status != Finished)
      {
        lane.raceDuration = (MAX_RACE_TIME_IN_SECONDS * 1000000);
        lane.finishTime = 0;
        lane.status = TooSlow;
      }
    }
    raceEnd = currentMicros;
    return RaceDone;
  }
  return RaceInProgress;
}

RaceStatus pollLanes()
{
  // Poll lanes
  unsigned short int finishCount = 0;
  unsigned long currentMicros = micros();
  for (auto& lane : Lanes)
  {
    if (lane.status == Racing)
    {
      uint16_t sensorValue = analogRead(LANE_PINS[lane.pin]);
      // if (random(1, 10000) > 3) sensorValue = SENSOR_THRESHOLD + 1; //introduce some randomness for testing the sort. hey, wheres the unit tests?
      if (sensorValue < SENSOR_THRESHOLD)
      {
        // Sensor tripped, record time
        lane.finishTime = currentMicros;
        lane.raceDuration = computeDuration(raceBegin, currentMicros);
        lane.status = Finished;
        finishCount++;
      }
    }
    else
    {
      // Lane is already finished
      finishCount++;
    }
  }
  if (finishCount >= NUM_LANES)
  {
    raceEnd = micros();
    return RaceDone;
  }

  return checkForTooLongOfARace();
}

void initializeLanesForRacing(LaneStatus newLaneStatus)
{
  for (auto& lane : Lanes)
  {
    lane.finishTime = 0;
    lane.raceDuration = 0;
    lane.status = newLaneStatus;
  }
}

TriggerStatus getTriggerStatus(int input)
{
  if (input == LOW)
  {
    return ReadyToRelease;
  }
  return Released;
}

void AdjustIndicatorLED(short channel, short brightness) {
  ledcWrite(channel, brightness);
}

void detectPresenceOfLanes() {
    // Clear and reserve space in the Lanes vector/array
    Lanes.clear();
    Lanes.reserve(NUM_LANES);
    
    // Detect and register lanes
    unsigned short int activeLaneCount = 0;
    for (unsigned short int i = 0; i < NUM_LANES; i++)
    {
      uint16_t sensorValue = analogRead(LANE_PINS[i]);
      LaneStatus ls = Finished;
      if (sensorValue < SENSOR_THRESHOLD) {
        ls = NotInUse;
      } else {
        activeLaneCount++;
      }
      Lanes.push_back((struct LaneInfo){.pin=i, .laneNumber=i+1, .status=ls, .finishTime = 0, .raceDuration = 0});
    }
  
    char buffer[200]; 
    String result;
    result.clear(); 
    
    sprintf(buffer,"Detected\n%d Lanes\n", activeLaneCount);
    result.concat(buffer);

    // List Lane Numbers
    for (auto& lane : Lanes)
    {
      sprintf(buffer,"%i", lane.laneNumber);
      result.concat(buffer);
    }

    result.concat('\n');

    // List Lane Status
    for (auto& li : Lanes)
    {
      if (li.status == NotInUse) {
        sprintf(buffer,"X");
      } else {
        sprintf(buffer,"%i", li.laneNumber);
      }

      result.concat(buffer);
    }

    // Output to Screen
    Serial.println(result);
    screen.displayResults(result);
}

void setup()
{
  Serial.begin(9600);
  raceStatus = Idle;

  // LED SETUP
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // INDICATOR LED SETUP, driven by PWM
  ledcSetup(GATESTATUS_LED_CHANNEL, 2500, 8);
  ledcSetup(RACING_LED_CHANNEL, 2500, 8);
  ledcAttachPin(GATESTATUS_LED, GATESTATUS_LED_CHANNEL);
  ledcAttachPin(RACING_LED, RACING_LED_CHANNEL);

  // Screen Setup
  screen = TimerScreen();
  screen.setup();
  screen.print("Booted...");

  // Gate/Trigger Setup
  trigger.attach(TRIGGER_PIN, INPUT_PULLUP); // USE EXTERNAL PULL-UP
  trigger.interval(5); // DEBOUNCE INTERVAL IN MILLISECONDS
  trigger.update();
  triggerStatus = getTriggerStatus(trigger.read());

  // Screen Mode Button Toggler Setup
  screenModeButton.attach(MODESWITCH_PIN, INPUT_PULLUP); // USE EXTERNAL PULL-UP
  screenModeButton.interval(100); // DEBOUNCE INTERVAL IN MILLISECONDS
  screenModeButton.update();

  // Extra Button Setup
  extraButton.attach(EXTRABUTTON_PIN, INPUT_PULLUP); // USE EXTERNAL PULL-UP
  extraButton.interval(100); // DEBOUNCE INTERVAL IN MILLISECONDS
  extraButton.update();

  detectPresenceOfLanes();

}

// Sorting Functions
auto laneTimeSorterFunction = [](LaneInfo a, LaneInfo b) { return a.raceDuration < b.raceDuration; };
auto laneNumberSorterFunction = [](LaneInfo a, LaneInfo b) { return a.laneNumber < b.laneNumber; };

void outputRaceTimes()
{
  char buffer[200];
  String result;
  result.clear(); 
  
  sort(Lanes.begin(), Lanes.end(), laneTimeSorterFunction);

  for (auto& lane : Lanes) {
    sprintf(buffer,"%d %3.4f\n", lane.laneNumber, getTimeInSeconds(lane.raceDuration));
    result.concat(buffer);
  }
  Serial.println(result);
  screen.displayResults(result);
}

void outputSensorReadouts()
{
  char buffer[200];
  String result;
  result.clear();

  sort(Lanes.begin(), Lanes.end(), laneNumberSorterFunction);

  for (auto& lane : Lanes)
  {
    uint16_t sensorValue = analogRead(LANE_PINS[lane.pin]);
    sprintf(buffer,"%d %d\n", lane.laneNumber, sensorValue);
    result.concat(buffer);
  }
  screen.displaySensorReadout(result);
}


void updateLEDs()
{
  if (raceStatus == RaceInProgress)
  {
    digitalWrite(LED_PIN, LOW);
    AdjustIndicatorLED(RACING_LED_CHANNEL, BRIGHTNESS_LOW);
  }
  else
  {
    digitalWrite(LED_PIN, HIGH);
    AdjustIndicatorLED(RACING_LED_CHANNEL, BRIGHTNESS_OFF);
  }

  if (triggerStatus == ReadyToRelease) {
    AdjustIndicatorLED(GATESTATUS_LED_CHANNEL, BRIGHTNESS_LOW);
  }
  else 
  {
    AdjustIndicatorLED(GATESTATUS_LED_CHANNEL, BRIGHTNESS_OFF);
  }
}

void processCurrentScreenMode() {
  switch (screenMode)
  {
    case SCREENMODE_LANETIMES:
      outputRaceTimes();
      break;
    case SCREENMODE_SENSOR_OUTPUT:
      detectPresenceOfLanes();
      break;
    case SCREENMODE_LANE_SUMMARY:
      detectPresenceOfLanes();
      break;
    case SCREENMODE_SOFTWARE_IS_RACE_READY:
      screen.print("Software\nawaiting\nrace\nstart.");
      break;
    default:
      Serial.println("Unknown screenMode");
      break;
  }
}

void loop()
{

  // Check for Gate Trigger
  trigger.update();
  if (trigger.changed())
  {
    int debouncedTriggerInput = trigger.read();
    triggerStatus = getTriggerStatus(debouncedTriggerInput);
    if (raceStatus != RaceInProgress && triggerStatus == Released)
    {
      raceBegin = micros();
      initializeLanesForRacing(Racing);
      Serial.println(RESPONSE_TIMER_STARTED_MESSAGE);
      raceStatus = RaceInProgress;
      screenMode = SCREENMODE_LANETIMES;
      screen.print("Race\nIn\nProgress");
      updateLEDs();
    }
  }

  // Button Presses
  extraButton.update();
  screenModeButton.update();

  if (extraButton.read() == LOW) {
    // Print message so user doens't think button is broken.
    // Shown only when button is held.
    screen.print("This\nbutton\ndoes\nnothing.");
  } else {
    if (screenModeButton.fell())
    {
      screenMode++;
      if(screenMode > 2) screenMode = 0;
      processCurrentScreenMode();
    }

    if(screenMode == SCREENMODE_SENSOR_OUTPUT) {
      outputSensorReadouts();
    }
  }

  if (extraButton.rose()) {
    // Returns screen to prior state
    processCurrentScreenMode();
  }

  // Update the LEDs to proper status
  updateLEDs();

  // Handle state changes
  switch (raceStatus)
  {
  case RaceInProgress:
    raceStatus = pollLanes();
    break;
  case RaceDone:
    screenMode = SCREENMODE_LANETIMES;
    outputRaceTimes();
    raceStatus = Idle;
    break;
  case Idle:
    break;
  default:
    Serial.println("Unknown Status");
    break;
  }

  // Sniff for incoming commands from software via the serial connection
  int incomingByte = 0; // for incoming serial data
  if (Serial.available() > 0)
  {
    // Read the incoming byte
    incomingByte = Serial.read();
    switch (incomingByte)
    {
    case COMMAND_GATE_CHECK:
      if (triggerStatus == ReadyToRelease)
      {
        Serial.println(RESPONSE_GATE_CLOSED);
      }
      else
      {
        Serial.println(RESPONSE_GATE_OPEN);
      }
      break;
    case COMMAND_FORCE_DATA_SEND:
      outputRaceTimes(); //TODO This really isn't fully/properly implemented.
      break;
    case COMMAND_TIMER_RESET:
      initializeLanesForRacing(Finished);
      if (triggerStatus == ReadyToRelease)
      {
        Serial.println(RESPONSE_TIMER_READY);
        screenMode = SCREENMODE_SOFTWARE_IS_RACE_READY;
        raceStatus = Idle;
        processCurrentScreenMode();
      }
      else
      {
        Serial.println(RESPONSE_GATE_OPEN); 
      }
    default:
      break;
    }
  }
}
