# Z-Wave CLI (zwavecli)

**Linux | Perl and C++ | Standalone Z-Wave control**

## Overview

Z-Wave CLI is a lightweight, standalone command-line utility for controlling and managing Z-Wave devices through a USB Z-Wave controller on Linux.

The project is designed for system administrators, developers, home automation enthusiasts and anyone who wants direct command-line access to Z-Wave devices without installing an entire home automation platform or running a permanent background daemon.

It uses a Perl command-line frontend, a custom C++ shared library and a modified version of OpenZWave.

## Features

- Standalone command-line operation without a background daemon.
- Device inclusion and normal exclusion.
- Removal of failed, broken or unreachable devices.
- Protection against accidental removal of the controller itself.
- Enumeration of registered Z-Wave nodes.
- Device identification and command-class information.
- Binary switch ON/OFF control with status verification.
- Live device status queries.
- Automatic legacy 8-bit node addressing on supported bridge controllers.
- Independent network membership verification after inclusion and exclusion.
- Configurable serial device and command-line override.
- Standardized error messages and exit codes for shell scripting.
- No requirement for Python, Docker, Home Assistant or a cloud account.

## Hardware Compatibility

The following hardware has been tested during development:

| Hardware | Description | Status |
| --- | --- | --- |
| Zooz 800 Series USB Z-Wave Stick | USB bridge controller, firmware 7.24 | Tested |
| Minoston MP31Z | Z-Wave plug-in binary switch | Tested |
| Raspberry Pi | Linux development and testing platform | Tested |

Other controllers and devices may work, but have not yet been verified. Support for a device's inclusion and identification does not necessarily mean that all of its command classes can be controlled by this utility.

The current device-control implementation targets binary ON/OFF switches. Dimmers, locks, sensors, thermostats, Z-Wave Long Range operation and secure S2 inclusion are not currently supported or validated.

Use controllers and devices designed for the same regional Z-Wave frequency.

## Requirements

- Linux
- A supported USB Z-Wave controller
- Permission to read and write the controller's serial device
- Perl
- Perl module `FFI::Platypus`
- C++ compiler with C++11 support
- GNU Make and `pkg-config`
- Patched OpenZWave library and its configuration database

No permanent daemon is required. Each command initializes the controller, performs the requested operation and shuts down the connection.

## Installation

Z-Wave CLI is currently a working development release. Automated installation, private library paths and portable distribution packaging are still under development.

The existing tested development installation uses:

| Path | Purpose |
| --- | --- |
| `/usr/local/bin/zwavecli` | Perl executable |
| `/usr/local/lib/libozw_bridge.so` | Custom C++ bridge |
| `/usr/local/lib/libopenzwave.so.1.6` | Patched OpenZWave library |
| `/etc/openzwave/` | OpenZWave configuration database |
| `/etc/zwavecli.conf` | Optional system-wide CLI configuration |

The frontend requires the matching custom bridge and patched OpenZWave library. Installing an unmodified distribution-provided OpenZWave library is not sufficient for the tested Zooz 800 controller configuration.

A complete, independently tested installation procedure will be documented when the build and installation system is finalized.

## Configuration

The default serial device is `/dev/ttyACM0`.

An optional configuration file named `zwavecli.conf` can override this default:

```ini
# Z-Wave CLI configuration
DEVICE=/dev/ttyACM0
```

Configuration files are searched in the following order:

1. `./zwavecli.conf`
2. `/etc/zwavecli.conf`

Only the first existing configuration file is used. The `-d` option overrides the configured serial device.

Example using a persistent Linux USB device path:

```bash
zwavecli -d /dev/serial/by-id/YOUR_CONTROLLER_ID list
```

The actual device path can be found using:

```bash
ls -l /dev/serial/by-id/
```

## Usage

```text
zwavecli [-d DEVICE] COMMAND
zwavecli [-d DEVICE] NODE COMMAND
```

### Controller commands

| Command | Description |
| --- | --- |
| `zwavecli list` | List registered nodes |
| `zwavecli add` | Include a new Z-Wave device |
| `zwavecli remove` | Perform normal device exclusion |
| `zwavecli version` | Display program version |
| `zwavecli help` | Display command-line help |

