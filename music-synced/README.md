# Music Synced

This folder is dedicated to all the programs that utilize the Soundcard to have the led sign blink, or otherwise seem in phase/synced to the music.
The code here is by no means the best, but it works on the LED sign and is relatively easy to get an overview of.
And yeah i know the simple strobe function is not music sync, but i just placed it here since this a custom folder

Most important thing is to remember the hardware/PCM-Device number. This can be found using the command: `aplay -l`. This will display something like:
```
**** List of PLAYBACK Hardware Devices ****
card 0: Device [USB PnP Sound Device], device 0: USB Audio [USB Audio]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
card 1: vc4hdmi [vc4-hdmi], device 0: MAI PCM i2s-hifi-0 [MAI PCM i2s-hifi-0]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
```

Here you then need to select the USB soundcard, which from the above example translates to `hw:0,0` as the PCMDEVICE attribute in the program.

