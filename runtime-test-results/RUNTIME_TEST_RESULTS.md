## Runtime Test Results

:x: **The test workflows are failing. Please check the run logs.** :x:

### Validation Tests

#### Hardware

Test|ESP32|ESP32-C3|ESP32-C5|ESP32-C6|ESP32-H2|ESP32-P4|ESP32-S2|ESP32-S3
-|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:
ble|1/1 :white_check_mark:|1/1 :white_check_mark:|1/1 :white_check_mark:|1/1 :white_check_mark:|1/1 :white_check_mark:|-|-|Error :fire:
democfg|2/2 :white_check_mark:|-|1/1 :white_check_mark:|1/1 :white_check_mark:|-|-|1/1 :white_check_mark:|1/1 :white_check_mark:
fs|51/51 :white_check_mark:|51/51 :white_check_mark:|51/51 :white_check_mark:|51/51 :white_check_mark:|51/51 :white_check_mark:|Error :fire:|51/51 :white_check_mark:|51/51 :white_check_mark:
hello_world|1/1 :white_check_mark:|1/1 :white_check_mark:|1/1 :white_check_mark:|1/1 :white_check_mark:|1/1 :white_check_mark:|Error :fire:|1/1 :white_check_mark:|1/1 :white_check_mark:
keyboard_layout|-|-|-|-|-|Error :fire:|10/10 :white_check_mark:|10/10 :white_check_mark:
nvs|2/2 :white_check_mark:|2/2 :white_check_mark:|1/1 :white_check_mark:|4/4 :white_check_mark:|4/4 :white_check_mark:|Error :fire:|2/2 :white_check_mark:|3/3 :white_check_mark:
periman|1/1 :white_check_mark:|1/1 :white_check_mark:|1/1 :white_check_mark:|1/1 :white_check_mark:|1/1 :white_check_mark:|Error :fire:|1/1 :white_check_mark:|1/1 :white_check_mark:
psram|10/10 :white_check_mark:|-|10/10 :white_check_mark:|-|-|Error :fire:|10/10 :white_check_mark:|10/10 :white_check_mark:
timer|3/3 :white_check_mark:|4/4 :white_check_mark:|4/4 :white_check_mark:|4/4 :white_check_mark:|4/4 :white_check_mark:|Error :fire:|4/4 :white_check_mark:|4/4 :white_check_mark:
touch|3/3 :white_check_mark:|-|-|-|-|Error :fire:|3/3 :white_check_mark:|3/3 :white_check_mark:
uart|12/12 :white_check_mark:|11/11 :white_check_mark:|11/11 :white_check_mark:|11/11 :white_check_mark:|11/11 :white_check_mark:|Error :fire:|12/12 :white_check_mark:|11/11 :white_check_mark:
unity|2/2 :white_check_mark:|2/2 :white_check_mark:|2/2 :white_check_mark:|2/2 :white_check_mark:|2/2 :white_check_mark:|Error :fire:|2/2 :white_check_mark:|2/2 :white_check_mark:
wifi_ap|2/2 :white_check_mark:|1/1 :white_check_mark:|1/1 :white_check_mark:|1/1 :white_check_mark:|-|-|2/2 :white_check_mark:|Error :fire:

#### Wokwi

Test|ESP32|ESP32-C3|ESP32-C6|ESP32-H2|ESP32-P4|ESP32-S2|ESP32-S3
-|:-:|:-:|:-:|:-:|:-:|:-:|:-:
fs|Error :fire:|51/51 :white_check_mark:|51/51 :white_check_mark:|51/51 :white_check_mark:|51/51 :white_check_mark:|51/51 :white_check_mark:|51/51 :white_check_mark:
gpio|Error :fire:|1/1 :white_check_mark:|1/1 :white_check_mark:|1/1 :white_check_mark:|1/1 :white_check_mark:|1/1 :white_check_mark:|1/1 :white_check_mark:
hello_world|Error :fire:|1/1 :white_check_mark:|1/1 :white_check_mark:|1/1 :white_check_mark:|1/1 :white_check_mark:|1/1 :white_check_mark:|1/1 :white_check_mark:
i2c_master|Error :fire:|7/7 :white_check_mark:|7/7 :white_check_mark:|6/6 :white_check_mark:|6/6 :white_check_mark:|7/7 :white_check_mark:|7/7 :white_check_mark:
keyboard_layout|-|-|-|-|10/10 :white_check_mark:|10/10 :white_check_mark:|10/10 :white_check_mark:
nvs|Error :fire:|2/2 :white_check_mark:|4/4 :white_check_mark:|4/4 :white_check_mark:|4/4 :white_check_mark:|2/2 :white_check_mark:|3/3 :white_check_mark:
psram|Error :fire:|-|-|-|8/8 :white_check_mark:|10/10 :white_check_mark:|10/10 :white_check_mark:
sdcard|Error :fire:|11/11 :white_check_mark:|11/11 :white_check_mark:|11/11 :white_check_mark:|11/11 :white_check_mark:|11/11 :white_check_mark:|11/11 :white_check_mark:
timer|Error :fire:|4/4 :white_check_mark:|4/4 :white_check_mark:|4/4 :white_check_mark:|4/4 :white_check_mark:|4/4 :white_check_mark:|4/4 :white_check_mark:
uart|Error :fire:|11/11 :white_check_mark:|11/11 :white_check_mark:|11/11 :white_check_mark:|11/11 :white_check_mark:|12/12 :white_check_mark:|11/11 :white_check_mark:
unity|Error :fire:|2/2 :white_check_mark:|2/2 :white_check_mark:|2/2 :white_check_mark:|2/2 :white_check_mark:|2/2 :white_check_mark:|2/2 :white_check_mark:
wifi|Error :fire:|1/1 :white_check_mark:|1/1 :white_check_mark:|-|1/1 :white_check_mark:|2/2 :white_check_mark:|3/3 :white_check_mark:


Generated on: 2026/03/04 03:39:40

[Commit](https://github.com/lucasssvaz/arduino-esp32/commit/a30a87ca66db7b24f8f61bb0cf975724d5c270fb) / [Build and QEMU run](https://github.com/lucasssvaz/arduino-esp32/actions/runs/22652288636) / [Hardware and Wokwi run](https://github.com/lucasssvaz/arduino-esp32/actions/runs/22652525917) / [Results processing](https://github.com/lucasssvaz/arduino-esp32/actions/runs/22653947928)

[Test results](https://github.com/lucasssvaz/arduino-esp32/runs/65659389861)