### Device commands

| Command | Description |
| --- | --- |
| `zwavecli NODE info` | Display device information |
| `zwavecli NODE on` | Turn a binary switch ON |
| `zwavecli NODE off` | Turn a binary switch OFF |
| `zwavecli NODE status` | Query current switch status |
| `zwavecli NODE remove` | Remove a failed or unreachable node |

### Options

| Option | Description |
| --- | --- |
| `-d DEVICE`, `--device DEVICE` | Override serial device |
| `-h`, `--help` | Display help |
| `-v`, `--version` | Display version |

## Examples

### List registered devices

```bash
zwavecli list
```

Example output:

```text
1,8
```

Node 1 is the controller in the tested network. Node 8 is a registered switch. Node IDs are assigned by the controller and may change when a device is excluded and included again.

### Include a device

```bash
zwavecli add
```

When prompted, activate the device's pairing mode using its manufacturer-specific procedure. On the tested Minoston switch, this is three quick button presses. Do not repeat the procedure while the device is already in pairing mode.

```text
Activate pairing mode on the Z-Wave device now.
Preparing Inclusion mode...
Inclusion mode ACTIVE (30 seconds).
Verifying network membership...
Inclusion successful: node 8
```

Use `zwavecli list` to obtain the assigned node ID. Verify device functionality after inclusion.

### Exclude a device

```bash
zwavecli remove
```

Normal exclusion requires participation from the device. Activate its pairing or exclusion mode as prompted, using the manufacturer's procedure.

```text
Activate pairing mode on the Z-Wave device now.
Preparing Exclusion mode...
Exclusion mode ACTIVE (30 seconds).
Verifying network membership...
Exclusion successful: node 8
```

### Remove a failed device

```bash
zwavecli 8 remove
```

This command removes a failed or unreachable device from the controller's network without requiring physical interaction with the device.

```text
Removing failed node 8...
Node 8 removed
```

The controller may reject removal if the device is still functioning or has not been classified as failed. The utility also prevents removal of the controller itself.

Unlike normal exclusion, failed-node removal does not necessarily clear the network information stored inside the removed device. If the device is later recovered, it may require normal exclusion before it can be included again.

### Control a switch

```bash
zwavecli 8 on
zwavecli 8 off
```

Successful ON/OFF operations produce no standard output. The utility verifies the requested state and returns a nonzero exit code if verification fails.

### Query live status

```bash
zwavecli 8 status
```

Possible output:

```text
ON
```

```text
OFF
```

```text
UNKNOWN
```

`UNKNOWN` indicates that the current state could not be verified and is accompanied by a nonzero exit code. Invalid or nonexistent nodes are reported as errors.

### Display device information

```bash
zwavecli 8 info
```

Example output (abbreviated):

```text
Node:             8
Manufacturer:     Unknown: id=0312
Product:          Unknown: type=ff00, id=ff0c
Device Type:      On/Off Power Switch
Name:             Unknown
Manufacturer ID:  0x0312
Product Type:     0xff00
Product ID:       0xff0c
Listening:        Yes
Routing:          Yes
Awake:            Yes
Failed:           No
Capabilities:
  0x20  COMMAND_CLASS_BASIC (v2)
  0x25  COMMAND_CLASS_SWITCH_BINARY (v2)
  0x72  COMMAND_CLASS_MANUFACTURER_SPECIFIC (v2)
```

Manufacturer and product names depend on the OpenZWave device database. Unknown names do not necessarily indicate an unsupported or malfunctioning device. Information is based on OpenZWave's discovered and cached device data.

## Exit Codes

| Code | Meaning |
| --- | --- |
| 0 | Successful operation |
| 1 | Controller initialization or communication failure |
| 2 | Invalid arguments or configuration |
| 3 | Node not found |
| 4 | Operation failed or could not be verified |
| 5 | Operation timed out |

Normal command results are written to standard output. Errors, warnings and operation progress messages are written to standard error.

