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

//turn the I2C frequency up to 400kHz (fast mode)
//#define TWI_FREQ  400000L

extern "C"
{
    #include <MyTWI.h>
}
#include <stdint.h>
#include <stdio.h>


//general definitions
#define EE_ADDR			0x50
#define LED				13
#define NUM_COMMANDS	10
#define DEVICE_INFO		"RocketSenseV1"
#define COUNTDOWN_SEC	120

//command array
typedef void (*ExternalCommand)(void);
ExternalCommand commands[NUM_COMMANDS] = {&ec_beginLog, &ec_continueLog, &ec_endLog, &ec_dumpEEPROM, &ec_reformatEEPROM,
					  &ec_dumpRaw, &ec_dumpFormatted, &ec_getDeviceInfo, &ec_logWithDelay, &ec_displayHelp};

boolean isLogging = false;	//current device state
uint16_t  currentMemoryAddress = 0;	//current write address of the eeprom
byte rawDumpPreamble[3] = {0x01, 0x02, 0x03};
byte rawDumpEOT[3] = {0x03, 0x02, 0x01};

uint8_t eepromWriteBuffer[66];
uint8_t currentBufferPos = 0;
uint8_t lastWriteTime = 0;

void setup()
{
	Serial.begin(115200);	//init Serial
	pinMode(LED, OUTPUT);	//init Status LED
        
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
            eepromWriteBuffer[currentBufferPos++] = 0xF0;
            eepromWriteBuffer[currentBufferPos++] = 0xF0;
            eepromWriteBuffer[currentBufferPos++] = 0xF0;
            eepromWriteBuffer[currentBufferPos++] = 0xF0;
            eepromWriteBuffer[currentBufferPos++] = 0xF0;
            eepromWriteBuffer[currentBufferPos++] = 0xF0;
            
            if ((currentBufferPos-2) % 64 == 0)    //when it is time to write to the eeprom
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
            delay(1);
            Serial.print("Logged at ");
            Serial.println(millis(), HEX);
        }
}

void eepromWrite(uint8_t* data, uint8_t quantity)    //TODO: check to make sure pointer is implemented correctly
{
    twi_writeTo(EE_ADDR, data, quantity, 1, 1);
}

void eepromRead(uint8_t* rxBuffer, uint8_t quantity)    //TODO: check to make sure pointer is implemented correctly
{
    twi_readFrom(EE_ADDR, rxBuffer, quantity, 1);
}

void ec_beginLog()
{
	currentMemoryAddress = 0;	//reset memory address
        currentBufferPos = 0;           //reset the buffer position
        lastWriteTime = (uint8_t)millis();    //reset the last write time to the current time
	isLogging = true;			//start logging
}

void ec_continueLog()
{
	isLogging = true;	//start logging
        lastWriteTime = (uint8_t)millis();    //reset the last write time to the current time
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
    /*for (uint16_t j = 0; j < 4096; j++)	//read the entire chip (0x8000 bytes total / 8 bytes per read = 4096)
	{
		Wire.requestFrom(EE_ADDR, 8);	//get 8 bytes from the eeprom
                if (!Wire.available()) break;    //in case no data was recieved
                uint16_t time = (((uint8_t)Wire.read())<<8) | ((uint8_t)Wire.read());
                Serial.print(time, DEC);
                Serial.print(",");
                Serial.print((uint8_t)(Wire.read()), DEC);
                Serial.print(",");
                Serial.print((uint8_t)(Wire.read()), DEC);
                Serial.print(",");
                Serial.print((uint8_t)(Wire.read()), DEC);
                Serial.print(",");
                Serial.print((uint8_t)(Wire.read()), DEC);
                Serial.print(",");
                Serial.print((uint8_t)(Wire.read()), DEC);
                Serial.print(",");
                Serial.print((uint8_t)(Wire.read()), DEC);
                Serial.print(",");
                Serial.print((uint8_t)(Wire.read()), DEC);
                Serial.println();
	}*/
        Serial.println("Laziness prevented this from being implemented... check again later");
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



