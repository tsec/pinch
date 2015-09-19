Pinch
-----

Pinch is an arcade emulator launcher, written for the
[Pijamma project](https://github.com/Pijamma).

It's incomplete, but usable.

Setting up
----------

Run pinch by executing `run.sh`. Most of the configuration can
be edited here by tweaking the various shell variables:

`EMU_DIR`
Directory where the emulator is located

`EMU_EXE`
Name of the executable

`EMU_ARGS`
Additional arguments to the emulator. Selected archive will be
included in addition to these arguments.

Titles
------

Titles are specified in a file named `config.json`. Example:

```
{
"sets": [
	{ "archive": "mslugx" },
	{ "archive": "samsho4" }
]}
```

Each title should have an accompanying screenshot in 
a subdirectory called `images`, in PNG format, with the same
name as the archive (e.g. `mslugx.png`, `samsho4.png`).

Usage
-----

To move between titles, press left or right arrow on the keyboard,
or use joystick 1 left/right. To select a title, press SPACE; to
exit press F12.

Joystick buttons can also be used to launch/exit - to specify,
edit `launch_button` and `exit_button` constants in
[pimenu.c](blob/master/pimenu.c).

Command-line arguments
----------------------

`-k <seconds>`
Enables kiosk mode - after specified number of seconds, pinch
launches a title at random.

`--launch-next`
When set, launches the title following the last one launched and
exits.

Compiling
---------

To compile on a Raspberry Pi, install SDL:

`sudo apt-get install libsdl-dev`

Run `make` to build.

License
-------

Pinch is licensed under the [Apache
license](http://www.apache.org/licenses/LICENSE-2.0), although parts
of it may be licensed differently. See [NOTICE](NOTICE) for
additional information.
