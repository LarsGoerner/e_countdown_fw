# E-Countdown Firmware Project

Target of this project is to create a firmware for a small battery driven
which provides date information as well as a countdown (in days) until
a event day defined by the client via Bluetooth LE.

The device also provides multiple customization options. The background
can be customized by any image content. Font family, style and size are
customizable, too. The device contains multiple fonts to choose.

A Fuel Gauge provides battery data which are presented on the display
and via bluetooth interface.

Besides the actual firmware this project also contains a display driver component
for the [Waveshare 4.26inch e-paper Display](https://www.waveshare.com/product/displays/e-paper/epaper-2/4.26inch-e-paper.htm).

## Build this project

### Pre-requirements

The device needs to be connected via USB to the host PC.

Make sure that the ESP-IDF, build tools and toolchain are installed on the
host machine. See Espressif's [getting started guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/index.html) for more information.

The repository content can be cloned like this:

```bash
> git clone https://github.com/LarsGoerner/e_countdown_fw.git
```

### Build

To build the project you need to navigate into the project and execute the build command

```bash
> cd e_countdown_fw
> idf.py build
```

### Flash & monitor

```bash
> idf.py flash
> idf.py monitor
```

Monitoring can be stopped by pressing strg+t followed by strg+x.

## Build the documentation

The documentation can be generated with [doxygen](https://www.doxygen.nl/index.html).

Make sure that your doxygen version is higher than 1.10.0.

```bash
> cd docs
> doxygen doxygen.conf
```

This will generate the html documentation.