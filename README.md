# Bela-to-PD-over-Serial

This code sends Bela's 8 analog pins over serial USB to a computer running Pure Data's external, Comport. 
To save bandwidth, each pin's 16 sample buffer is averaged into one value, which is then transmitted.
