# A personalized version of the "rpi-rgb-led-matrix" library made by "Hzeller"

The purpose of this project is to utilize the existing library and slighty modify it, to fit the needs for the project "Hoolacane LED sign". The original README.md can be found in the original repository, while this repository's README will only focus on the parts necessary for the project in question.

Some, seemingly unrelated code/commands will also be included here, since it is for a specific usecase, but it can be applied to other uses.

## Getting started
This project will utilize a raspberry pi 3 B+ running raspberry pi OS lite for the controls,
a "RGB LED matrix panel drive board" from [Electrodragon](https://www.electrodragon.com/product/rgb-matrix-panel-drive-board-raspberry-pi/)
to interface with the LED panels, two [BTF-Lighting](https://www.amazon.de/-/en/dp/B085C2N571?ref=ppx_yo2ov_dt_b_product_details&th=1)
5V 40A power supplies, and 9 [LysonLED](https://www.aliexpress.com/item/32382200566.html) LED panels.


<br/><br/>
### Network
It is highly recommended to set up a network connection on the raspberry pi, such that you can connect using SSH. On newer versions of raspberry pi lite os, this can be done using the network manager module:
```
sudo nmtui
```
This command pulls up a UI making it much easier to connect multiple different networks.

Once a network connection has been establish, we can connect the raspberry pi to the github repository. Generate a SSH key pair with the below command:
```
ssh-keygen -t rsa -b 4096 -C "YourEmail@gmail.com"
```
This generates a private and public key. Copy the public key (including the rsa part), and add it as a "deploy key" in the [repo](https://github.com/chrell99/hoolacane-rpi-led-matrix/settings/keys).

Afterwards test the connectio: `ssh -T git@github.com`. If the key returned matches any of the [official github public keys](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/githubs-ssh-key-fingerprints), you should be good to go. You might need to add you email and signature for commiting from the rapsberry pi.

**Remember to clone the repo with SSH**
```
git clone git@github.com:chrell99/hoolacane-rpi-led-matrix.git
```

<br/><br/>
### First compile and run

Once you've cloned the git repository to the raspberry Pi, you can run the following command in the top folder:
```
sudo make -C examples-api-use
```
Once done you can run one of the examples, by changing to the `examples-api-use` folder, but some 
configuration is necessary. The modules from LysonLED need the following parameters set to function 
properly:

|Flag                    | Description |
|:-----------------------| :-------------------------|
|`--led-slowdown-gpio=2` | Slowdown GPIO. Needed for faster raspberry Pis and/or slower panels.|
|`--led-multiplexing=1`  | In particular bright outdoor panels with small multiplex ratios require this. Often an indicator: if there are fewer address lines than expected: ABC (instead of ABCD) for 32 high panels and ABCD (instead of ABCDE) for 64 high panels.|
|`--led-show-refresh`    | Not necessary for the function of the LED panel, but a nice piece of information to have on the raspberry PI|

Flags can be set while calling one of the examples, e.g the following snippet:
```
sudo ./demo -D0 --led-slowdown-gpio=2 --led-multiplexing=1 --led-show-refresh 
```




<br/><br/>
## Connecting multiple modules

When connecting multiple modules a few flags have to be set in order for it to work properly, aswell as
well as some physical configuration. On the Electrodragon board theres is a small switch on top of the
board. Ensure this is set in the "P3 chain" state to allow 3 parallel chains.
|Flag                    | Description |
|:-----------------------| :-------------------------|
|`--led-parallel=3` | Number of LED panels in parallel (How many individual lines are connected to the PI). |
|`--led-chain=3`  | Number of LED panels in series. |
|`--led-pixel-mapper="Rotate:90"`    | The rotation of the display in degrees. Has to be multiple of 90.|

The settings depend on the sign but the above is what will work for this project. 

Combining this, with the demo from ealier you can run a command like:

```
sudo ./demo -D0 --led-chain=3 --led-parallel=3 --led-slowdown-gpio=2 --led-multiplexing=1
```
