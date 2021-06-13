#include <Arduino.h>
#include <Bounce2.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define ANALOG_PIN_LANE_4 36 //4
#define ANALOG_PIN_LANE_3 39 //3
#define ANALOG_PIN_LANE_2 32 //2
#define ANALOG_PIN_LANE_1 33 //1
#define TRIGGER_PIN 2
#define SENSOR_THRESHOLD 4000
#define MAX_RACE_TIME_IN_SECONDS 9.999
#define NUM_LANES 4

// https://grandprix-software-central.com/index.php/software/faq/faq/95-gprm-custom - Details for GPRM
#define COMMAND_GATE_CHECK 'G'                         //This command will ask the timer to check if the start gate is open or not.
#define COMMAND_TIMER_RESET 'R'                        //This command will tell the timer to reset itself so it is ready for the next heat. Any results or indications of finish order should be cleared from the computer screen and the timer's display.
#define COMMAND_FORCE_DATA_SEND 'F'                    //This command is used if you want to halt the timing (e.g. a car doesn't finish or there is an empty lane). If supported by the timer, pressing the Escape key will prompt the timer to return whatever data that it does have. In this case, any cars that had not finished will be given the maximum time. This command is not necessary if the timer sends the data for a lane as soon as the lane has finished timing. If the timer doesn't support this command, then you must wait until the timer times out before the results will be displayed on screen.
const char *RESPONSE_TIMER_READY = "READY";            //This command will tell the timer to reset itself so it is ready for the next heat. Any results or indications of finish order should be cleared from the computer screen and the timer's display.
const char *RESPONSE_GATE_OPEN = "GATE OPEN";          //This is the response that the timer will send back to indicate that the start gate is open.
const char *RESPONSE_GATE_CLOSED = "GATE CLOSED";      //This is the response that the timer will send back to indicate that the start gate is closed.
const char *RESPONSE_TIMER_STARTED_MESSAGE = "RACING"; //This is the message that the timer will send back to indicate that the heat has started (start gate has opened).

const uint8_t ANALOG_PINS[NUM_LANES] = {33, 32, 39, 36}; //, 31, 35, 25, 26}; // 1st four are right, unsure about the rest

enum LaneStatus
{
  Racing,
  Finished,
  TooSlow
};
enum TriggerStatus //aka Gate
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
  LaneStatus status;
  unsigned long finishTime;
};
LaneInfo *Lanes = new LaneInfo[NUM_LANES];
RaceStatus raceStatus;
TriggerStatus triggerStatus;

unsigned long raceBegin = 0;
unsigned long raceEnd = 0;

Bounce trigger = Bounce();
const unsigned long MAX_LONG = 4294967295;

// SET A VARIABLE TO STORE THE LED STATE
int ledState = HIGH;
int LED_PIN = LED_BUILTIN;
//bool isRaceInProgress = false;

float getTimeInSeconds(unsigned long raceBeginTime, unsigned long endTime)
{
  unsigned long durationInMicros = 0;
  if (endTime < raceBeginTime)
  {
    //we overflowed the micros() limit (about every 50 minutes of ON time)
    durationInMicros = (MAX_LONG - raceBeginTime) + endTime;
  }
  else
  {
    durationInMicros = endTime - raceBeginTime;
  }
  return (durationInMicros / 1000000.0);
}

RaceStatus checkForTooLongOfARace()
{
  if (getTimeInSeconds(raceBegin, micros()) > MAX_RACE_TIME_IN_SECONDS)
  {
    //abort race, too long;
    unsigned long finishTimeForSlowRacers = micros() + 1000000;
    for (unsigned short int i = 0; i < NUM_LANES; i++)
    {
      if (Lanes[i].status != Finished)
      {
        Lanes[i].finishTime = finishTimeForSlowRacers;
        Lanes[i].status = TooSlow;
      }
    }
    raceEnd = micros();
    return RaceDone;
  }
  return RaceInProgress;
}

RaceStatus pollLanes()
{
  //poll lanes
  unsigned short int finishCount = 0;
  for (unsigned short int i = 0; i < NUM_LANES; i++)
  {
    if (Lanes[i].status == Racing)
    {
      uint16_t sensorValue = analogRead(ANALOG_PINS[i]);
      //sprintf(buffer, "Lane %d sensorValue: %d", i, sensorValue);
      //Serial.println(buffer);
      if (sensorValue < SENSOR_THRESHOLD)
      {
        //sensor tripped, record time
        Lanes[i].finishTime = micros();
        Lanes[i].status = Finished;
        //sprintf(buffer, "Lane %d: %lu", i, Lanes[i].finishTime);
        //Serial.println(buffer);
        finishCount++;
      }
    }
    else
    {
      //lane is already finished
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
  for (int i = 0; i < NUM_LANES; i++)
  {
    Lanes[i].finishTime = 0;
    Lanes[i].status = newLaneStatus;
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

void setup()
{
  Serial.begin(9600);

  // LED SETUP
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, ledState);

  // SELECT ONE OF THE FOLLOWING :
  // 1) IF YOUR INPUT HAS AN INTERNAL PULL-UP
  // bounce.attach( BOUNCE_PIN ,  INPUT_PULLUP ); // USE INTERNAL PULL-UP
  // 2) IF YOUR INPUT USES AN EXTERNAL PULL-UP
  trigger.attach(TRIGGER_PIN, INPUT_PULLUP); // USE EXTERNAL PULL-UP

  // DEBOUNCE INTERVAL IN MILLISECONDS
  trigger.interval(25); // interval in ms
  raceStatus = Idle;
  trigger.update();
  triggerStatus = getTriggerStatus(trigger.read());
}

void outputRaceTimes()
{
  for (int i = 0; i < NUM_LANES; i++)
  {
    Serial.printf("%d %3.4f  ", (i + 1), getTimeInSeconds(raceBegin, Lanes[i].finishTime));
  }
  Serial.println();
}

void updateLEDs()
{
  if (raceStatus == RaceInProgress)
  {
    digitalWrite(LED_PIN, LOW);
  }
  else
  {
    digitalWrite(LED_PIN, HIGH);
  }
}

void loop()
{
  trigger.update();
  if (trigger.changed())
  {
    int deboucedInput = trigger.read();
    triggerStatus = getTriggerStatus(deboucedInput);
    if (raceStatus != RaceInProgress && triggerStatus == Released)
    {
      initializeLanesForRacing(Racing);
      Serial.println(RESPONSE_TIMER_STARTED_MESSAGE);
      raceBegin = micros();
      raceStatus = RaceInProgress;
      updateLEDs();
      delay(1000);
    }
  }

  updateLEDs();

  //handle state changes
  switch (raceStatus)
  {
  case RaceInProgress:
    raceStatus = pollLanes();
    break;
  case RaceDone:
    outputRaceTimes();
    raceStatus = Idle;
    break;
  case Idle:
    break;
  default:
    Serial.println("Unknown Status");
    break;
  }

  //sniff for incoming commands from software via the serial connection
  int incomingByte = 0; // for incoming serial data
  if (Serial.available() > 0)
  {
    //read the incoming byte
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
      outputRaceTimes(); //TODO This really isn't fully/properly implmented.
      break;
    case COMMAND_TIMER_RESET:
      initializeLanesForRacing(Finished);
      if (triggerStatus == ReadyToRelease)
      {
        Serial.println(RESPONSE_TIMER_READY);
      }
      else
      {
        Serial.println("GATE OPEN");
      }
    default:
      break;
    }
  }
}
