## Runtime Test Results

:x: **The test workflows are failing. Please check the run logs.** :x:

### Validation Tests

#### Hardware

Test|ESP32|ESP32-C3|ESP32-C5|ESP32-C6|ESP32-H2|ESP32-P4|ESP32-S2|ESP32-S3
-|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:
democfg|Error :fire:|-|Error :fire:|Error :fire:|-|-|Error :fire:|Error :fire:
fs|Error :fire:|Error :fire:|Error :fire:|Error :fire:|Error :fire:|Error :fire:|Error :fire:|Error :fire:
hello_world|Error :fire:|Error :fire:|Error :fire:|Error :fire:|Error :fire:|Error :fire:|Error :fire:|Error :fire:
nvs|Error :fire:|Error :fire:|Error :fire:|Error :fire:|Error :fire:|Error :fire:|Error :fire:|Error :fire:
periman|Error :fire:|Error :fire:|Error :fire:|Error :fire:|Error :fire:|Error :fire:|Error :fire:|Error :fire:
psram|Error :fire:|-|Error :fire:|-|-|Error :fire:|Error :fire:|Error :fire:
timer|Error :fire:|Error :fire:|Error :fire:|Error :fire:|Error :fire:|Error :fire:|Error :fire:|Error :fire:
touch|Error :fire:|-|-|-|-|Error :fire:|Error :fire:|Error :fire:
uart|Error :fire:|Error :fire:|Error :fire:|Error :fire:|Error :fire:|Error :fire:|Error :fire:|Error :fire:
unity|Error :fire:|Error :fire:|Error :fire:|Error :fire:|Error :fire:|Error :fire:|Error :fire:|Error :fire:

#### Wokwi

Test|ESP32|ESP32-C3|ESP32-C6|ESP32-H2|ESP32-P4|ESP32-S2|ESP32-S3
-|:-:|:-:|:-:|:-:|:-:|:-:|:-:
fs|51/51 :white_check_mark:|51/51 :white_check_mark:|51/51 :white_check_mark:|51/51 :white_check_mark:|51/51 :white_check_mark:|51/51 :white_check_mark:|51/51 :white_check_mark:
gpio|1/1 :white_check_mark:|1/1 :white_check_mark:|1/1 :white_check_mark:|1/1 :white_check_mark:|1/1 :white_check_mark:|1/1 :white_check_mark:|1/1 :white_check_mark:
hello_world|1/1 :white_check_mark:|1/1 :white_check_mark:|1/1 :white_check_mark:|1/1 :white_check_mark:|1/1 :white_check_mark:|1/1 :white_check_mark:|1/1 :white_check_mark:
i2c_master|7/7 :white_check_mark:|7/7 :white_check_mark:|7/7 :white_check_mark:|6/6 :white_check_mark:|6/6 :white_check_mark:|7/7 :white_check_mark:|7/7 :white_check_mark:
nvs|0/2 :x:|2/2 :white_check_mark:|4/4 :white_check_mark:|4/4 :white_check_mark:|4/4 :white_check_mark:|2/2 :white_check_mark:|3/3 :white_check_mark:
psram|10/10 :white_check_mark:|-|-|-|8/8 :white_check_mark:|10/10 :white_check_mark:|10/10 :white_check_mark:
sdcard|11/11 :white_check_mark:|11/11 :white_check_mark:|11/11 :white_check_mark:|11/11 :white_check_mark:|11/11 :white_check_mark:|11/11 :white_check_mark:|11/11 :white_check_mark:
timer|3/3 :white_check_mark:|4/4 :white_check_mark:|4/4 :white_check_mark:|4/4 :white_check_mark:|4/4 :white_check_mark:|4/4 :white_check_mark:|4/4 :white_check_mark:
uart|28/31 :x:|10/10 :white_check_mark:|10/10 :white_check_mark:|10/10 :white_check_mark:|10/10 :white_check_mark:|59/59 :white_check_mark:|10/10 :white_check_mark:
unity|2/2 :white_check_mark:|2/2 :white_check_mark:|2/2 :white_check_mark:|2/2 :white_check_mark:|2/2 :white_check_mark:|2/2 :white_check_mark:|2/2 :white_check_mark:
wifi|2/2 :white_check_mark:|1/1 :white_check_mark:|1/1 :white_check_mark:|-|-|2/2 :white_check_mark:|3/3 :white_check_mark:


Generated on: 2026/01/12 02:13:11

[Commit](https://github.com/lucasssvaz/arduino-esp32/commit/3d067db01b04636cc16103784ada61d1e1bc40b4) / [Build and QEMU run](https://github.com/lucasssvaz/arduino-esp32/actions/runs/20905729385) / [Hardware and Wokwi run](https://github.com/lucasssvaz/arduino-esp32/actions/runs/20905833365) / [Results processing](https://github.com/lucasssvaz/arduino-esp32/actions/runs/20906003018)

[Test results](https://github.com/lucasssvaz/arduino-esp32/runs/60059592235)
