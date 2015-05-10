/*
 * Rocket.ino
 * Code for sensor system to be incorporated into a model rocket to take
 * real world data during flight. Currently supports altimeter/temperature
 * sensor and EEPROM chip to store measurements. Will add support to take
 * measurements from accelerometer as well.
 *
 * David Bonsall
 * Last update: 9/17/14
 * v1.0
**/


#include <Wire.h>
#include "MPL3115A2.h"
#include <stdint.h>
#include <stdio.h>

//general definitions
#define EE_ADDR			0x50
#define LED				13
#define NUM_COMMANDS	8
#define DEVICE_INFO		"RocketSenseV1"
#define COUNTDOWN_SEC	120

MPL3115A2 myPressure;	//pressure sensor object

//command array
typedef void (*ExternalCommand)(void);
ExternalCommand commands[NUM_COMMANDS] = {&ec_beginLog, &ec_continueLog, &ec_endLog, &ec_dumpEEPROM, &ec_reformatEEPROM,
					  &ec_dumpRaw, &ec_getDeviceInfo, &ec_logWithDelay};

boolean isLogging = false;	//current device state
uint16_t  currentMemoryAddress = 0;	//current write address of the eeprom
byte rawDumpPreamble[3] = {0x01, 0x02, 0x03};
byte rawDumpEOT[3] = {0x03, 0x02, 0x01};

void setup()
{
	Serial.begin(115200);	//init Serial
	pinMode(LED, OUTPUT);	//init Status LED
	Wire.begin();			//init i2c
	myPressure.begin();		//init pressure sensor

	//sensor config
	myPressure.setModeAltimeter();		//set to output altitude
	myPressure.setOversampleRate(7);	//recommended value of 128; see example
	myPressure.enableEventFlags();		//required; see example
}

void loop()
{
	if (Serial.available())	//check to see if there are any commands in the input stream
	{
		uint8_t cmd = (uint8_t)Serial.read();	//read the command
		if(!(cmd >= NUM_COMMANDS))				//make sure the command is within the range
			(commands[cmd])();					//execute the command at the specified index
	}

	if(isLogging)	//if we are currently logging
	{
		uint32_t time = (uint32_t)millis();				//get the time and cast from unsigned long to u int to save memory space
		float altitude = myPressure.readAltitude();		//get the altitude
		float temperature = myPressure.readTemp();		//get the temperature

		//convert the floats to byte arrays
		uint8_t altitudeBytes[4];					//create destination array for altitude
		memcpy(altitudeBytes, &altitude, 4);		//copy
		uint8_t temperatureBytes[4];				//create destination array for altitude
		memcpy(temperatureBytes, &temperature, 4);	//copy

		//write to memory
		Wire.beginTransmission(EE_ADDR);				//start transmission with the eeprom
		Wire.write((uint8_t)(currentMemoryAddress>>8));	//msb of address
		Wire.write((uint8_t)currentMemoryAddress); 		//lsb of address

		//write time
		Wire.write((uint8_t)(time>>24));	//write msb to the eeprom
		Wire.write((uint8_t)(time>>16));	//write most significant center byte
		Wire.write((uint8_t)(time>>8));		//write least significant center byte
		Wire.write((uint8_t)time);			//write lsb

		//write altitude
		Wire.write(altitudeBytes, 4);

		//write temperature
		Wire.write(temperatureBytes, 4);
		
		Wire.write(0xF0);
		Wire.write(0xF0);
		Wire.write(0xF0);
		Wire.write(0xF0);

		Wire.endTransmission();			//end transmission with the eeprom
		currentMemoryAddress += 16;		//12 bytes transferred, plus four "imaginary" to make page writes work out....

		//Serial debug information
		Serial.print("Logged at ");
		Serial.println(millis(), DEC);
	}

}

void ec_beginLog()
{
	currentMemoryAddress = 0;	//reset memory address
	isLogging = true;			//start logging
}

void ec_continueLog()
{
	isLogging = true;	//start logging
}

void ec_endLog()
{
	isLogging = false;	//stop logging
}

