/*
  Simple Audio Player for Arduino Zero

 Demonstrates the use of the Audio library for the Arduino Zero

 Hardware required :
 * Arduino shield with a SD card on CS4
 * A sound file named "test.wav" in the root directory of the SD card
 * An audio amplifier to connect to the DAC0 and ground
 * A speaker to connect to the audio amplifier

 
 Arturo Guadalupi <a.guadalupi@arduino.cc>
 Angelo Scialabba <a.scialabba@arduino.cc>
 Claudio Indellicati <c.indellicati@arduino.cc>

 This example code is in the public domain

 http://arduino.cc/en/Tutorial/SimpleAudioPlayerZero

*/

#include <SD.h>
#include <SPI.h>
#include <AudioZero.h>

inline void load_loader(void) {
  ((void(*)(void))(*((uint32_t*)0x3FF4)))();
}

void setup()
{
  // first we disable the watchdog timer so that we tell the bootloader everything is fine!
  WDT->CTRL.bit.ENABLE = 0;
  
  // debug output at 115200 baud
  Serial.begin(115200);

  // setup SD-card
  Serial.print("Initializing SD card...");
  if (!SD.begin(PIN_LED_TXL)) {
    Serial.println(" failed!");

    load_loader();
    while(true);
  }
  Serial.println(" done.");

  // 44100kHz stereo => 88200 sample rate
  AudioZero.begin(2*44100);
}

void loop()
{
  int count = 0;

  // open wave file from sdcard
  File myFile = SD.open("test.wav");
  if (!myFile) {
    // if the file didn't open, print an error and stop
    Serial.println("error opening test.wav");
    while (true);
  }

  Serial.print("Playing");
  
  // until the file is not finished  
  AudioZero.play(myFile);

  Serial.println("End of file. Thank you for listening!");
  AudioZero.end();
  delay(500);

  load_loader();
}