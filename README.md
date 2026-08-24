To alter Bit rate or word size edit  SPI_FREQ_HZ	 WORD_BITS	

Connect pin 202 to pin 203 with a jumper cable
Connect to the uart pins (411 and 410) to read output from the application

There are four configurations to use. These four combinations are made from the inclusion or absense of two factors. If profiling is enabled/disabled and if DTC is enabled/disabled

By default, just run with profiling enabled. 


To build the four configurations run:
west build -p always -d build-dtc-prof -b ek_ra6m5 -- -DEXTRA_CONF_FILE=overlay-profiler.conf && west build -p always -d build-nodtc-prof -b ek_ra6m5 -- -DEXTRA_CONF_FILE=overlay-profiler.conf -DCONFIG_SPI_RA_DTC=n

(or for including builds without profiler)
west build -p always -d build-dtc -b ek_ra6m5 && west build -p always -d build-nodtc -b ek_ra6m5 -- -DCONFIG_SPI_RA_DTC=n && west build -p always -d build-dtc-prof -b ek_ra6m5 -- -DEXTRA_CONF_FILE=overlay-profiler.conf && west build -p always -d build-nodtc-prof -b ek_ra6m5 -- -DEXTRA_CONF_FILE=overlay-profiler.conf -DCONFIG_SPI_RA_DTC=n







This command takes longer to run since it is a pristine build on all 4 configurations. After the first build, a faster build can be run for small changes like changing the spi bit rate or spi transfer size
west build -d build-dtc && west build -d build-nodtc && west build -d build-dtc-prof && west build -d build-nodtc-prof

To run one of the builds use west flash .e.g

west flash -d build-dtc-prof
west flash -d build-nodtc-prof