void ec_dumpEEPROM()
{
	Serial.println("Beginning of data transmission");
	Wire.beginTransmission(EE_ADDR);	//start the transmission
	Wire.write(0x00);					//msb of start address
	Wire.write(0x00);					//lsb of start address
	Wire.endTransmission();				//end transmission

	uint16_t byteNum = 0;				//keep track of memory address for table output
	for (uint16_t j = 0; j < 4096; j++)	//read the entire chip (0x8000 bytes total / 8 bytes per read = 4096)
	{
		Wire.requestFrom(EE_ADDR, 8);	//get 8 bytes from the eeprom

		//print the memory address for the row
		Serial.print(byteNum, HEX);
		Serial.print(":\t");

		for (uint8_t i = 0; i < 8; i++)	//get each individual byte
		{
			if (!(Wire.available())) break;	//if nothing was read from the chip, don't print anything
			uint8_t data = Wire.read();		//get byte from the buffer

			//print the data value
			Serial.print(data, HEX);
			Serial.print(" ");
		}
		Serial.println();	//go to the next line
		byteNum += 8;		//increment byte counter
		delay(1);			//wait to avoid overloading the eeprom when reading, (probably unnecessary)
	}
	Serial.println("End of data transmission");
}

//writes all bytes of the eeprom to 0xFF to "erase" or reformat eeprom
//writes in groups of 16; 32 or 64 byte groups were not functioning correctly, possibly due to Wire buffer behaviour
void ec_reformatEEPROM()
{	
	Serial.println("Reformat EEPROM (all data will be erased)?");	//confirm that the user wants to erase data
	while (!(Serial.available()));									//wait until we get a response back from computer
	char val = (char)Serial.read();									//get the answer
	
	if (!(val == 'y' || val == 'Y'))	//if the answer is not yes, cancel
	{
		Serial.println("Reformat cancelled");
		return;
	}

	//reformat
	for (uint16_t curAddr = 0; curAddr < uint16_t(0x8000); curAddr += 16)	//write all the bytes in groups of 16
	{
		//send debug info
		if (curAddr%64 == 0)	//if we are at the beginning of a page, print debug info
		{
			Serial.print("Writing page");
			Serial.print(curAddr/64 + 1);
			Serial.println(" of 512...\t");
		}
		
		Wire.beginTransmission(EE_ADDR);	//start the transmission
		Wire.write((uint8_t)(curAddr>>8));	//send the msb
		Wire.write((uint8_t)curAddr);		//send the lsb
		
		for (uint8_t i = 0; i < 16; i ++)	//write 16 bytes to 0xFF
		{
			Wire.write(0xFF);
		}
		
		Wire.endTransmission();			//end the transmission
		//Wire.flush();
		
		delay(5);						//give time for the page to write
	}
	Serial.println("Finished reformat");
	
}

void ec_dumpRaw()
{
	Serial.write(rawDumpPreamble, 3);
	Wire.beginTransmission(EE_ADDR);	//start the transmission
	Wire.write(0x00);					//msb of start address
	Wire.write(0x00);					//lsb of start address
	Wire.endTransmission();				//end transmission

	for (uint16_t j = 0; j < 4096; j++)	//read the entire chip (0x8000 bytes total / 8 bytes per read = 4096)
	{
		Wire.requestFrom(EE_ADDR, 8);	//get 8 bytes from the eeprom

		for (uint8_t i = 0; i < 8; i++)	//get each individual byte
		{
			if (!(Wire.available())) break;	//if nothing was read from the chip, don't print anything
			uint8_t data = Wire.read();		//get byte from the buffer

			//print the data value
			Serial.write(data);
		}
		delay(1);			//wait to avoid overloading the eeprom when reading, (probably unnecessary)
	}
	Serial.write(rawDumpEOT, 3);
}

void ec_getDeviceInfo()
{
	Serial.println(DEVICE_INFO);
}

void ec_logWithDelay()
{
	Serial.println("Beginning Countdown...");
	for (uint8_t i = COUNTDOWN_SEC; i >=0; i++)
	{
		Serial.println(i + "...");
		delay(1000);
		if (Serial.available())	//see if there is any command
		{
			uint8_t cmd = (uint8_t)Serial.read();	//read the command
			if (cmd == 2)	//if the command is the end log command, end the countdown and return
				return;
		}
	}
	ec_beginLog();
}



