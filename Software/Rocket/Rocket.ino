/*
 * Rocket.ino
 * Code for sensor system to be incorporated into a model rocket to take
 * real world data during flight. Currently supports EEPROM chip to store
 * measurements. Will add support to take measurements from accelerometer
 * as well.
 *
 * David Bonsall
 * Last update: 5/27/15
 * v1.0
**/

//turn the I2C frequency up to 400kHz (fast mode)
//#define TWI_FREQ  400000L

extern "C"
{
    #include <MyTWI.h>
}
#include <stdint.h>
#include <stdio.h>
#include "accelerometer.h"

//general definitions
#define EE_ADDR			0x50
#define AXL_ADDR                0x19
#define LED				13
#define NUM_COMMANDS	10
#define DEVICE_INFO		"RocketSenseV1"
#define COUNTDOWN_SEC	120
#define ACCEL_SCALE_FACTOR    (0.007185059f)    //24g / 2^15 * 9.81

//Argument definitions to determine whether or not to send a stop condition when using MyTWI
#define I2C_NO_STOP        0
#define I2C_YES_STOP       1

//command array
typedef void (*ExternalCommand)(void);
ExternalCommand commands[NUM_COMMANDS] = {&ec_beginLog, &ec_continueLog, &ec_endLog, &ec_dumpEEPROM, &ec_reformatEEPROM,
					  &ec_dumpRaw, &ec_dumpFormatted, &ec_getDeviceInfo, &ec_logWithDelay, &ec_displayHelp};

//globals
boolean isLogging = false;	//current device state
uint16_t  currentMemoryAddress = 0;	//current write address of the eeprom

//preamble and end of transmission signals for dump raw mode
const byte rawDumpPreamble[3] = {0x01, 0x02, 0x03};
const byte rawDumpEOT[3] = {0x03, 0x02, 0x01};

//buffer to hold write address and data to write to eeprom chip to allow for 64 byte page writes at one time
uint8_t eepromWriteBuffer[66];
uint8_t currentBufferPos = 0;

void setup()
{
	Serial.begin(115200);	//init Serial
	pinMode(LED, OUTPUT);	//init Status LED
        axl_init();             //init the accelerometer
        
        //print out startup info to the serial terminal
        Serial.println("Device Reset...");
        Serial.print(DEVICE_INFO);
        Serial.println(" send 'h' for help");
}

void loop()
{
	proccessCommand();
        
        if (isLogging)    //if we are currently logging
        {
            if (currentBufferPos == 0)    //if the buffer needs the write address added to it
            {
                eepromWriteBuffer[currentBufferPos++] = (uint8_t)(currentMemoryAddress>>8);    //msb
                eepromWriteBuffer[currentBufferPos++] = (uint8_t)(currentMemoryAddress);        //lsb
            }
            
            uint16_t time = (uint16_t)millis();    //get the time and cast it to 2 bytes
            
            //write the 2 byte time
            eepromWriteBuffer[currentBufferPos++] = (uint8_t)(time>>8);
            eepromWriteBuffer[currentBufferPos++] = (uint8_t)(time);
            
            //write 6 placeholder bytes
            /*eepromWriteBuffer[currentBufferPos++] = 0xF0;
            eepromWriteBuffer[currentBufferPos++] = 0xF0;
            eepromWriteBuffer[currentBufferPos++] = 0xF0;
            eepromWriteBuffer[currentBufferPos++] = 0xF0;
            eepromWriteBuffer[currentBufferPos++] = 0xF0;
            eepromWriteBuffer[currentBufferPos++] = 0xF0;*/
            
            //write the acceration
            axl_get3DAcceleration(currentBufferPos, eepromWriteBuffer);    //put the acceration data into the write buffer
            currentBufferPos += 6;    //account for the 6 bytes of acceration data writen to the buffer
            
            if ((currentBufferPos-2) % 64 == 0)    //when it is time to write to the eeprom; -2 is to account for the 2 byte address for the eeprom write
            {
                //write to memory
                eepromWrite(eepromWriteBuffer, 66);    //write all of the data to eeprom
                
                currentMemoryAddress += 64;    //increment the current eeprom memory address
                currentBufferPos = 0;    //reset the buffer index
                
                //testing purposes only
                /*for (uint8_t i = 0; i < 64; i++)
                    Serial.println(eepromWriteBuffer[i], HEX);
                isLogging = 0;*/
            }
            //delay(1);    //probably not neccesary for final, just to limit log speed
            
            //Loging info, heartbeat of sorts.....
            Serial.print("Logged at ");
            Serial.println(millis(), HEX);
        }
}

/**
 ============================================================================================
 =================================EEPROM COMMANDS============================================
 ============================================================================================
**/

