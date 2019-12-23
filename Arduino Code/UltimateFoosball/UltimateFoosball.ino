// *******************************************************************************
//
// Ultimate Foosball Table Firmware v1.0 (12/9/2019)
//
// Firmware to power the customizations done for my son's foosball table.
//
// Written by Jay Collett (jay AT jaycollett.com)
// http://www.jaycollett.com
//
// Additional unmodified libraries used (non-Arduino):
// VS1053 from Adafruit (www.adafruit.com)
// FastLED from FastLED (FastLED.io)
//
// This code is licensed under the MIT license.
//
// *******************************************************************************
#include <FastLED.h>
#include <SPI.h>
#include <Adafruit_VS1053.h>
#include <SD.h>
#include <Adafruit_GFX.h>
#include "Adafruit_LEDBackpack.h"

// ***********************************************************************************************************************************************
//
// Define our global params for the program
//
// ***********************************************************************************************************************************************
#define   NUM_LEDS_HOME_TEAM      62    // number of LEDs in the home team LED strip
#define   NUM_LED_VISITING_TEAM   62    // number of LEDs in the visiting team LED strip
#define   VISITING_LED_DATA_PIN   11    // pin used to talk with the neopixel LED strip for the visiting team LED strip
#define   HOME_TEAM_LED_DATA_PIN  12    // pin used to talk with the neopixel LED strip for the home team LED strip
#define   LED_BRIGHTNESS          128   // set the brightness of the neopixel LEDs (0-255) 0 = off, 255 = full on ouch bright
#define   HOME_SCORE_PIN          15    // pin used for the home team beam break (to detect a goal/score)
#define   VISITING_SCORE_PIN      19    // pin used for the visiting team beam break (to detect a goal/score)
#define   HOT_TEAM_MILLISECS      10000 // if a team scores again within 10 seconds, play a hot team sound clip
#define   GAME_RESET_BUTTON       13    // pin used to control resetting the current game (not the MCU)
#define   VS1053_RESET            -1    // VS1053 reset pin (not used!)
#define   WINC_EN                 2     // enable pin for WINC1500
#define   CARDCS                  5     // Card chip select pin
#define   VS1053_CS               6     // VS1053 chip select pin (output)
#define   VS1053_DREQ             9     // VS1053 Data request, ideally an Interrupt pin
#define   VS1053_DCS              10    // VS1053 Data/command select pin (output)
#define   BORED_CROWD_MILLIS      45000 // 45 seconds must lapse before we play a bored crowd sound clip
#define   ISR_WAIT_TIME           1000  // time in ms that must elapse before processing any interrtupt again
#define   TWK_LED_DELAY           200   // time between calls to the twinkle led function in the loop event
//#define   DEBUG


// Setup our debug printing
#ifdef DEBUG
#define debug(x)     Serial.print(x)
#define debugln(x)   Serial.println(x)
#else
#define debug(x)     // define empty, so macro does nothing
#define debugln(x)
#endif

// ***********************************************************************************************************************************************
//
// Init objects
//
// ***********************************************************************************************************************************************
CRGB homeTeamLeds[NUM_LEDS_HOME_TEAM];
CRGB visitingTeamLeds[NUM_LED_VISITING_TEAM];
Adafruit_VS1053_FilePlayer musicPlayer = Adafruit_VS1053_FilePlayer(VS1053_RESET, VS1053_CS, VS1053_DCS, VS1053_DREQ, CARDCS);
Adafruit_7segment scoreDisplay = Adafruit_7segment();

// ***********************************************************************************************************************************************
//
// Init global variables (volatile is needed as we set some of these in the ISR functions)
//
// ***********************************************************************************************************************************************
volatile unsigned int homeTeamScore;
volatile unsigned int visitingTeamScore;
volatile char lastTeamScored;
volatile unsigned int lastScoreTime;
enum TrackType { HomeTeamScore, VisitorTeamScore, HomeTeamHOT, VisitorTeamHOT, CrowdIsBored, SystemStart, SystemReset };
unsigned int tempAnalogReadForRandom;
unsigned int lastTimeBoredCrowdWasPlayed;
volatile bool homeTeamHot = false;
volatile bool visitingTeamHot = false;
volatile bool gameResetRequested = false;
volatile bool processAGoal = false;
volatile unsigned int lastRSTTriggerTime = 0;
unsigned int lastTwinkleLEDUpdate = 0;

// EVENTS to be coded for
//
// 1. Home or Visitor Goal
// 2. Multiple goals within x seconds for a single team, ON FIRE team
// 3. No goals within x seconds, the crowd is growing restless
// 4. Random smack talk or random anncounement if no goal in x seconds?
// 5. Start-up/game start anncoucement
//

