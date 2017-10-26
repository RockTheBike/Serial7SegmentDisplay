#define BAUDRATE        57600
/* Serial 7 Segment Display Firmware
 originally Version: 3.0.1 By: Jim Lindblom (SparkFun Electronics)
 Description: This firmware goes on the SparkFun Serial 7-Segment displays.
 https://www.sparkfun.com/search/results?term=serial+7+segment&what=products
 Note: To use the additional pins, PB6 and PB7, on the ATmega328 we have to add
 some maps to the pins_arduino.h file. This allows Arduino to identify PB6 as
 digital pin 22, and PB7 as digital pin 23. Because the Serial 7-Segment runs on
 the ATmega328's internal oscillator, these two pins open up for our use.  */

#include <Wire.h>  // Handles I2C
#include <EEPROM.h>  // Brightness, Baud rate, and I2C address are stored in EEPROM
#include "settings.h"  // Defines command bytes, EEPROM addresses, display data
// This code uses the SevSeg library, which can be donwnloaded from:
// https://github.com/sparkfun/SevSeg
// git@github.com:sparkfun/SevSeg.git
#include "SevSeg.h" //Library to control generic seven segment displays

SevSeg myDisplay; //Create an instance of the object

//Global variables
unsigned int analogValue6 = 0; //These are used in analog meter mode
unsigned int analogValue7 = 0;
unsigned char deviceMode; // This variable is useds to select which mode the device should be in
unsigned char commandMode = 0;  // Used to indicate if a commandMode byte has been received

// Struct for circular data buffer data received over UART, SPI and I2C are all sent into a single buffer
struct dataBuffer
{
  unsigned char data[BUFFER_SIZE];  // THE data buffer
  unsigned int head;  // store new data at this index
  unsigned int tail;  // read oldest data from this index
}
buffer;  // our data buffer is creatively named - buffer

// Struct for 4-digit, 7-segment display
// Stores display value (digits),  decimal status (decimals) for each digit, and cursor for overall display
struct display
{
  char digits[4];
  unsigned char decimals;
  unsigned char cursor;
}
display;  // displays be displays

void setup()
{
  setupDisplay(); //Initialize display stuff (common cathode, digits, brightness, etc)

  //We need to check emergency after we have initialized the display so that we can use the display during an emergency reset
  setupTimer();  // Setup timer to control interval reading from buffer
  Serial.begin(BAUDRATE);

  interrupts();  // Turn interrupts on, and les' go

  //Preload the display buffer with a default
  display.digits[0] = 1;
  display.digits[1] = 2;
  display.digits[2] = 3;
  display.digits[3] = 4;
}

// The display is constantly PWM'd in the loop()
void loop() {
  myDisplay.DisplayString(display.digits, display.decimals); //(numberToDisplay, decimal point location)
  serialEvent(); //Check the serial buffer for new data
}

// This is effectively the UART0 byte received interrupt routine
// But not quite: serialEvent is only called after each loop() interation
void serialEvent() {
  while (Serial.available())
  {
    unsigned int i = (buffer.head + 1) % BUFFER_SIZE;  // read buffer head position and increment
    unsigned char c = Serial.read();  // Read data byte into c, from UART0 data register

    if (i != buffer.tail)  // As long as the buffer isn't full, we can store the data in buffer
    {
      buffer.data[buffer.head] = c;  // Store the data into the buffer's head
      buffer.head = i;  // update buffer head, since we stored new data
    }
  }
}

// updateBufferData(): This beast of a function is called by the Timer 1 ISR if there is new data in the buffer.
// If the data controls display data, that'll be updated.
// If the data relates to a command, commandmode will be set accordingly or a command
// will be executed from this function.
void updateBufferData()
{

  // First we read from the oldest data in the buffer
  unsigned char c = buffer.data[buffer.tail];
  buffer.tail = (buffer.tail + 1) % BUFFER_SIZE;  // and update the tail to the next oldest

  // if the last byte received wasn't a command byte (commandMode=0)
  // and if the data is displayable (0-0x76 or 0x78), the display will be updated
  if ((commandMode == 0) && ((c < 0x76) || (c == 0x78)))
  {
    display.digits[display.cursor] = c;  // just store the read data into the cursor-active digit
    display.cursor = ((display.cursor + 1) % 4);  // Increment cursor, set back to 0 if necessary
  }
  else if ((c == RESET_CMD) && (!commandMode))  // If the received char is the reset command
  {
    for(int i = 0 ; i < 4 ; i++)
      display.digits[i] = 'x';  // clear all digits
    display.decimals = 0;  // clear all decimals
    display.cursor = 0;  // reset the cursor
  }
  else if (commandMode != 0)  // Otherwise, if data is non-displayable and we're in a commandMode
  {
    switch (commandMode)
    {
    case DECIMAL_CMD:  // Decimal setting mode
      display.decimals = c;  // decimals are set by one byte
      break;
    case BRIGHTNESS_CMD:  // Brightness setting mode
      EEPROM.write(BRIGHTNESS_ADDRESS, c);    // write the new value to EEPROM
      myDisplay.SetBrightness(c); //Set the display to this brightness level
      break;
    case CURSOR_CMD:  // Set the cursor
      if (c <= 3)  // Limited error checking, if >3 cursor command will have no effect
        display.cursor = c;  // Update the cursor value
      break;
    case DIGIT1_CMD:  // Single-digit control for digit 1
      display.digits[0] = c | 0x80;  // set msb to indicate single digit control mode
      break;
    case DIGIT2_CMD:  // Single-digit control for digit 2
      display.digits[1] = c | 0x80;
      break;
    case DIGIT3_CMD:  // Single-digit control for digit 3
      display.digits[2] = c | 0x80;
      break;
    case DIGIT4_CMD:  // Single-digit control for digit 4
      display.digits[3] = c | 0x80;
      break;
    }
    // Leaving commandMode
    // !!! If the commandMode isn't a valid command, we'll leave command mode, should be checked below?
    commandMode = 0;
  }
  else  // Finally, if we weren't in command mode, if the byte isn't displayable, we'll enter command mode
  {
    commandMode = c;  // which command mode is reflected by value of commandMode
  }
}

