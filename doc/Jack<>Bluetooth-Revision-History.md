
# Jack <> Bluetooth Revision history

## Rev 1.0

Development was halted at the schematic stage because major changes in the schematic were required (moving from one ADC and one DAC to a single CODEC) and the device was difficult to route on a 2-layer PCB.

### Rev 1.0 schematic:
!["jack<>bluetooth_rev1_schematic.png"](images/jack<>bluetooth_rev1_schematic.png)

## Rev 2.0

The design was splitted into two separate boards: power and audio. Two schematics and layouts were created. The audio board includes one audio codec instead of the two DAC and ADC.
After a PCB review, this revision was abandoned in favor of a single device with a 4-layer board.

### Rev 2.0 Audio board schematic:

![jack<>bluetooth_rev2_schematic.png](images/jack<>bluetooth_rev2_schematic.png)

### Rev 2.0 Audio board layout:

![jack<>bluetooth_rev2_top.png](images/jack<>bluetooth_rev2_top.png)
![jack<>bluetooth_rev2_bottom.png](images/jack<>bluetooth_rev2_bottom.png)

### Rev 0.1 Power management device schematic:

![Power-Management-Device_schematic.png](images/Power-Management-Device_schematic.png)

### Rev 0.1 Power management device layout:

![Power-Management-Device_top.png](images/Power-Management-Device_top.png)
![Power-Management-Device_bottom.png](images/Power-Management-Device_bottom.png)

## Rev 3.0

The current revision, still in progress. Waiting PCB review.

### Changes Compared to Rev 2.0:

- Takes mechanical constraints into account (to design an enclosure).
- Allows the screen power to be shut down using a PMOS control.
- Uses a switching voltage regulator for digital power instead of LDOs.
- Adds ESD protection to the UART.
- Fixes issues, bugs, and bad practices from Rev 2.0.

### Rev 3.0 Schematic :

![jack<>bluetooth_rev3.0_schematic.png](images/jack<>bluetooth_rev3.0_schematic.png)

Also available in pdf: [jack<>bluetooth_rev3.0_schematic.pdf](jack<>bluetooth_rev3.0_schematic.pdf)

### Rev 3.0 Layout :

![jack<>bluetooth_rev3.0_top.png](images/jack<>bluetooth_rev3.0_top.png)

![jack<>bluetooth_rev3.0_bottom.png](images/jack<>bluetooth_rev3.0_bottom.png)
