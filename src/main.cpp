/*
  ESP 32 Blink
  Turns on an LED on for one second, then off for one second, repeatedly.
  The ESP32 has an internal blue LED at D2 (GPIO 02)

*/
#include <Arduino.h>
#include <Bounce2.h>

#define ANALOG_PIN_LANE_4 36 //4
#define ANALOG_PIN_LANE_3 39 //3
#define ANALOG_PIN_LANE_2 32 //2
#define ANALOG_PIN_LANE_1 33 //1
#define TRIGGER_PIN 2
#define SENSOR_THRESHOLD 4000
#define NUM_LANES 4
const uint8_t ANALOG_PINS[NUM_LANES] = {33, 32, 39, 36}; //, 31, 35, 25, 26}; // 1st four are right, unsure about the rest

enum LaneStatus
{
  Racing,
  Finished
};
enum TriggerStatus 
{
  ReadyToRelease,
  Released
};
enum RaceStatus
{
  Idle,
  ReadyToRace,
  RaceInProgress,
  RaceDone,
  ScoresSent
};
struct LaneInfo
{
  LaneStatus status;
  unsigned long finishTime;
};
LaneInfo *Lanes = new LaneInfo[NUM_LANES];
RaceStatus raceStatus;

unsigned long raceBegin = 0;
unsigned long raceEnd = 0;

Bounce trigger = Bounce();

// int lane4 = 0;
// int lane3 = 0;
// int lane2 = 0;
// int lane1 = 0;
const unsigned long MAX_LONG = 4294967295;

// SET A VARIABLE TO STORE THE LED STATE
int ledState = HIGH;
int LED_PIN = LED_BUILTIN;
//bool isRaceInProgress = false;


RaceStatus pollLanes()
{
  //poll lanes
  int finishCount = 0;
  char buffer[200];
  for (int i = 0; i < NUM_LANES; i++)
  {
    if (Lanes[i].status == Racing)
    {
      uint16_t sensorValue = analogRead(ANALOG_PINS[i]);
      sprintf(buffer, "Lane %d sensorValue: %d", i, sensorValue);
      Serial.println(buffer);
      if (sensorValue < SENSOR_THRESHOLD)
      {
        //sensor tripped, record time
        Lanes[i].finishTime = micros();
        Lanes[i].status = Finished;
        //send scores
        
        sprintf(buffer, "Lane %d: %lu", i, Lanes[i].finishTime);
        Serial.println(buffer);
        delay(250);
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
    return RaceDone;
  }
  return RaceInProgress;
}

void initializeLanesForRacing(LaneStatus newLaneStatus) {
  for (int i = 0; i < NUM_LANES; i++)
  {
    Lanes[i].finishTime = 0;
    Lanes[i].status = newLaneStatus;
  }
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
  initializeLanesForRacing(Finished);
  if (trigger.read() == LOW) {
    raceStatus = ReadyToRace;
  }
}


void loop()
{
  //digitalWrite(LED_PIN, HIGH);   // turn the LED on (HIGH is the voltage level)
  //delay(200);                       // wait for a second
  //digitalWrite(LED_PIN, LOW);    // turn the LED off by making the voltage LOW
  //delay(50);  // wait for a second

  // Update the Bounce instance (YOU MUST DO THIS EVERY LOOP)
  trigger.update();

  //check to see
  switch (raceStatus)
  {
  case RaceInProgress:
    raceStatus = pollLanes();
    Serial.println("RaceInProgress");
    break;
  case ReadyToRace:
    //watch for trigger to be thrown which means race started.
    if (trigger.changed())
    {
      int deboucedInput = trigger.read();
      if (deboucedInput == HIGH)
      {
        initializeLanesForRacing(Racing);
        raceStatus = RaceInProgress;
      }
    }
    Serial.println("ReadyToRace");
    break;
  case RaceDone:
    //dunno
    Serial.println("done");
    //send scores
    char buffer[200];
    sprintf(buffer, "Lane 1: %lu, Lane 2: %lu, Lane 3: %lu Lane 4: %lu", Lanes[0].finishTime, Lanes[1].finishTime, Lanes[2].finishTime, Lanes[3].finishTime);
    Serial.println(buffer);
    
    raceStatus = ScoresSent;
    break;
  case ScoresSent:
    Serial.println("ScoresSent");
    delay(3000);
    //watch for trigger to be set
    if (trigger.changed())
    {
      int deboucedInput = trigger.read();
      if (deboucedInput == LOW)
      {
        raceStatus = ReadyToRace;
      }
    }
    break;
  case Idle:
    //watch for trigger to be set
    if (trigger.changed())
    {
      int deboucedInput = trigger.read();
      if (deboucedInput == LOW)
      {
        raceStatus = ReadyToRace;
      }
    }
    
    Serial.println("Idle");
    break;
  default:
    Serial.println("Unknown Status");
    break;
  }

  if (raceStatus == RaceInProgress)  {
    digitalWrite(LED_PIN, LOW);
  } else {
    digitalWrite(LED_PIN, HIGH);
  }

  // // <Bounce>.changed() RETURNS true IF THE STATE CHANGED (FROM HIGH TO LOW OR LOW TO HIGH)
  // if (trigger.changed())
  // {
  //   int deboucedInput = trigger.read();
  //   // IF THE CHANGED VALUE IS LOW
  //   if (deboucedInput == HIGH)
  //   {
  //     ledState = !ledState;            // SET ledState TO THE OPPOSITE OF ledState
  //     digitalWrite(LED_PIN, ledState); // WRITE THE NEW ledState
  //   }
  // }

  // lane4 = analogRead(ANALOG_PIN_LANE_4);
  // lane3 = analogRead(ANALOG_PIN_LANE_3);
  // lane2 = analogRead(ANALOG_PIN_LANE_2);
  // lane1 = analogRead(ANALOG_PIN_LANE_1);
  // //unsigned long time = micros();


  
  //delay(100); 
}

