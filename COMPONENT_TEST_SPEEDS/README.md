# QSPI bridge testing

The tools in this directory will help you to test the QSPI bridge device. Ensure that the device is connected and not open in another application (such as Control Center)

## run_test_script.bat

Launch this script from an instance of command prompt.

The script performs the following:
1. Ensure python is available
2. Create a virtual environment and activate it
3. Run `scripts\qspi_bridge_test_app.py` within the virtual environment

This default run of `scripts\qspi_bridge_test_app.py` will test a default write of 100 MB

### Running more tests

Follow the steps below to run read, write, frequency change, and mixed tests:

1. Ensure that the virtual environment is created and activated (run above script)
2. Run the following command:

```cmd
python scripts\qspi_bridge_test_app.py --help
```

3. The program output will display various options to use for testing the device

### Example test commands

* Generate a result chart by testing writes and reads for QSPI clock frequencies from 11 MHz to 16 MHz:
```powershell
python scripts\qspi_bridge_test_app.py --test chart --chart-min-freq 12 --chart-max-freq 16
```

* Set the QSPI clock to 26 MHz:
```powershell
python scripts\qspi_bridge_test_app.py --test set-clk --clk-mhz 26
```

* Test QSPI write with 120 MB data:
```powershell
python scripts\qspi_bridge_test_app.py --test write --size 120
```

* Test QSPI read and write both, with 60 MB data:
```powershell
python scripts\qspi_bridge_test_app.py --test both --size 60
```