// ***********************************************************************************************************************************************
//
// Main Setup Method Call
//
// ***********************************************************************************************************************************************
void setup() {
  // init serial output
  Serial.begin(9600);

  // set up the scoreboard display (7 segment led)
  scoreDisplay.begin(0x70);

  if (! musicPlayer.begin()) { // initialise the music player
    debugln(F("Couldn't find VS1053, do you have the right pins defined?"));
    while (1);
  }

  debugln(F("VS1053 found"));
  delay(250);

  musicPlayer.sineTest(0x44, 500);    // Make a tone to indicate VS1053 is working
  delay(250);

  if (!SD.begin(CARDCS)) {
    debugln(F("SD failed, or not present"));
    while (1);  // don't do anything more
  }
  debugln("SD OK!");

  // Set volume for left, right channels. lower numbers == louder volume!
  musicPlayer.setVolume(5, 5);
  musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT);  // DREQ int

  // build the object of LEDs for the home team array
  FastLED.addLeds<NEOPIXEL, HOME_TEAM_LED_DATA_PIN>(homeTeamLeds, NUM_LEDS_HOME_TEAM).setCorrection(TypicalLEDStrip);;


  // build the object of LEDs for the visiotr team array
  FastLED.addLeds<NEOPIXEL, VISITING_LED_DATA_PIN>(visitingTeamLeds, NUM_LED_VISITING_TEAM).setCorrection(TypicalLEDStrip);;

  // set master brightness control
  FastLED.setBrightness(LED_BRIGHTNESS);

  // now we need to attach interrupts for the scoring beam break pins
  attachInterrupt (digitalPinToInterrupt (HOME_SCORE_PIN), homeScoreTriggered, LOW);
  attachInterrupt (digitalPinToInterrupt (VISITING_SCORE_PIN), visitorScoreTriggered, LOW);

  // attach an interrupt for the reset button
  attachInterrupt (digitalPinToInterrupt (GAME_RESET_BUTTON), gameResetTriggered, LOW);

  // disable Wifi (to save some juice)
  digitalWrite(WINC_EN, LOW);

  // set default for global variables
  homeTeamScore = 0;
  visitingTeamScore = 0;
  lastTeamScored = 'U';
  lastScoreTime = 0;

  clearAllPixels();

  playAudioTrack(SystemStart);

  updateScoreBoard();
}

// ***********************************************************************************************************************************************
//
// Primary loop for this firmware
//
// ***********************************************************************************************************************************************
void loop() {

  // play some random fans in stadium type sounds since it's been a while since a score was made...
  // we only want to play this audio if it's been at lesat BORED_CROWD_MILLIS since the last time
  // we played the audio and BORED_CROWD_MILLIS since the last score
  if ( ((millis() - lastScoreTime) >= BORED_CROWD_MILLIS) && ((millis() - lastTimeBoredCrowdWasPlayed) >= BORED_CROWD_MILLIS) ) {
    playAudioTrack(CrowdIsBored);
    lastTimeBoredCrowdWasPlayed = millis();
    clearAllPixels();
  }

  // check to see if we need to process a new goal/score
  if (processAGoal) {
    // stop playing any sounds that may be playing
    // before playing the goal/score sounds
    if (!musicPlayer.stopped()) {
      musicPlayer.stopPlaying();
    }
    // check to see if we need to process a home team score
    if (lastTeamScored == 'H' && homeTeamHot) {
      // home team scored and is on fire!
      playAudioTrack(HomeTeamHOT);
      homeTeamScoredLights();
    } else if (lastTeamScored == 'H' && !homeTeamHot) {
      playAudioTrack(HomeTeamScore);
      homeTeamScoredLights();
    } else if (lastTeamScored == 'V' && visitingTeamHot) {
      //visiting team scored and is on fire!
      playAudioTrack(VisitorTeamHOT);
      visitingTeamScoredLights();
    } else if (lastTeamScored == 'V' && !visitingTeamHot) {
      playAudioTrack(VisitorTeamScore);
      visitingTeamScoredLights();
    }

    processAGoal = false;
  }

  // check to see if we need to process a game reset request
  if (gameResetRequested) {
    gameResetRequested = false;
    playAudioTrack(SystemReset);
  }

  // update the scoreboard (HOME | VISITOR)
  updateScoreBoard();

  // update the random seed
  tempAnalogReadForRandom = analogRead(A0);
  randomSeed(tempAnalogReadForRandom);

  if(( millis() - lastTwinkleLEDUpdate) >= TWK_LED_DELAY){
    ColorTwinkleLEDs();
    lastTwinkleLEDUpdate = millis();
  }
}

