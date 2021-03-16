# iRODS Client - iCommands

This repository hosts the client iCommands, the default command line interface to iRODS.

## Build

To build the iCommands, you will need the `irods-dev` and `irods-runtime` packages.

This is a CMake project and can be built with:

```
cd irods_client_icommands
mkdir build
cd build
cmake -GNinja ../
ninja package
```

## Install

The packages produced by CMake will install the ~50 iCommands, by default, into `/usr/bin`.

## Build without any Package Repositories

If you need to build the iRODS iCommands without the use of any APT/YUM repositories, it will be necessary
to build all the dependencies yourself.  The steps include:

1. Download, build, and install packages from https://github.com/irods/externals
2. Update your `PATH` to include the newly built CMake
3. Download, build, and install `irods-dev(el)` and `irods-runtime` from https://github.com/irods/irods
4. Download, build, and install `irods-icommands` from https://github.com/irods/irods_client_icommands
   
Our dependency chain will shorten as older distributions age out.

The current setup supports new C++ features on those older distributions.

## Userspace Packaging

A `userspace-tarball` buildsystem target is provided to generate a userspace tarball package. This package
will contain the iCommands and all required library dependencies.
See `packaging/userspace/build_and_package.example.sh` for an example of how to build and package the
iCommands for userspace deployment.

### Build Dependencies

The userspace packager needs a few extra packages to work properly:
- Required: Python 3.5+.
    - Recommended: Python 3.6+
- Required: `setuptools` Python package.
    - Available as `python3-setuptools` via yum/apt on Centos 7/Ubuntu.
    - Available as `setuptools` on PyPI.
- Required: `psutil` Python module.
    - Available as `python3-psutil` via apt on Ubuntu.
    - Available as `python36-psutil` via yum on Centos 7.
    - Available as `psutil` on PyPI.
- Required for Python <3.5.3 (Ubuntu 16.04) only: `typing` Python site-package.
    - Available as `typing` on PyPI. Version 3.7.4.3 recommended.
- Recommended: `lief` Python 3 module, version 0.10.0+ (preferably 0.11.0+).
    - Available as `lief` on PyPI.
        - The latest version available for Python 3.5 (Ubuntu 16.04) is 0.10.1. If installing via `pip`,
          the version number must be manually specified.
- Recommended: `chrpath` tool
    - Available as `chrpath` via yum/apt on Centos/Ubuntu.

If you've got `pip`, the following one-liners should get all the Python dependencies installed:
- For Python 3.5 (Ubuntu 16.04): `python3 -m pip install lief==0.10.1 psutil setuptools typing==3.7.4.3`
- For Python 3.6+: `python3 -m pip install lief psutil setuptools`
