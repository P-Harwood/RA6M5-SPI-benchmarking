# EK-RA6M5 CPU usage benchmark

This project runs coremark benchmarker on the ek_ra6m5 

## Initiate Commands
This creates a new directory, downloads the application and the working Zephyr build at the time of creation. 
Go into any new directory, e.g. Documents/test/ and run the following commands in a command prompt terminal:
```
mkdir coremark_Test
cd coremark_Test
python -m venv .venv
.venv\Scripts\activate.bat
pip install west
west init -m https://github.com/P-Harwood/RA6M5-SPI-benchmarking
cd applications\RA6M5_bench
west update
pip install -r ..\..\zephyr\scripts\requirements-base.txt
west sdk install -t arm-zephyr-eabi
west build -b ek_ra6m5 -d build\v1 -p always .
west flash -d build\v1
```



## Wiring

- Jumper P202 to P203 (SPI0 loopback)
- Jumper P613 to P614 (UART7 loopback)
- UART to P411 and P410 for reading output

## Build and flash

From inside the application directory:

```
west build -b ek_ra6m5 -d build\v1 -p always .
west flash -d build\v1
```

## Clocks



This application was requested to be made with the System clock (ICLK) running at the full 200Mhz (Source #1).
The Fastest achievable speed on the PCLKA is 100Mhz (Source #1).
so the PCKLA is sourced from ICLK with a divisor of 2.

SPI
The target SPI bit rate is 15Mhz.  
The achievable bit rate on the ra6m5 is: Bit rate =  PCLK/ 2x(n+1)* 2^N 
where n = {0,1,2 ... 255}, N = {0,1,2,3}    (Source #2)
The closest achievable bit rate to 15Mhz with a PCLK of 100Mhz is a divisor of 6
resulting in a Bitrate of 16.66Mhz. This can be achieved with a SPBR of 2 and a BRDV of 0
(SPBR) n = 2, (BRDV) N = 0, PCLK = 100Mhz
Bitrate x           =       100MHz  / 2*(2+1) * 2^0
Bitrate 16.666Mhz   =       100MHz  / 2*(3) * 1
Bitrate 16.666Mhz   =       100MHz  / 6

A PCLK speed of 90Mhz could achieve a 15Mhz bit rate, by using a SPBR of 6.
However, this would require reducing the ICLK to 180Mhz, and capping QSPI speed to 45Mhz Max.

Note: If 15Mhz is required then using r_sci_spi can be explored, with this a bit rate of
15.0390625 mpbs can be achieved. this is +0.26% error. MDDR = 154, BRR = 0 SMR = 0 (source #4)


QSPI
The target QSPI bit rate is 60Mhz
The Highest achievable bit rate on the RA6M5 is PCLKA/2 (Source #3) 
PCLK max speed is 100Mhz (Source #1), sp the max QSPI speed is 50Mhz

UART
The target SPI bit rate is 7.5Mbds.  
7.48697916 mpbs can be achieved. this is +0.174% error. MDDR = 230, BRR = 1 SMR = 0 (source #4)


Source #1:  RA6M5 User Manual Rev.1.50, Page 151, Table 8.2 Clock generation circuit specifications for the internal clocks

Source #2:  RA6M5 User Manual Rev.1.50, Page 1562, Chapter 34.2.8 SPBR : SPI Bit Rate Register

Source #3:  RA6M5 User Manual Rev.1.50, Page 1639, Chapter 35.2.3 SFMSKC : Clock Control Register

Source #4:  RA6M5 User Manual Rev.1.50, Page 1070, Table 30.8 Clock source settings

## ADC

The adc in this example is using FSP calls. This is because the Rensas Zephyr upstream code does not offer continous support, 
FSP calls are localised to the initialisation. Upstream Zephyr implementation hardcodes Single scan in its source code.

## DMA vs DTC

DTC and DMA are both technologies which move memory without interaction from the processor. DMA stores its information in registers 
DTC stores its data in SRAM rather than registers, this offers more flexibility such as chaining commands, and every interrupt
can have a linked DTC operation while DMA is limited by register size. The Renesas implementaiton of DTC in the Zephyr code makes
it a support function for other peripherals which work in the background such as for  SPI or SCI, accessible through prj.conf.
DMA is offered as a standalone peripheral. In this applicaiton DTC is the best choice for SPI/SCI.