void eepromWrite(uint8_t* data, uint8_t quantity)    //TODO: check to make sure pointer is implemented correctly
{
    twi_writeTo(EE_ADDR, data, quantity, 1, I2C_YES_STOP);
}

void eepromRead(uint8_t* rxBuffer, uint8_t quantity)    //TODO: check to make sure pointer is implemented correctly
{
    twi_readFrom(EE_ADDR, rxBuffer, quantity, I2C_YES_STOP);
}

/**
 ============================================================================================
 ===============================AXCELEROMETER COMMANDS=======================================
 ============================================================================================
**/
void axl_writeRegister(uint8_t regAddr, uint8_t data)
{
    uint8_t temp[2] = {regAddr, data};       //put the address and the data into a holder array
    twi_writeTo(AXL_ADDR, temp, 2, 1, I2C_YES_STOP);    //write the data to the specified register
}

uint8_t axl_readRegister(uint8_t regAddr)
{
    uint8_t temp[1] = {regAddr};    //make an array to put the address into
    twi_writeTo(AXL_ADDR, temp, 1, 1, I2C_NO_STOP);    //write the register number but do not stop
    twi_readFrom(AXL_ADDR, temp, 1, I2C_YES_STOP);     //read the value into the temp array
    return temp[0];    //return the value
}

void axl_readMultiple(uint8_t startRegAddr, uint8_t* data, uint8_t num)
{
    uint8_t temp[1] = {startRegAddr | INCREMENT_MASK};    //make an array to put the address into. also set address to auto increment with each byte read
    twi_writeTo(AXL_ADDR, temp, 1, 1, I2C_NO_STOP);       //write the register address
    twi_readFrom(AXL_ADDR, data, num, I2C_YES_STOP);      //read the data into the given buffer
}

void axl_get3DAcceleration(uint8_t startIndex, uint8_t* dataBuffer)
{
    while ((axl_readRegister(STATUS_REG) & 0b00000111) != 0x03);    //wait for there to be nex available data for the x, y, and z registers
    
    //for debuging purposes, should probably be taken out for deployed version
    if (axl_readRegister(STATUS_REG) & 0b01110000)    //check if any of the axis are averrun
        Serial.println("Overrun Occured");
        
    //Read the 6 axcelleration bytes from the axcelerometer and put them into a data buffer at a given start index
    //allows for direct read into the eeprom write buffer.
    axl_readMultiple(OUT_X_L, &(dataBuffer[startIndex]), 6);    //still need to test
}

void axl_init()
{
    axl_writeRegister(CTRL_REG1, 0b00111111);    //Normal pwr Mode; 1kHz data rate; xyz eneable
    //delay(1);    //dont think I need it
    axl_writeRegister(CTRL_REG4, 0b10110000);    //block read, +-24g range
}

/**
 ============================================================================================
 ===============================EXTERNAL COMMANDS============================================
 ============================================================================================
**/

void ec_beginLog()
{
	currentMemoryAddress = 0;	//reset memory address
        currentBufferPos = 0;           //reset the buffer position
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

        uint8_t addrBuffer[2] = {0,0};    //starting address {msb, lsb}
        eepromWrite(addrBuffer, 2);        //write the address to the eeprom
        
        uint8_t rxBuffer[8];
	uint16_t byteNum = 0;				//keep track of memory address for table output
	for (uint16_t j = 0; j < 4096; j++)	//read the entire chip (0x8000 bytes total / 8 bytes per read = 4096)
	{
                eepromRead(rxBuffer, 8);

		//print the memory address for the row
		Serial.print(byteNum, HEX);
		Serial.print(":\t");

		for (uint8_t i = 0; i < 8; i++)	//get each individual byte
		{       
			//print the data value
                        Serial.print(rxBuffer[i], HEX);
			Serial.print(" ");
		}
		Serial.println();	//go to the next line
		byteNum += 8;		//increment byte counter
	}
	Serial.println("End of data transmission");
}

//writes all bytes of the eeprom to 0xFF to "erase" or reformat eeprom
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
	for (uint16_t curAddr = 0; curAddr < uint16_t(0x8000); curAddr += 64)	//write all the bytes in groups of 16
	{
		//send debug info
		if (curAddr%64 == 0)	//if we are at the beginning of a page, print debug info
		{
			Serial.print("Writing page");
			Serial.print(curAddr/64 + 1);
			Serial.println(" of 512...\t");
		}
		
                currentBufferPos = 0;
                eepromWriteBuffer[currentBufferPos++] = (uint8_t)(curAddr>>8);    //msb
                eepromWriteBuffer[currentBufferPos++] = (uint8_t)(curAddr);        //lsb
                for (;currentBufferPos < 66; currentBufferPos++)    //put 0xFF into the buffer to write to the EEPROM
                {
                    eepromWriteBuffer[currentBufferPos] = 0xFF;
                }
                eepromWrite(eepromWriteBuffer, 66);    //write the reformatted page to the eeprom
                delay(5);    //allow for page write
                
	}
	Serial.println("Finished reformat");
	
}