//Sets up the hardware pins to control the 7 segments and display type
void setupDisplay()
{
  //Determine the display brightness
  byte settingBrightness = EEPROM.read(BRIGHTNESS_ADDRESS);
  if(settingBrightness > BRIGHTNESS_DEFAULT) {
    settingBrightness = BRIGHTNESS_DEFAULT; //By default, unit will be brightest
    EEPROM.write(BRIGHTNESS_ADDRESS, settingBrightness);
  }
  myDisplay.SetBrightness(settingBrightness); //Set the display to 100% bright

  // Set the initial state of displays and decimals 'x' =  off
  display.digits[0] = 'x';
  display.digits[1] = 'x';
  display.digits[2] = 'x';
  display.digits[3] = 'x';
  display.decimals = 0x00;  // Turn all decimals off
  display.cursor = 0;  // Set cursor to first (left-most) digit

  buffer.head = 0;  // Initialize buffer values
  buffer.tail = 0;

  //This pinout is for the original Serial7Segment layout
  int digit1 = 16; // DIG1 = A2/16 (PC2)
  int digit2 = 17; // DIG2 = A3/17 (PC3)
  int digit3 = 3;  // DIG3 = D3 (PD3)
  int digit4 = 4;  // DIG4 = D4 (PD4)

  //Declare what pins are connected to the segments
  int segA = 8;  // A = D8 (PB0)
  int segB = 14; // B = A0 (PC0)
  int segC = 6;  // C = D6 (PD6), shares a pin with colon cathode
  int segD = A1; // D = A1 (PC1)
  int segE = 23; // E = PB7 (not a standard Arduino pin: Must add PB7 as digital pin 23 to pins_arduino.h)
  int segF = 7;  // F = D7 (PD6), shares a pin with apostrophe cathode
  int segG = 5;  // G = D5 (PD5)
  int segDP= 22; //DP = PB6 (not a standard Arduino pin: Must add PB6 as digital pin 22 to pins_arduino.h)

  int digitColon = 2; // COL-A = D2 (PD2) (anode of colon)
  int segmentColon = 6; // COL-C = D6 (PD6) (cathode of colon), shares a pin with C
  int digitApostrophe = 9; // APOS-A = D9 (PB1) (anode of apostrophe)
  int segmentApostrophe = 7; // APOS-C = D7 (PD7) (cathode of apostrophe), shares a pin with F

  int numberOfDigits = 4; //Do you have a 2 or 4 digit display?

  int displayType = COMMON_ANODE; //SparkFun 10mm height displays are common anode

  //Initialize the SevSeg library with all the pins needed for this type of display
  myDisplay.Begin(displayType, numberOfDigits,
  digit1, digit2, digit3, digit4,
  digitColon, digitApostrophe,
  segA, segB, segC, segD, segE, segF, segG,
  segDP,
  segmentColon, segmentApostrophe);
}

// The display data is updated on a Timer interrupt
ISR(TIMER1_COMPA_vect)
{
  noInterrupts();

  // if head and tail are not equal, there's data to be read from the buffer
  if (buffer.head != buffer.tail)
    updateBufferData();  // updateBufferData() will update the display info, or peform special commands

  interrupts();
}

// setupTimer(): Set up timer 1, which controls interval reading from the buffer
void setupTimer()
{
  // Timer 1 is se to CTC mode, 16-bit timer counts up to 0xFF
  TCCR1B = (1<<WGM12) | (1<<CS10);
  OCR1A = 0x00FF;
  TIMSK1 = (1<<OCIE1A);  // Enable interrupt on compare
}
