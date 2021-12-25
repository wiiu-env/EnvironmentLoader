# Setup payload
This is a payload that should be run with [CustomRPXLoader](https://github.com/wiiu-env/CustomRPXLoader).

## Usage
Put the `payload.rpx` in the `sd:/wiiu/` folder of your sd card and use the `CustomRPXLoader` to run this setup payload.

This payload checks for enviroments in the following directory: `sd:/wiiu/environments/`. 
Per default it's booting `sd:/wiiu/environments/default`, to choose which environment will be booted, hold **X** on the gamepad while launching.

When launching an given enviroment, all `.rpx` files in `[ENVIRONMENT]/modules/setup` will be run.
- Make sure not to call `exit` in the setup payloads
- The one time setups will be run in the order of their ordered filenames.

Example file structure for having a `default` and a `installer` environment on the sd card:
```
sd:\wiiu\environments\default\modules\setup\00_mocha.rpx
sd:\wiiu\environments\default\modules\setup\01_other_cool_payload.rpx
sd:\wiiu\environments\installer\modules\setup\00_mocha.rpx
sd:\wiiu\environments\installer\modules\setup\01_installer_auncher.rpx
```

## Building
Make you to have [wut](https://github.com/devkitPro/wut/) installed and use the following command for build:
```
make install
```

## Building using the Dockerfile

It's possible to use a docker image for building. This way you don't need anything installed on your host system.

```
# Build docker image (only needed once)
docker build . -t environmentloader-builder

# make 
docker run -it --rm -v ${PWD}:/project environmentloader-builder make

# make clean
docker run -it --rm -v ${PWD}:/project environmentloader-builder make clean
```


## Credits
- maschell
- Copy paste stuff from dimok
- Copy pasted the solution for using wut header in .elf files from [RetroArch](https://github.com/libretro/RetroArch)
- Copy pasted resolving the ElfRelocations from [decaf](https://github.com/decaf-emu/decaf-emu)