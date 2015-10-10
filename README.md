# MCPE Viz
by Plethora777

MCPE Viz is a command-line tool that parses Minecraft Pocket Edition (MCPE) world files.  It creates overview images from the world files, and reports *lots* of details on your world.

It can also:

* Create a powerful web-based viewer for your world.

* Create images of individual layers of your world

* Create an image of the biomes in your world

* Create an image of block light in your world (kind of like an image from space at night)

* Create an image of the block height in your world (kind of like a topographic map)

* Create an image of the grass color in your world (it's pretty interesting!)

* Create movies of all layers from bedrock to build limit (*ffmpeg* required)

* Dump a *huge* amount of info about your world including:
  * Dumps detailed information about all mobs, players and items in your world
  * For each 16x16 block chunk:
    * Top blocks (block type, block data, biome)
    * Histogram of all blocks in the chunk
    * And more...
  * For each world type (e.g. overworld, nether):
    * Histogram of biomes (per-block column)


## Examples

Here are example images: http://imgur.com/a/YiIWe

Here's an example of movie output: https://www.youtube.com/watch?v=kBCOrctFIGM


## Experimental win32 and win64 builds

I used mingw to build win32 and win64 executables.  You can find the exe and required dll's in mcpe_viz.win32.zip and mcpe_viz.win64.zip.  To try it out on windows, download either of the zip files, unzip it and run the exe.


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


### Experimental Browser-based Openlayers Viewer

You can create a browser-based viewer for your world files.  It will allow you to pan, zoom and switch between the different images.  It's pretty spiffy!  To make a complete viewer (with all the images), do something like this:

```
> ./mcpe_viz --db ./mcpe/another1/ --out ./mcpe/output/mcpe_output10 --html --all-image --slices
```

This will create a LOT of images -- individual images for each layer of the overworld and nether, as well as all the regular images.  It will also create "./mcpe/output/mcpe_output10.html".  Load this file in your browser and you will have an Openlayers-based viewer for your world!  Openlayers (http://openlayers.org/) is a super cool javascript library for mapping.

Usage notes:
* Zoom in with double click or press the "+" button (upper left)
* Zoom out with shift-double click or press the "-" button (upper left)
* Move with click and drag
* Rotate with shift-alt click and drag
* Your mouse position is shown in the upper right in world coordinates and image coordinates
* You can go full-screen (button under mouse position window)
* Tool bar at the bottom of the screen:
  * Choose Overworld or Nether
  * Scroll through the layers from 0 (bedrock) to 127 (build limit)
  * Choose Overview, Biome etc
  * Show Mobs and Items -- click on an entry and the map will be updated with labeled points.  You can toggle on as many different types as you like.  Click on the points or labels to get more info on the item.  Note that labels are not drawn when you are zoomed out.
  

## Customization

You can copy mcpe_viz.cfg to ~/.mcpe_viz/mcpe_viz.cfg and then edit that file to customize the way mcpe_viz works.

You can copy mcpe_viz.xml to ~/.mcpe_viz/mcpe_viz.xml and then edit that file to set custom colors for blocks and biomes.


## Compiling from source

If you just want to run the software on windows, see above :)  If you would like to compile it for Linux (or Windows), read on.

### Requirements

* You know how to compile things :)
* Mojang's LevelDB from github (see below) (https://github.com/Mojang/leveldb-mcpe.git)
* libnbt++ from github (see below) (https://github.com/ljfa-ag/libnbtplusplus.git)
* [Optional] *ffmpeg* for creating movies


### How to compile

#### Mojang's LevelDB

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

#### libnbt++

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

#### Compile mcpe_viz

From the top directory of mcpe_viz:

```
> make
```

If all goes well, you will have "mcpe_viz" in build/


## TODO

There is lots still todo.  Search on 'todo' in the code to see what needs attention.
