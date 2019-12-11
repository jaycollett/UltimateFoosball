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

//
// Define our global params for the program
//
#define   NUM_LEDS_HOME_TEAM      60
#define   NUM_LED_VISITOR_TEAM    60
#define   VISITOR_LED_DATA_PIN    5
#define   HOME_TEAM_LED_DATA_PIN  6
#define   LED_BRIGHTNESS          128
#define   HOME_SCORE_PIN          15
#define   VISITOR_SCORE_PIN       19
#define   HOT_TEAM_MILLISECS      10000 // if a team scores again within 10 seconds, play a hot team sound clip
#define   GAME_RESET_BUTTON       13
#define   VS1053_RESET            -1    // VS1053 reset pin (not used!)
#define   WINC_EN                 2     // enable pin for WINC1500
#define   CARDCS                  5     // Card chip select pin
#define   VS1053_CS               6     // VS1053 chip select pin (output)
#define   VS1053_DREQ             9     // VS1053 Data request, ideally an Interrupt pin
#define   VS1053_DCS              10    // VS1053 Data/command select pin (output)
#define   BORED_CROWD_MILLIS      45000 // 45 seconds must lapse before we play a bored crowd sound clip
#define   DEBUG


// Setup our debug printing
#ifdef DEBUG
#define debug(x)     Serial.print(x)
#define debugln(x)   Serial.println(x)
#else
#define debug(x)     // define empty, so macro does nothing
#define debugln(x)
#endif

//
// Init objects
//
CRGB homeTeamLeds[NUM_LEDS_HOME_TEAM];
CRGB vistorTeamLeds[NUM_LED_VISITOR_TEAM];
Adafruit_VS1053_FilePlayer musicPlayer = Adafruit_VS1053_FilePlayer(VS1053_RESET, VS1053_CS, VS1053_DCS, VS1053_DREQ, CARDCS);
Adafruit_7segment scoreDisplay = Adafruit_7segment();

//
// Init global variables
//
unsigned int homeTeamScore;
unsigned int visitorTeamScore;
char lastTeamScored;
unsigned int lastScoreTime;
enum TrackType { HomeTeamScore, VisitorTeamScore, HomeTeamHOT, VisitorTeamHOT, CrowdIsBored, SystemStart, SystemReset };
unsigned int tempAnalogReadForRandom;

// EVENTS to be coded for
//
// 1. Home or Visitor Goal
// 2. Multiple goals within x seconds for a single team, ON FIRE team
// 3. No goals within x seconds, the crowd is growing restless
// 4. Random smack talk or random anncounement if no goal in x seconds?
// 5. Start-up/game start anncoucement
// 6. ball in play annoucement (if beam breaks on ball entry)
//

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
  musicPlayer.setVolume(12, 12);
  musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT);  // DREQ int

  // build the object of LEDs for the home team array
  FastLED.addLeds<NEOPIXEL, HOME_TEAM_LED_DATA_PIN>(homeTeamLeds, NUM_LEDS_HOME_TEAM).setCorrection(TypicalLEDStrip);;


  // build the object of LEDs for the visiotr team array
  FastLED.addLeds<NEOPIXEL, VISITOR_LED_DATA_PIN>(vistorTeamLeds, NUM_LED_VISITOR_TEAM).setCorrection(TypicalLEDStrip);;

  // set master brightness control
  FastLED.setBrightness(LED_BRIGHTNESS);

  // now we need to attach interrupts for the scoring beam break pins
  attachInterrupt (digitalPinToInterrupt (HOME_SCORE_PIN), homeScoreTriggered, LOW);
  attachInterrupt (digitalPinToInterrupt (VISITOR_SCORE_PIN), visitorScoreTriggered, LOW);

  // attach an interrupt for the reset button
  attachInterrupt (digitalPinToInterrupt (GAME_RESET_BUTTON), gameResetTriggered, LOW);

  // disable Wifi (to save some juice)
  digitalWrite(WINC_EN, LOW);

  // set default for global variables
  homeTeamScore = 0;
  visitorTeamScore = 0;
  lastTeamScored = 'U';
  lastScoreTime = 0;

  playAudioTrack(SystemStart);
}


void loop() {

  // TODO: Figure out how to make the LEDs do something cool while we wait for an event/interrupt

 // Play some random fans in stadium type sounds since it's been a while since a score was made...
 if((millis() - lastScoreTime) >= BORED_CROWD_MILLIS){
  playAudioTrack(CrowdIsBored);
 }
 
 // update the random seed
  tempAnalogReadForRandom = analogRead(A0);
  randomSeed(tempAnalogReadForRandom);
}


void homeScoreTriggered() {
  // this is the ISR function for when the home team beam break is triggered
  debugln("Home score interrupt was triggered...");

  // capture time this score event happened
  unsigned int thisScoreTime = millis();

  // increment the score counter and play the sound for this team
  homeTeamScore++;

  // check to see if this event qualifies for a HOT team score
  // which means that the team scored more than once in HOT_TEAM_MILLISECS  seconds
  if ( (lastTeamScored == 'H') && ((thisScoreTime - lastScoreTime) <= HOT_TEAM_MILLISECS) ) {
    debugln("Home team is HOT, playing audio for hot home team...");
    playAudioTrack(HomeTeamScore);
  }else{
     // play regular score sounds
     debugln("Playing home team score sound..");
     playAudioTrack(HomeTeamScore);
  }
  lastTeamScored = 'H';
  lastScoreTime = thisScoreTime;

  // update the scoreboard (HOME | VISITOR)
  updateScoreBoard();
}

void visitorScoreTriggered() {
  // this is the ISR function for when the visitor team beam break is triggered
  debugln("Visitor score interrupt was triggered...");

  // capture time this score event happened
  unsigned int thisScoreTime = millis();

  // increment the score counter and play the sound for this team
  visitorTeamScore++;

  if ( (lastTeamScored == 'V') && ((thisScoreTime - lastScoreTime) <= HOT_TEAM_MILLISECS) ) {
    debugln("Visiting team is HOT, playing sounds for hot visiting team");
    playAudioTrack(VisitorTeamHOT);
  }else{
    debugln("Playing visiting team score sound...");
    playAudioTrack(VisitorTeamScore);
  }

  lastTeamScored = 'V';
  lastScoreTime = thisScoreTime;

  // update the scoreboard (HOME | VISITOR)
  updateScoreBoard();
}

void gameResetTriggered() {
  // this is the ISR function for the game reset button
  debugln("gameReset interrupt was triggered...");

  lastTeamScored = 'U';
  lastScoreTime = 0;
  visitorTeamScore = 0;
  homeTeamScore = 0;

  debugln("Playing system reset sound...");
  playAudioTrack(SystemReset);
}


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
  debug("Selected random track to play: ");
  debugln(result.name());
  musicPlayer.startPlayingFile(result.name());
}

void updateScoreBoard(){
  scoreDisplay.writeDigitNum(0, (homeTeamScore / 10) % 10, false);
  scoreDisplay.writeDigitNum(1, (homeTeamScore % 10), false);
  scoreDisplay.drawColon(false);
  scoreDisplay.writeDigitNum(3, (visitorTeamScore / 10) % 10, false);
  scoreDisplay.writeDigitNum(4, visitorTeamScore % 10, false);
  scoreDisplay.writeDisplay();
}