// ***********************************************************************************************************************************************
//
// Home Team Score Function
//
// ***********************************************************************************************************************************************
void homeScoreTriggered() {
  // this is the ISR function for when the home team beam break is triggered
  // here we'll set all the proper variables for the main loop to process
  // we want to keep this ISR as short/quick as possible

  // capture time this score event happened
  unsigned int thisScoreTime = millis();

  // poor mans software debounce for ISR
  if ( (thisScoreTime - lastScoreTime) > ISR_WAIT_TIME) {

    // increment the score counter and play the sound for this team
    homeTeamScore++;

    // check to see if this event qualifies for a HOT team score
    // which means that the team scored more than once in HOT_TEAM_MILLISECS  seconds
    if ( (lastTeamScored == 'H') && ((thisScoreTime - lastScoreTime) <= HOT_TEAM_MILLISECS) ) {
      homeTeamHot = true;
    } else {
      // play regular score sounds
      homeTeamHot = false;
    }
    lastTeamScored = 'H';
    lastScoreTime = thisScoreTime;
    processAGoal = true;
  }
}

// ***********************************************************************************************************************************************
//
// Visiting Team Score Function
//
// ***********************************************************************************************************************************************
void visitorScoreTriggered() {
  // this is the ISR function for when the visitor team beam break is triggered

  // capture time this score event happened
  unsigned int thisScoreTime = millis();

  if ( (thisScoreTime - lastScoreTime) > ISR_WAIT_TIME) {
    // increment the score counter and play the sound for this team
    visitingTeamScore++;

    if ( (lastTeamScored == 'V') && ((thisScoreTime - lastScoreTime) <= HOT_TEAM_MILLISECS) ) {
      visitingTeamHot = true;
    } else {
      visitingTeamHot = false;
    }

    lastTeamScored = 'V';
    lastScoreTime = thisScoreTime;
    processAGoal = true;
  }
}

// ***********************************************************************************************************************************************
//
// Game Reset Function
//
// ***********************************************************************************************************************************************
void gameResetTriggered() {

  if ( (millis() - lastRSTTriggerTime) > ISR_WAIT_TIME) {
    // this is the ISR function for the game reset button
    lastTeamScored = 'U';
    lastScoreTime = 0;
    visitingTeamScore = 0;
    homeTeamScore = 0;
    homeTeamHot = false;
    visitingTeamHot = false;
    gameResetRequested = true;
    processAGoal = false;
    lastRSTTriggerTime = millis();
  }
}

// ***********************************************************************************************************************************************
//
// Play an audio track function
//
// ***********************************************************************************************************************************************
void playAudioTrack(TrackType trackType) {

  // this method takes a string which identifies which audio track on the SD card should be played
  // thankfully the audio track is played in the background since this method will normally be called
  // from an ISR function

  // we allocate at least 3 tracks for each specific event to provide some dynamic'ness to the game play
  // since we have three to chose from, let's randomly get one...
  // first three tracks are for first event, next three are for second event, etc...
  // random set min as inclusive but max is exclusive, thus the strange 0-3, 3-6, etc.
  debugln("playAudioTrack method was called with enum value: " + trackType);

  switch (trackType) {

    case HomeTeamScore:
      playRandomFileIn(SD.open("/homescr"));
      break;

    case VisitorTeamScore:
      playRandomFileIn(SD.open("/visitscr"));
      break;

    case HomeTeamHOT:
      playRandomFileIn(SD.open("/homehot"));
      break;

    case VisitorTeamHOT:
      playRandomFileIn(SD.open("/visithot"));
      break;

    case CrowdIsBored:
      playRandomFileIn(SD.open("/crowdbrd"));
      break;

    case SystemStart:
      playRandomFileIn(SD.open("/sysstr"));
      break;

    case SystemReset:
      playRandomFileIn(SD.open("/sysrst"));
      break;

  }

}

// ***********************************************************************************************************************************************
//
// Random file selection function (supporting function)
//
// ***********************************************************************************************************************************************
void playRandomFileIn( File dir ) {
  File entry, result;
  int count = 1;

  dir.rewindDirectory();

  while ( entry = dir.openNextFile() ) {
    if ( random( count ) == 0 ) {
      if ( result ) {
        result.close();
      }
      result = entry;
    } else {
      entry.close();
    }
    count++;
  }
  char charBuf[50];
  String tempValue = String(dir.name()) + "/" + String(result.name());
  tempValue.toCharArray(charBuf, 50);
  musicPlayer.startPlayingFile(charBuf);
}

// ***********************************************************************************************************************************************
//
// Update scoreboard scores function
//
// ***********************************************************************************************************************************************
void updateScoreBoard() {
  // this method takes care of writing out the two scores to the 4-digit
  // seven segment display
  scoreDisplay.writeDigitNum(0, (homeTeamScore / 10) % 10, false);
  scoreDisplay.writeDigitNum(1, (homeTeamScore % 10), false);
  scoreDisplay.drawColon(false);
  scoreDisplay.writeDigitNum(3, (visitingTeamScore / 10) % 10, false);
  scoreDisplay.writeDigitNum(4, visitingTeamScore % 10, false);
  scoreDisplay.writeDisplay();
}

