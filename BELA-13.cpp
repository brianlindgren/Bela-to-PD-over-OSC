//Bela 13.1
//much thanks to Giulio Moro at Bela for helping me to get this working :)

#include <Bela.h>
#include <cmath>
#include <iostream>
#include <libraries/UdpClient/UdpClient.h>
#include <libraries/OscSender/OscSender.h>
#include <libraries/OscReceiver/OscReceiver.h>
#include <libraries/Pipe/Pipe.h>

Pipe oscPipe;
OscReceiver oscReceiver;
OscSender oscSender;
int localPort = 7562;
int remotePort = 5000;
const char* remoteIp = "192.168.7.1";

constexpr int kBufferSize = 1024; // Total size of the circular buffer
int kAnalogInChannels;
int gAnalogFrames;
int gAnalogSampleRate;

float input; //from Bela->context
uint16_t circularBuffer[kBufferSize]; // Circular buffer
UdpClient udpClient(4567, "192.168.7.1");
AuxiliaryTask serialCommsTask;

int readIndex = 0;  // Current read index
int writeIndex = 0; // Current write index

void ioLoop(void* arg) {
    while (!Bela_stopRequested()) {
        while (readIndex != writeIndex) { //send 1 block at a time until caught up
            uint16_t buffer[kAnalogInChannels * gAnalogFrames]; // init transfer buffer
            
            //copy analog frames to transfer buffer
            for (int frame = 0; frame < gAnalogFrames; ++frame) {
                for (int channel = 0; channel < kAnalogInChannels; ++channel) {
                    int bufferIndex = (frame * kAnalogInChannels) + channel;
                    int circularBufferIndex = (readIndex + bufferIndex) % kBufferSize;
                    buffer[bufferIndex] = circularBuffer[circularBufferIndex];
                }
            }
    
            readIndex = (readIndex + (gAnalogFrames * kAnalogInChannels)) % kBufferSize; // Move the read index to the next block and wrap
            
            udpClient.send(buffer, sizeof(buffer)); //send to PD
        }
        usleep(250);
    }
}

void on_receive(oscpkt::Message* msg, const char*, void*)
{
    // we make a copy of the incoming message and we send it down the pipe to the real-time thread
    oscpkt::Message* incomingMsg = new oscpkt::Message(*msg);
    oscPipe.writeNonRt(incomingMsg);

    // the real-time thread sends back to us the pointer once it is done with it
    oscpkt::Message* returnedMsg;
    while (oscPipe.readNonRt(returnedMsg) > 0) {
        delete returnedMsg;
    }
}

bool setup(BelaContext *context, void *userData) {
    /// from Bela example
    oscPipe.setup("incomingOsc");
    oscReceiver.setup(localPort, on_receive);
    oscSender.setup(remotePort, remoteIp);

    // the following code sends an OSC message to address /osc-setup
    oscSender.newMessage("/osc-setup").send();

    printf("Waiting for handshake ....\n");
    // we want to stop our program and wait for a new message to come in.
    // therefore, we set the pipe to blocking mode.
    oscPipe.setBlockingNonRt(false);
    oscPipe.setBlockingRt(true);
    oscPipe.setTimeoutMsRt(1000);
    oscpkt::Message* msg = nullptr;
    int ret = oscPipe.readRt(msg);
    bool ok = false;
    if (ret > 0) {
        if (msg && msg->match("/osc-setup-reply")) {
            printf("handshake received!\n");
            ok = true;
        }
        delete msg;
    }
    if (!ok) {
        fprintf(stderr, "No handshake received: %d\n", ret);
        return false;
    }
    // in the remainder of the program, we will be calling readRt() from render(), and we want it
    // to return immediately if there are no new messages available. We therefore set the
    // pipe to non-blocking mode
    oscPipe.setBlockingRt(false);
    
    serialCommsTask = Bela_createAuxiliaryTask(ioLoop, 90, "serial-thread", NULL);
    Bela_scheduleAuxiliaryTask(serialCommsTask);
    printf("AnalogFrames: %d\n", context->analogFrames);
    printf("AnalogInChannels: %d\n", context->analogInChannels);
    printf("AnalogSampleRate: %d\n", (int)context->analogSampleRate);
    gAnalogFrames = context->analogFrames; //# of analog frames
    kAnalogInChannels = context->analogInChannels;
    gAnalogSampleRate = (int)context->analogSampleRate;
    oscSender.newMessage("/osc-settings").add(gAnalogSampleRate).add(gAnalogFrames).add(kAnalogInChannels).send();
    return true;
}

void render(BelaContext *context, void *userData)
{
    oscpkt::Message* msg = nullptr;
    int ret = oscPipe.readRt(msg);

    if (ret > 0) {
        if (msg && msg->match("/osc-restart"))
        {
            oscSender.newMessage("/osc-setup").send();
        
            printf("Waiting for handshake ....\n");
            oscPipe.setBlockingNonRt(false);
            oscPipe.setBlockingRt(true);
            oscPipe.setTimeoutMsRt(1000);
            oscpkt::Message* msg = nullptr;
            int ret = oscPipe.readRt(msg);
            bool ok = false;
            if (ret > 0) {
                if (msg && msg->match("/osc-setup-reply")) {
                    printf("handshake received!\n");
                    ok = true;
                }
                delete msg;
            }
            if (!ok) {
                fprintf(stderr, "No handshake received: %d\n", ret);
            }
            oscPipe.setBlockingRt(false);
            
            serialCommsTask = Bela_createAuxiliaryTask(ioLoop, 90, "serial-thread", NULL);
            Bela_scheduleAuxiliaryTask(serialCommsTask);
            printf("AnalogFrames: %d\n", context->analogFrames);
            printf("AnalogInChannels: %d\n", context->analogInChannels);
            printf("AnalogSampleRate: %d\n", (int)context->analogSampleRate);
            gAnalogFrames = context->analogFrames; //# of analog frames
            kAnalogInChannels = context->analogInChannels;
            gAnalogSampleRate = (int)context->analogSampleRate;
            oscSender.newMessage("/osc-settings").add(gAnalogSampleRate).add(gAnalogFrames).add(kAnalogInChannels).send();
        }
        delete msg;
    }

    if (readIndex != writeIndex) {
        rt_printf("udp catchup! >> readIndex: %d, writeIndex: %d\n", readIndex, writeIndex);
    }

    for (unsigned int n = 0; n < gAnalogFrames; n++) {
        for (unsigned int ar = 0; ar < kAnalogInChannels; ar++) {
            input = analogRead(context, n, ar); //read from ADC/context
            int circularBufferIndex = (writeIndex + ar) % kBufferSize;
            circularBuffer[circularBufferIndex] = input * 65535; //convert from float to int
        }
        writeIndex = (writeIndex + kAnalogInChannels) % kBufferSize; // Increment write index and wrap
    }
}

void cleanup(BelaContext *context, void *userData) {}
