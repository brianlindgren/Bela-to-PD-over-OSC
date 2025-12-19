// 14 - Integrated with input switching
// You can switch the input by changing the #define below
#define INSTRUMENT // INSTRUMENT or AUDIOFILE

#include <Bela.h>
#include <cmath>
#include <iostream>
#include <libraries/UdpClient/UdpClient.h>
#include <libraries/OscSender/OscSender.h>
#include <libraries/OscReceiver/OscReceiver.h>
#include <libraries/Pipe/Pipe.h>

// Conditional include and variables for the AUDIOFILE source
#if defined(AUDIOFILE)
#include <libraries/AudioFile/AudioFile.h>
unsigned int gReadPtr = 0; // Position of last read sample from file
std::string gFilename = "guitar.wav"; // Default audio file
std::vector<float> sampleBuffer;
#endif


// OSC and Pipe setup
Pipe oscPipe;
OscReceiver oscReceiver;
OscSender oscSender;
int localPort = 7562;
int remotePort = 5000;
const char* remoteIp = "192.168.7.1";

// Buffer and context variables
constexpr int kBufferSize = 1024; // Total size of the circular buffer
int kAnalogInChannels;
int gAnalogFrames;
int gAnalogSampleRate;

float input; // from Bela->context
uint16_t circularBuffer[kBufferSize]; // Circular buffer
UdpClient udpClient(4567, "192.168.7.1");
AuxiliaryTask serialCommsTask;

int readIndex = 0;  // Current read index
int writeIndex = 0; // Current write index

// This auxiliary task sends the buffered data over UDP
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

// Callback function for receiving OSC messages
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
    /// OSC Handshake (from Bela example)
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
    // Set the pipe to non-blocking for the render loop
    oscPipe.setBlockingRt(false);

    // Load the audio file if the AUDIOFILE macro is defined
    #if defined(AUDIOFILE)
    sampleBuffer = AudioFileUtilities::loadMono(gFilename);
    if(sampleBuffer.size() == 0) {
        rt_printf("Error loading audio file '%s'. Make sure it is in your project.\n", gFilename.c_str());
        return false;
    }
    rt_printf("Loaded audio file '%s' with %zu samples\n", gFilename.c_str(), sampleBuffer.size());
    #endif

    // Start the auxiliary task for sending data
    serialCommsTask = Bela_createAuxiliaryTask(ioLoop, 90, "serial-thread", NULL);
    Bela_scheduleAuxiliaryTask(serialCommsTask);

    // Store context variables and send them over OSC
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
    // --- OSC Message Handling ---
    oscpkt::Message* msg = nullptr;
    int ret = oscPipe.readRt(msg);

    if (ret > 0) {
        if (msg && msg->match("/osc-restart"))
        {
            // This block handles re-establishing the connection if requested via OSC
            oscSender.newMessage("/osc-setup").send();
            printf("Waiting for handshake ....\n");
            oscPipe.setBlockingRt(true);
            oscPipe.setTimeoutMsRt(1000);
            oscpkt::Message* replyMsg = nullptr; // Use a different pointer name
            int replyRet = oscPipe.readRt(replyMsg);
            bool ok = false;
            if (replyRet > 0) {
                if (replyMsg && replyMsg->match("/osc-setup-reply")) {
                    printf("handshake received!\n");
                    ok = true;
                }
                delete replyMsg;
            }
            if (!ok) {
                fprintf(stderr, "No handshake received: %d\n", replyRet);
            }
            oscPipe.setBlockingRt(false);

            // Restart the communication task and resend settings
            serialCommsTask = Bela_createAuxiliaryTask(ioLoop, 90, "serial-thread", NULL);
            Bela_scheduleAuxiliaryTask(serialCommsTask);
            gAnalogFrames = context->analogFrames;
            kAnalogInChannels = context->analogInChannels;
            gAnalogSampleRate = (int)context->analogSampleRate;
            oscSender.newMessage("/osc-settings").add(gAnalogSampleRate).add(gAnalogFrames).add(kAnalogInChannels).send();
        }
        delete msg;
    }

    // --- Data Buffering ---
    //if (readIndex != writeIndex) {
        //rt_printf("udp catchup! >> readIndex: %d, writeIndex: %d\n", readIndex, writeIndex);
    //}

#if defined(INSTRUMENT)
    // --- INSTRUMENT MODE ---
    // Read live data from the analog inputs. This loop runs for gAnalogFrames,
    // matching the block size expected by the UDP sending task.
    for (unsigned int n = 0; n < gAnalogFrames; n++) {
        for (unsigned int ar = 0; ar < kAnalogInChannels; ar++) {
            input = analogRead(context, n, ar);
            int circularBufferIndex = (writeIndex + ar) % kBufferSize;
            circularBuffer[circularBufferIndex] = input * 65535; //convert from float to int
        }
        writeIndex = (writeIndex + kAnalogInChannels) % kBufferSize; // Increment write index and wrap
    }
#elif defined(AUDIOFILE)
    // --- AUDIOFILE MODE ---
    // The render callback runs at the analog sample rate, but the audio file
    // is at the audio sample rate (2x analog rate). To make it play at the correct
    // speed, we must read two samples from the file for every one analog frame period.
    // We will read the first sample and skip the second to perform a simple
    // 2:1 downsampling, matching the data rate of the analog inputs.
    for (unsigned int n = 0; n < gAnalogFrames; n++) {
        // Read the first of the two samples for this time period
        if(++gReadPtr >= sampleBuffer.size())
            gReadPtr = 0;
        input = sampleBuffer[gReadPtr];

        // Read and discard the second sample to keep pace
        if(++gReadPtr >= sampleBuffer.size())
            gReadPtr = 0;

        // Write the single downsampled value to all channels for this frame
        for (unsigned int ar = 0; ar < kAnalogInChannels; ar++) {
            int circularBufferIndex = (writeIndex + ar) % kBufferSize;
            circularBuffer[circularBufferIndex] = input * 65535; //convert from float to int
        }
        writeIndex = (writeIndex + kAnalogInChannels) % kBufferSize; // Increment write index and wrap
    }
#endif
}

void cleanup(BelaContext *context, void *userData) {
    // Nothing to do here in this version
}
