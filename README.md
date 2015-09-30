# MCPE Viz
Plethora777 -- 2015.9.26

MCPE Viz is a command-line tool that parses Minecraft Pocket Edition (MCPE) world files (LevelDB).  It creates overview images from the world files, and reports *lots* of details on your world.

It can also:

* Create images of individual layers of your world

* Create an image of the biomes in your world

* Create movies of all layers from bedrock to build limit (*ffmpeg* required)

* Dump a *huge* amount of info about your world including:
  * For each 16x16 block chunk:
    * Top blocks (block type, block data, biome)
    * Histogram of all blocks in the chunk
    * And more...
  * For each world type (e.g. overworld, nether):
    * Histogram of biomes (per-block column)

## Experimental win32 build

I used mingw32 to build a win32 executable.  It is lightly tested :)  You can find the exe and required dll's in mcpe_viz.win32.zip


## Requirements

* You know how to compile things :)
* Mojang's LevelDB from github (see below) (https://github.com/Mojang/leveldb-mcpe.git)
* libnbt++ from github (see below) (https://github.com/ljfa-ag/libnbtplusplus.git)
* [Optional] *ffmpeg* for creating movies


## How to compile

### Mojang's LevelDB

The code uses Mojang's modified version of LevelDB.  Here's how to compile it (from the top dir of mcpe_viz):

```
> git clone https://github.com/Mojang/leveldb-mcpe.git
> chdir leveldb-mcpe
> make
```

If all goes well, there will be a "libleveldb.a" in leveldb-mcpe/

To get it to compile on Fedora, I found that I needed to do this from leveldb-mcpe/:

```
> ln -s /usr/include include/zlib
```

If you have compile errors, check the README* files from leveldb-mcpe for prerequisites

### libnbt++

The code uses libnbt++.  Here's how to compile it (from the top dir of mcpe_viz):

```
> git clone https://github.com/ljfa-ag/libnbtplusplus.git
> chdir libnbtplusplus
> mkdir build
> chdir build
> cmake .. -DNBT_BUILD_TESTS=OFF
> make
```

If all goes well, there will be a "libnbt++.a" in libnbtplusplus/build/

If you have compile errors, check the README* files from libnbtplusplus for prerequisites

### Compile mcpe_viz

From the top directory of mcpe_viz:

```
> make
```

If all goes well, you will have "mcpe_viz" in build/


## Usage

**DO NOT RUN THIS ON YOUR ORIGINAL MCPE DATA FILES**

**DO NOT RUN THIS ON YOUR ONLY BACKUP OF MCPE DATA FILES**

**MAKE A COPY OF YOUR DATA AND RUN THIS AGAINST THAT COPY ONLY!**

See "./mcpe_viz --help" for the most up-to-date usage info

Here's an example invocation:

```
> ./mcpe_viz --grid --db ./mcpe/another1/ --out ./mcpe/output/mcpe_output8
```

This will read the leveldb from "./mcpe/another1" and name output files starting with "./mcpe/output/mcpe_output8", and it will draw chunk boundaries on your output image.  This also dumps the *voluminous* output to "mcpe_output8.log".  The log file has a *ton* of interesting information about your world.  "grep" is your friend.

Please note that --db expects the directory which contains "level.dat".


## CUSTOMIZATION

You can copy mcpe_viz.cfg to ~/.mcpe_viz/mcpe_viz.xml and then edit that file to customize the way mcpe_viz works.

You can copy mcpe_viz.xml to ~/.mcpe_viz/mcpe_viz.xml and then edit that file to set custom colors for blocks and biomes.


## TODO

This is just a quick and dirty tool that I made so that I could get an overview of one of my MCPE worlds.  The code is a bit messy and there is lots still todo.  Search on 'todo' in the code to see what needs attention.
