# Environment Loader
This is a payload that should be run with [CustomRPXLoader](https://github.com/wiiu-env/CustomRPXLoader).

## Usage
Put the `payload.rpx` in the `sd:/wiiu/` folder of your sd card and use the `CustomRPXLoader` to run this setup payload, hold X on the Gamepad while loading to force open the menu.

This payload checks for enviroments in the following directory: `sd:/wiiu/environments/`. 

Example file structure for having a `tiramisu` and a `installer` environment on the sd card:
```
sd:\wiiu\environments\tiramisu\modules\setup\00_mocha.rpx
sd:\wiiu\environments\tiramisu\modules\setup\01_other_cool_payload.rpx
sd:\wiiu\environments\installer\modules\setup\00_mocha.rpx
sd:\wiiu\environments\installer\modules\setup\01_installer_launcher.rpx
```

When you start the EnvironmentLoader a selection menu appears. Use Y on the Gamepad to set a default enviroment.
To open the selection menu when a default enviroment is set, hold X on the Gamepad while launching the EnvironmentLoader.

When launching an given enviroment, all `.rpx` files in `[ENVIRONMENT]/modules/setup` will be run.
- Make sure not to call `exit` in the setup payloads
- The files will be run in the order of their ordered filenames.

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
- Copy pasted stuff from dimok
- Copy pasted resolving the ElfRelocations from [decaf](https://github.com/decaf-emu/decaf-emu)
- https://github.com/serge1/ELFIO