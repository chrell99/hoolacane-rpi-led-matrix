A personalized version of the "rpi-rgb-led-matrix" library made by "Hzeller"
============================================================================

The purpose of this project is to utilize the existing library and slighty modify it,
to fit the needs for the project "Hoolacane LED sign". The original README.md can be
found in the original repository, while this repository's README will only focus on the
parts necessary for the project in question.

Getting started
---------------
This project will utilize a raspberry pi 3 B+ running raspberry pi OS lite for the controls,
a "RGB LED matrix panel drive board" from [Electrodragon](https://www.electrodragon.com/product/rgb-matrix-panel-drive-board-raspberry-pi/)
to interface with the LED panels, two [BTF-Lighting](https://www.amazon.de/-/en/dp/B085C2N571?ref=ppx_yo2ov_dt_b_product_details&th=1)
5V 40A power supplies, and 9 [LysonLED](https://www.aliexpress.com/item/32382200566.html) LED panels.

Firstly clone the git repository to the raspberry Pi, and afterwards run the following command in
the top folder:
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

Connecting multiple modules
---------------
When connecting multiple modules a few flags have to be set in order for it to work properly, aswell as
well as some physical configuration. On the Electrodragon board theres is a small switch on top of the
board. Ensure this is set in the "P3 chain" state to allow 3 parallel chains.
|Flag                    | Description |
|:-----------------------| :-------------------------|
|`--led-parallel=3` | Number of LED panels in parallel (How many individual lines are connected to the PI). |
|`--led-chain=3`  | Number of LED panels in series. |
|`--led-pixel-mapper="Rotate:90"`    | The rotation of the display in degrees. Has to be multiple of 90.|

The settings depend on the sign but the above is what will work for this project. 

This is a test to test the commits from the rasp pi
