#include <Bela.h>
#include <libraries/Pipe/Pipe.h>
#include <libraries/Serial/Serial.h>
#include <cmath>
#include <iostream>
#include <string>

Serial gSerial;
Pipe gPipe;
float input;
float array[8];
int writevalue = 0;
float analogsum = 0;
float analogavg = 0;
AuxiliaryTask serialCommsTask;

void serialIo(void* arg) {
	while(!Bela_stopRequested())
	{
    for(unsigned int ars = 0; ars < 8; ars++) {
	gSerial.write((std::to_string(array[ars]).c_str())); //send the cv
	gSerial.write(","); //44
    gSerial.write(std::to_string(ars).c_str()); //send the adc #
    gSerial.write("*"); //42
   }
}
}

bool setup(BelaContext *context, void *userData) {
	gSerial.setup ("/dev/ttyGS0", 115200);
	AuxiliaryTask serialCommsTask = Bela_createAuxiliaryTask(serialIo, 0, "serial-thread", NULL);
	Bela_scheduleAuxiliaryTask(serialCommsTask);

	gPipe.setup("serialpipe", 1024);

	return true;
}

void render(BelaContext *context, void *userData)
{
//READ FRAME BY FRAME OF THE BUFFER
	for(unsigned int ar = 0; ar < 8; ar++) {
	analogsum = 0;
	for(unsigned int n = 0; n < context->audioFrames; n++) {
    input = (analogRead(context, n, ar));
    analogsum = input + analogsum;
    }
    analogavg = analogsum/16;
    array[ar] = analogavg;
    }
    //Bela_scheduleAuxiliaryTask(serialCommsTask);
}

void cleanup(BelaContext *context, void *userData) {}