void ec_dumpRaw()
{
	Serial.write(rawDumpPreamble, 3);

        uint8_t addrBuffer[2] = {0,0};    //starting address {msb, lsb}
        eepromWrite(addrBuffer, 2);        //write the address to the eeprom

	for (uint16_t j = 0; j < 4096; j++)	//read the entire chip (0x8000 bytes total / 8 bytes per read = 4096)
	{
		//Wire.requestFrom(EE_ADDR, 8);	//get 8 bytes from the eeprom
                
                uint8_t rxBuffer[8];
                eepromRead(rxBuffer, 8);
		for (uint8_t i = 0; i < 8; i++)	//get each individual byte
		{
			//print the data value
                        Serial.write(rxBuffer[i]);
		}
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
			if (cmd == 2 || cmd == 'e')	//if the command is the end log command, end the countdown and return
				return;
		}
	}
	ec_beginLog();
}

void ec_dumpFormatted()
{
        Serial.println("Raw or float? 'r'/'f'");
        while (!Serial.available());    //wait for there to be something sent
        
        uint8_t cmd = Serial.read();    //get the command
        if (cmd != (uint8_t)'r' || cmd != (uint8_t)'f')    //make sure the command is valid
        {
            Serial.println("Invalid command");
            return;
        }
        
        
        for (uint16_t i = 0; i < 4096; i++)    //read the entire chip (0x8000 bytes total / 8 bytes per read = 4096)
        {
            uint8_t temp[8];    //make a buffer for the read data
            eepromRead(temp, 8);    //read 8 bytes (1 log entry)
            uint16_t time = (temp[0]<<8) | (temp[1]);    //make the two byte time, MSB is in higher position; should probably change to little endian for sake of consistency
            int16_t xRaw = (int16_t)((temp[2]) | (temp[3]<<8));    //make the two byte x acceration
            int16_t yRaw = (int16_t)(temp[4]) | (temp[5]<<8);    //make the two byte x acceration
            int16_t zRaw = (int16_t)(temp[6]) | (temp[7]<<8);    //make the two byte x acceration
            
            if (cmd == 'f')    //if we need to print it out in float format
            {
                //scale the raw to a float value in meters/second/second
                float xAccel = xRaw * ACCEL_SCALE_FACTOR;
                float yAccel = yRaw * ACCEL_SCALE_FACTOR;
                float zAccel = zRaw * ACCEL_SCALE_FACTOR;
                
                //print out the data comma seperated
                Serial.print(time, DEC);
                Serial.print(",");
                Serial.print(xAccel, DEC);
                Serial.print(",");
                Serial.print(yAccel, DEC);
                Serial.print(",");
                Serial.print(zAccel, DEC);
                Serial.println();
            }
            else    //if we need to print it out in raw format
            {
                //print out the data comma seperated
                Serial.print(time, DEC);
                Serial.print(",");
                Serial.print(xRaw, DEC);
                Serial.print(",");
                Serial.print(yRaw, DEC);
                Serial.print(",");
                Serial.print(zRaw, DEC);
                Serial.println();
            }
        }
        Serial.println("End transmission");
}

void ec_displayHelp()
{
    Serial.println(DEVICE_INFO);
    Serial.println("Valid Commands");
    Serial.println("h = display help");
    Serial.println("i = get device info");
    Serial.println("b = begin log");
    Serial.println("c = continue log");
    Serial.println("e = end log");
    Serial.println("t = log with delay");
    Serial.println("d = dump eeprom data");
    Serial.println("w = dump eeprom data as raw bytes");
    Serial.println("f = dump eeprom with comma seperated data");
    Serial.println("r = reformat/erase eeprom data");
}

void proccessCommand()
{
    if (Serial.available())
    {
        uint8_t cmd = Serial.read();
        switch (cmd)
        {
            case (uint8_t)'b':
                ec_beginLog();
                break;
            case (uint8_t)'c':
                ec_continueLog();
                break;
            case (uint8_t)'e':
                ec_endLog();
                break;
            case (uint8_t)'i':
                ec_getDeviceInfo();
                break;
            case (uint8_t)'d':
                ec_dumpEEPROM();
                break;
            case (uint8_t)'w':
                ec_dumpRaw();
                break;
            case (uint8_t)'f':
                ec_dumpFormatted();
                break;
            case (uint8_t)'r':
                ec_reformatEEPROM();
                break;
            case (uint8_t)'t':
                ec_logWithDelay();
                break;
            case (uint8_t)'h':
                ec_displayHelp();
                break;
            default:
                if (cmd < NUM_COMMANDS)
                    (commands[cmd])();
                else
                    Serial.println("invalid command, send 'h' for help");
        }
    }
}