Inclusion and exclusion results are checked against independently enumerated network membership. If membership changes without a confirmed controller completion notification, the utility reports the discrepancy and returns a nonzero exit code rather than incorrectly reporting a simple timeout.

## Shell Scripting

Z-Wave CLI is designed to be used directly from shell scripts, cron jobs and other automation systems.

```bash
#!/bin/bash

NODE=8

if zwavecli "$NODE" on; then
    echo "Switch turned on successfully"
else
    rc=$?
    echo "Switch operation failed (exit code $rc)" >&2
    exit "$rc"
fi
```

Example status-based automation:

```bash
if state=$(zwavecli 8 status); then
    case "$state" in
        ON)  echo "Switch is on" ;;
        OFF) echo "Switch is off" ;;
    esac
else
    echo "Unable to determine switch status" >&2
fi
```

Avoid running multiple commands concurrently against the same serial controller. Concurrent access has not been validated.

## Architecture

```text
Shell / Automation Script
           |
           v
       zwavecli
    (Perl frontend)
           |
      FFI::Platypus
           |
           v
   libozw_bridge.so
     (C++ bridge)
           |
           v
   Patched OpenZWave
           |
      USB Serial
           |
           v
   Z-Wave Controller
           |
      Z-Wave Network
           |
           v
      Z-Wave Devices
```

## OpenZWave Modifications

The project uses a modified OpenZWave 1.6 development revision based on `3fff11d2`. The exact upstream revision and patch set are to be preserved in the source distribution.

The modifications include:

- Acceptance of bridge-type USB controllers.
- Translation of incoming bridge application-command messages into the legacy format expected by OpenZWave.
- Automatic initialization of legacy 8-bit node addressing on bridge controllers.
- Corrected Serial API setup response handling.
- Supervision bypass for binary switch commands to address observed ON/OFF control failures in the tested configuration.

These modifications are required for the tested hardware configuration. They have not been validated against every OpenZWave-compatible controller or device.

## Security and Operational Limitations

- Secure S2 inclusion is not currently supported.
- The tested development network uses non-secure inclusion; it should not be treated as suitable for security-critical devices.
- Inclusion and exclusion use a 30-second active window. Controller initialization, shutdown and independent membership verification add to the total execution time.
- OpenZWave currently creates logs and cache files in the working directory. Relocating runtime files is planned.
- Some devices may remain in pairing or configuration mode briefly after the controller reports successful inclusion.
- A successful membership change does not guarantee that every device capability has been fully interrogated.

## Project Status

Z-Wave CLI has been tested for:

- Controller initialization and USB power-cycle recovery.
- Automatic 8-bit node addressing.
- Inclusion and exclusion, including independent membership verification.
- Inclusion and exclusion timeouts.
- Device enumeration and information retrieval.
- Verified binary switch control and status reporting.
- Unresponsive and nonexistent device detection.
- Failed-device removal and rejection of removal requests for functioning devices.
- Controller removal protection.
- Argument validation and exit-code handling.

Testing has been performed on the development hardware listed above. Broader hardware compatibility and installation on clean Linux systems remain to be validated.

## Roadmap

- [ ] Complete reproducible source builds and installation scripts.
- [ ] Provide an uninstallation procedure.
- [ ] Isolate the patched OpenZWave libraries from other system applications.
- [ ] Relocate runtime logs and device caches.
- [ ] Reduce controller initialization and command execution times.
- [ ] Update the manufacturer and product identification database.
- [ ] Expand controller and device compatibility testing.
- [ ] Investigate secure inclusion support.
- [ ] Add support for additional device command classes.
- [ ] Provide generic Linux source archives and distribution-specific packages.

## License

The license for the original Z-Wave CLI code has not yet been selected. OpenZWave and other third-party components remain subject to their respective licenses. Their license notices and applicable source distribution requirements must be preserved.

## Acknowledgments

This project builds upon the work of the [OpenZWave project](https://github.com/OpenZWave/open-zwave) and uses [FFI::Platypus](https://metacpan.org/pod/FFI::Platypus) to interface Perl with the native C++ bridge.

Z-Wave CLI is an independent project and is not affiliated with or endorsed by the Z-Wave Alliance, Zooz or Minoston.
