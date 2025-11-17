# Jack <> Bluetooth Design Specification/Details

## Purpose

The purpose of the *Jack <> Bluetooth Design Specification* is to document the design choices and provide details on this project.

## Table of Content

1. [Jack2Bluetooth features](#1-jack2bluetooth-features)
2. [Architecture](#2-architecture)
    1. [Jack to bluetooth](#21-jack-to-bluetooth)
    2. [Bluetooth to jack](#22-bluetooth-to-jack)
    3. [Overall Architecture](#23-overall-architecture)
    4. [User interface](#24-user-interface)
3. [Component choices](#3-component-choices)
    1. [MCU](#31-mcu--microcontroller-unit)
    2. [Audio codec](#32-audio-codec)
    3. [Battery](#33-battery)
        1. [ESP32-C6 curruent consumption](#331-esp32-c6-current-consumption)
        2. [TLV320AIC3101 current consumption](#332-tlv320aic3101-current-consumption)
        3. [Current consumption conclusions](#333-current-consumption-conclusions)
    4. [PMIC](#34-pmic--power-management-integrated-circuit)
    5. [LDO](#35-ldo--low-dropout-voltage-regulator)
    6. [Switch Regulator](#36-switching-voltage-regulator-switch)
4. [Electronic schematic](#4-electronic-schematic)
    1. [Jack 3.5mm](#41-jack-35mm)
    2. [Jack 2.5mm](#42-jack-25mm)
    3. [Audio Codec](#43-audio-codec-tlv320aic3101)
    4. [MCU](#44-mcu-esp32-c6-wroom-1)
5. [GPIOs pin assignments](#5-gpios-pin-assignments)
6. [I2C Addresses](#6-i2c-addresses)
7. [Layout](#7-layout)
    1. [Main ideas](#71-layout-main-ideas)
    2. [Impedance matching](#72-impedance-matching)
    3. [Trace length](#73-trace-length)
    4. [Layers](#74-layers)
        1. [Layer 1](#741-layer-1)
        2. [Layer 2](#742-layer-2)
        3. [Layer 3](#743-layer-3)
        4. [Layer 4](#744-layer-4)

## 1. Jack2Bluetooth features

This project aims to design a device with the following features:

- Audio input from a 2.5mm audio jack can be sampled and transmitted via Bluetooth to a headphone.
- Digital audio from a smartphone can be received via Bluetooth and converted into analog audio through a 3.5mm audio jack.
- The device can be powered via USB-C or run on battery for up to 3 hours.
- The battery charges when the device is connected to USB-C.
- A screen and buttons allow switching between "Jack to Bluetooth" and "Bluetooth to Jack" modes.
- The screen and buttons enable Bluetooth peripheral selection when the device functions as a Bluetooth central.
- The screen turns off after 1 minute of inactivity to save power.

## 2. Architecture

### 2.1 Jack to Bluetooth

For audio sampling:
![jack_to_bluetooth_architecture.png](images/jack_to_bluetooth_architecture.png)
The audio codec must support line-level audio input signals. The ADC is integrated into the audio codec with an amplifier.

### 2.2 Bluetooth to Jack

![bluetooth_to_jack_architecture.png](images/bluetooth_to_jack_architecture.png)
The audio codec must include a DAC with an amplifier.

### 2.3 Overall architecture

When merging the two previous architecture, it becomes:
![jack2bluetooth_reduced_architecture.png](images/jack2bluetooth_reduced_architecture.png)

And when adding power components and the user interface components, the overall architecture is :

![jack2bluetooth_architecture.png](images/jack2bluetooth_architecture.png)

### 2.4 User Interface

Based on the architecture, the user interface is then:

|User interface    | Direction      |
| ---------------- | -------------- |
| Button Next      | Input          |
| Button Enter     | Input          |
| Button Back      | Input          |
| Button On/Off    | Input          |
| Audio Jack 2.5mm | Input          |
| Usb-C            | Input          |
| Bluetooth        | Input & Output |
| Screen           | Output         |
| Audio Jack 3.5mm | Output         |

## 3. Component choices

### 3.1 MCU : Microcontroller Unit

The ESP32 was chosen due to its widespread availability and ease of use. To simplify RF design, an ESP32 module with an integrated antenna was selected.

Requirements:

- Bluetooth Classic support with protocol A2DP
- Integrated antenna
- USB, I2S, I2C, SPI inerfaces
- Core frequency (not the max supported but the integrated clock frequency)
- Memory size

Revision 3.1:
First choice made in Rev3.0 was ESP32-C6-WROOM-N8.
This was a bad choice because ESP32-C6 only supports Bluetooth Classic Low Energy and not Bluetooth classic.

New choice should be default ESP32. It should be a module to have clock and antenna integrated.

List of modules considered:

- ESP32-WROVER-E
- ESP32-WROOM-32E

Both have the same soc. However, ESP32-WROVER-E has an additional memory PSRAM memory because its module is bigger than ESP32-WROOM-32E.

The version with the bigger flash (16MB) and PSRAM (8MB) is selected.

Decision is made to use **ESP32-WROVER-E-N16R8**.

Usefull link: [ESP HW Design guidelines](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32/index.html).

### 3.2 Audio Codec

Requirements:

- DAC 24kHz, 48kHz, 96kHz - Input is I2S - Output is single-ended headphone amplifier (16-32 Ohms load).
- ADC 24kHz, 48kHz, 96kHz - Input is single-ended, line-level audio - Output is I2S.

Final choice : **TLV320AIC3101** (Texas Instrument).

Revision 3.1: efforts where made to consider other devices to avoid QFN packages. However, all the other packages seen were classified as outdated by the manufacturer or were missing important features. So, decision was made to stay with the TLV320AIC3101.

### 3.3 Battery

The battery should provide at least 3 hours of operation.
The most power-intensive components are the MCU, audio codec, and the screen.

The screen is going to be on only when a user push a button (and stays on for ~30s). Therefore, it should be off in idle mode, in transmitting mode or receiving mode. Since the screen is mostly off, its consumption is negligible.

Revision 3.1: The battery connector has been updated to avoid soldering.

#### 3.3.1 ESP32-C6 Current consumption

According to the esp32-c6 datasheet, the Bluetooth LE current consumption are:
![ESP32-C6_current_consumption.png](images/ESP32-C6_current_consumption.png)

#### 3.3.2 TLV320AIC3101 Current consumption

According to its datasheet, it current consumptions are :
![TLV320AIC3101_current_consumption.png](images/TLV320AIC3101_current_consumption.png)

So it is negligeable compared to the ESP32-C6 Bluetooth LE transmission currents.

#### 3.3.3 Current consumption conclusions

The critical use case for current consumption is during audio sampling and Bluetooth transmission. Assuming the transmission is done at 9dBm, the required battery to power the device 3 hours is around 600mA.

#### 3.3.4 Revision 3.1

- Battery Capacity: 1200mAh（Real 1200mAh）
- Battery Voltage:3.7V  
- Battery Model: 103040
- Battery Size: 10x30x40mm
- Charging voltage:4.2V
- Cut-off voltage:2.75V
- Charging and discharging current:4A-8A
- Cycle life:1000 times
- Safety protection:charging,over-discharging,over-current,Protection
- Connector: PH2.0 plug

### 3.4 PMIC : Power Management Integrated Circuit

The PMIC should support power path between the USB-C and the battery, battery charging when USB-C is plugged, battery discharging when unplugged.
Since a power on/off system is needed, if this IC could have sleep or deep sleep mode with very low current consumption, it could be nice.

Final choice : **BQ25619RTWR** (Texas Instrument).

All the elements are providing in the first page of product specification:
![BQ25619RTWR_spec.png](images/BQ25619RTWR_spec.png)

### 3.5 LDO : Low-dropout voltage regulator

It was decided to use an LDO for the analog to have a clean alimentation. LDO should not be fixed to be able to reuse it in other designs.

Choice was made to use **TLV758P** from Texas Instrument.

### 3.6 Switching Voltage Regulator switch

Choice was made to use the **TPS62A01PDDCR** from Texas Instrument.

## 4. Electronic schematic

### 4.1 Jack 3.5mm

![jack_connector.png](images/jack_connector.png)

And the ASJ datasheet gives:

![asj-200-b-ht_datasheet](images/asj-200-b-ht_datasheet.png)

1. Sleeve is GND
2. Tip is Left
3. Ring is Right

So the schematic on kicad is:

![jack35_schematic.png](images/jack35_schematic.png)

### 4.2 Jack 2.5mm

The schematic is
![jack25_schematic.png](images/jack25_schematic.png)

### 4.3 Audio Codec (TLV320AIC3101)

The typical application from the spec is:

![TLV320AIC3101_typical_app.png](images/TLV320AIC3101_typical_app.png)

It is also worth looking at the development board:

![TLV320AIC3101_dev_board.png](images/TLV320AIC3101_dev_board.png)

The schematic done is then:

![TLV320AIC3101_schematic.png](images/TLV320AIC3101_schematic.png)

4 capacitors are to be determined, C8, C9, C20, C21.

First, C8=C9 and C20=C21.

Then, C8 and Rinput_of_line2R is a high pass filter, where fc = 1/(2*pi*C*R).

![TLV320AIC3101_input_resistance.png](images/TLV320AIC3101_input_resistance.png)

From TLV320AIC3101 datasheet, Rinput_of_line2R
is between 20kΩ and 80kΩ depending on the attenuation programmed in the audio codec.

The audio bandwidth is 20-20kHz. A capacitor of 1uF gives fc = 8Hz for R=20kΩ and fc= 2Hz for R=80kΩ which should be good for this application. So C8=C9=1uF.

Under the hyptohesis that the load resistance on the jack 3.5mm is 10kΩ (same hypothesis as in the TLV320AIC3101 datasheet), C20 and Rload are doing an high pass filter. A 10uF capacitor ensures that for Rload > 800Ω, the audio bandwidth is respected. So C20=C21=10uF.

### 4.4 MCU (ESP32-C6-WROOM-1)

The datasheet of the ESP32-C6-WROOM-1 recommend the following schematic:

![ESP32-C6_typical_app.png](images/ESP32-C6_typical_app.png)

A good resource is also the [ESP32-C6-DevKitC-1 v1.4](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c6/esp32-c6-devkitc-1/index.html)

![ESP32-C6_devkitc.png](images/ESP32-C6_devkitc.png)

Based on this two schematics, the schematic done is:

![ESP32-C6_schematic.png](images/ESP32-C6_schematic.png)

To boot the option are based on table 7 from the ESP32-C6-WROOM-1 datasheet:
![ESP32-C6_boot.png](images/ESP32-C6_boot.png)

### 4.5 PMIC (BQ25619RTWR)

The typical application from the BQ25619RTWR datasheet is:

![BQ25619RTWR_typical_app.png](images/BQ25619RTWR_typical_app.png)

The [BQ25619 evaluation module (EVM)](https://www.ti.com/tool/BQ25619EVM) is also a good reference:

![BQ25619_evm](images/BQ25619_evm.png)

Based on that, the schematic done is :
![BQ25619_schematic](images/BQ25619_schematic.png)

In this case, since the device is powered by USB-C, PSEL should be connected to HIGH with REGN value. This decision is based on the BQ25619_evm schematic.

PMID_GOOD is connected to REGN through a 10k resistor as described in the pin configuration section of the datasheet.

PMID is connected to a 10uF capacitor following the datasheet typical application.

QON does not need to be pulled high and can stay floating when button not pushed.

Two resistors R28 and R29 need to be determined following section `7.3.6.4.1 JEITA Guideline Compliance During Charging Mode` of the datasheet. Since the default battery used for this project does not have temperature sensor, these resistors are equals to 10k as described in the pin configuration section of the datasheet.

### 4.6 LDO (TLV758P)

The typical application is:
![TLV758P_typical_app](images/TLV758P_typical_app.png)

The target analog voltage is 3.3V.

R1/R2 = Vout/Vfb - 1 = 5

R1 + R2 <= Vout/(1e-6) = 3.3MΩ

Decision was to have R1 = 750kΩ and R2 = 150kΩ.
Therefore, the schematic is:
![TLV758P_schematic](images/TLV758P_schematic.png)

### 4.7 Switch Regulators (TPS62A01PDDCR)

The typical application is :
![TPS62A01PDDCR_typical_application](images/TPS62A01PDDCR_typical_application.png)

Two regulators are required to create 1.8V and 3.3V. A voltage divider is built with R1 and R2 to fix the output voltage.

The first one can follow the same design as the typical application since it was already designed for Vout = 1.8V.

The second one needs to be designed for Vout = 3.3V. The section `8.2.2.1 Setting the Output Voltage` of the datasheet gives R2 <= 100kΩ and R1 = R2(Vout/0.6 - 1) = 4.5*R2

Choice is made to take R1 = 100kΩ, and R2 made with two resistors R2 = 300k + 150k

![TPS62A01PDDCR_schematic.png](images/TPS62A01PDDCR_schematic.png)

### 4.8 Screen

The screen is going to be connected through an SPI interface. It will be powered thanks to the 3.3 voltage. However to be able to power off the screen, an electronic circuit is required.

![Screen_schematic](images/Screen_schematic.png)

Revision 3.1: Decision is made to keep an external SPI interface as most screen provide this setup. It should be make debug easier.

### 4.9 USB-C & UART Connectors

The schematic done is based on the devkit of the ESP32.

![usbc_connector_schematic.png](images/usbc_connector_schematic.png)

Revision 3.1: Decision is made to remove ESD protection for uart considering the PCB will be manipulated with the necessary ESD protections.
A second UART is added for monitoring the ESP logs. The first one will therefore only be used to boot.

### 4.10 Control Buttons

![control_buttons_schematic](images/control_buttons_schematic.png)

Revision 3.1: An IOs expander is added to avoid taking all the pins of the MCU.
The buttons and leds are therefore connected to this expander.

### 4.11 IOs Expander

IOs expander should:

- provide i2c interface for mcu.
- control at least 8 GPIOs
- support irq
- be available in an easy soldering package

Chip MCP23008 is selected because is do the job and it is available in DIP and SSOP packages!

Pins A0, A1, A2 are used to set the package address for the I2C bus.
So the address is: 0x2A_value.
Here A0 are all set to GND. So final address is 0x20

Final diagram is :
![IOs expander](images/ios_expander_schematic.png)

## 5. GPIOs Pin Assignments

### 5.1 MCU Pin Assignments

This table contains all the pin assignments. It was done updated during layout to make rooting easier.

|IO°             |Type | Active Mode    | Debug/Bringup|
|----------------|-----|----------------|--------------|
| IO0            | I/O |                     | booting.      |
| IO1 TXD0       | I/O | codec_i2s_mclk      | uart_tx_boot  |
| IO2            | I/O | codec_i2s_bclk      |               |
| IO3 RXD0       | I/O |                     | uart_rx_boot  |
| IO4            | I/O | codec_i2s_wclk      |               |
| IO5            | I/O | codec_i2s_din       |               |
| IO12           | I/O |                     | jtag_tdi      |
| IO13           | I/O |                     | jtag_tck      |
| IO14           | I/O |                     | jtag_tms      |
| IO15           | I/O |                     | jtag_tdo      |
| IO18           | I/O | codec_i2s_dout      |               |
| IO19           | I/O | i2c_sda             |               |
| IO21           | I/O | i2c_clk             |               |
| IO22           | I/O | lcd_scl             |               |
| IO23           | I/O | lcd_sda             |               |
| IO25           | I/O | lcd_res             |               |
| IO26           | I/O | lcd_dc              |               |
| IO27           | I/O | pmic_nCE            |               |
| IO32           | I/O | io_expander_reset_l |               |
| IO33           | I/O | uart_tx_log         |               |
| IO34           | I   | uart_rx_log         |               |
| IO35           | I   |                     |               |
| IO36 SENSOR_VP | I   | io_expander_int     |               |
| IO39 SENSOR_VN | I   | pmic_nINT           |               |

### 5.2 GPIOs Pin extender

|IO°  |Type | Active Mode      |
|-----|-----|------------------|
| IO0 |     | button_enter     |
| IO1 |     | button_back      |
| IO2 |     | button_next      |
| IO3 |     | button_direction |
| IO4 |     | led_adc          |
| IO5 |     | led_dac          |
| IO6 |     | lcd_vcc_ctrl     |
| IO7 |     | codec_reset_l    |

## 6. I2C addresses

I2C address is over 7 bits.

- 0x00 -> (ESP Master)
- 0x18 -> Audio Codec
- 0x6a -> PMIC
- 0x20 -> IOs expander

## 7. Layout

### 7.1 Layout main ideas

The goals were to have:

- all the high speed signals on the first layer
  - I2S
  - USB data
- all the analog signals on the first layer level and in a define region.
- respect the mecanical constraints for the buttons position. To be able to have a functional box over it.
- have the antena on a side that could be directed toward the user
- use the led in the box.
- match impedance for the usb
- the RF should be far away from the analog
- close to the antenna, there should be no HF signals and as much ground as possible.

The user interface is the back of the PCB:

![jack<>bluetooth_rev3.0_bottom.png](images/jack<>bluetooth_rev3.0_bottom.png)

The spi screen is going to be put in the big white rectangle. The three buttons : previous, enter and next for controlling the screen are put under the screen. A slide switch was added to select the mode between Jack -> Bluetooth or Bluetooth -> Jack. It also controls the two LEDs indicating the direction.

Two more leds are available on the top right corner to indicate if the device is on and the last one to indicate if the device is plugged.

On the other side, the main one, there are all the main components and all the main signals :

![jack<>bluetooth_rev3.0_top.png](images/jack<>bluetooth_rev3.0_top.png)

All the inputs/outputs connectors are located on the same slide of the board. The antenna is outside of the board to have the maximum omni-directional radiation pattern. The analog part is as far as possible from the RF.

### 7.2 Impedance matching

The USB differential pair should be 90ohms.

![impedance_matching_computations](images/impedance_matching_computations.png)

### 7.3 Trace Length

- usb are the same length (+-0.1mm)
- left and right of jack 3.5mm are the same length (+-0.1mm)
- left and right of jack 2.5mm are the same length (+-0.1mm)
- for I2S:
  - Shortest line : 25.75mm
  - Longest line : 31.42mm
  - delta is 5.67mm. Assuming a delay of 6.2ps/mm, it creates a delay of 35ps which respects the timing requirements of the audio-codec.

### 7.4 Layers

L1 : Signals
L2 : GND
L3 : Power
L4 : Low signals

#### 7.4.1 Layer 1

![layout_l1.png](images/layout_l1.png)

#### 7.4.2 Layer 2

![layout_l2.png](images/layout_l2.png)

#### 7.4.3 Layer 3

![layout_l3.png](images/layout_l3.png)

#### 7.4.4 Layer 4

![layout_l4.png](images/layout_l4.png)
