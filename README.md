<h1>RocketSenseV1</h1>
<h2>Data collection platform for a model rocket</h2>

This project contains hardware and software files to build a simple data collection system for a model rocket using an accelerometer. It stores all of the data on an EEPROM chip.

To clone this repo: `git clone https://github.com/dmbonsall/RocketSenseV1.git`

This hardware portion of this project includes an eagle project that contains schematics for the system. It also contains a spreadsheet that has some details of how I did some of the board layout on perfboard to get it to fit inside the body of a model rocket.

The software portion of this project has two parts: the arduino code and a windows GUI to assist with reading the data off of the arduino. The arduino code is writen in C and the Windows utility is written in C# and XAML. Eventually I will get around to making a general command line interface since I have since moved away from windows development and so that it can be used by a wider audience.

Building the Arduino code is as simple as loading it into the arduino IDE and hitting build. The Windows utility is contained as a VS project, although all of the config files are for VS 2010 so you may be better off importing the code files into a clean project in your version of VS and going from there if you are running something newer.