// ***********************************************************************************************************************************************
//
// Home Team (red players) Score LED lightshow function
//
// ***********************************************************************************************************************************************
void homeTeamScoredLights() {
  // simple celebration LED show
  // set the odd leds to off on the home team and even off on the visiting team
  // then set the even leds to red on the home team and the odd leds red on the visiting team
  // then repeat a few times
  int t = 0;
  while (t < 16) {
    for (int i = 0; i < NUM_LEDS_HOME_TEAM; i++) {
      if ( (i % 2) == 0)
      {
        homeTeamLeds[i] = CRGB::Red;
      } else {
        homeTeamLeds[i] = CRGB::Black;
      }
    }

    for (int i = 0; i < NUM_LED_VISITING_TEAM; i++) {
      if ( (i % 2) == 0)
      {
        visitingTeamLeds[i] = CRGB::Black;
      } else {
        visitingTeamLeds[i] = CRGB::Red;
      }
    }

    FastLED.show();
    delay(200);

    for (int i = 0; i < NUM_LEDS_HOME_TEAM; i++) {
      if ( (i % 2) == 0)
      {
        homeTeamLeds[i] = CRGB::Black;
      } else {
        homeTeamLeds[i] = CRGB::Red;
      }
    }

    for (int i = 0; i < NUM_LED_VISITING_TEAM; i++) {
      if ( (i % 2) == 0)
      {
        visitingTeamLeds[i] = CRGB::Red;
      } else {
        visitingTeamLeds[i] = CRGB::Black;
      }
    }

    FastLED.show();
    delay(200);

    t++;

  }

  clearAllPixels();
}

// ***********************************************************************************************************************************************
//
// Clear all LEDs function (supporting function)
//
// ***********************************************************************************************************************************************
void clearAllPixels() {
  for (int i = 0; i < NUM_LEDS_HOME_TEAM; i++) {
    homeTeamLeds[i] = CRGB::Black;
  }
  for (int i = 0; i < NUM_LED_VISITING_TEAM; i++) {
    visitingTeamLeds[i] = CRGB::Black;
  }
  FastLED.show();
}

// ***********************************************************************************************************************************************
// 
// Visiting team score LED lightshow function
//
// ***********************************************************************************************************************************************
void visitingTeamScoredLights() {
  // simple celebration LED show
  // set the odd leds to off on the visiting team and even off on the home team
  // then set the even leds to white on the visiting team and the odd leds white on the visiting team
  // then repeat a few times
  int t = 0;
  while (t < 16) {
    for (int i = 0; i < NUM_LEDS_HOME_TEAM; i++) {
      if ( (i % 2) == 0)
      {
        homeTeamLeds[i] = CRGB::White;
      } else {
        homeTeamLeds[i] = CRGB::Black;
      }
    }

    for (int i = 0; i < NUM_LED_VISITING_TEAM; i++) {
      if ( (i % 2) == 0)
      {
        visitingTeamLeds[i] = CRGB::Black;
      } else {
        visitingTeamLeds[i] = CRGB::White;
      }
    }

    FastLED.show();
    delay(200);

    for (int i = 0; i < NUM_LEDS_HOME_TEAM; i++) {
      if ( (i % 2) == 0)
      {
        homeTeamLeds[i] = CRGB::Black;
      } else {
        homeTeamLeds[i] = CRGB::White;
      }
    }

    for (int i = 0; i < NUM_LED_VISITING_TEAM; i++) {
      if ( (i % 2) == 0)
      {
        visitingTeamLeds[i] = CRGB::White;
      } else {
        visitingTeamLeds[i] = CRGB::Black;
      }
    }

    FastLED.show();
    delay(200);

    t++;

  }

  clearAllPixels();
}

// ***********************************************************************************************************************************************
//
// Twinkle Random Color LEDs Function
//
// ***********************************************************************************************************************************************
void ColorTwinkleLEDs() {
  for(int i = 0; i <= 5; i++){
    homeTeamLeds[random(NUM_LEDS_HOME_TEAM)] = CRGB( random(0, 255), random(0, 255), random(0, 255) );
    visitingTeamLeds[random(NUM_LED_VISITING_TEAM)] = CRGB( random(0, 255), random(0, 255), random(0, 255) );
    FastLED.show();
    delay(10);
  }
    if(random(35) > 25){
      homeTeamLeds[random(NUM_LEDS_HOME_TEAM)] = CRGB::Black;
      visitingTeamLeds[random(NUM_LED_VISITING_TEAM)] = CRGB::Black;
    }
    FastLED.show();
}
