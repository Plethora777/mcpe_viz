# MCPE Viz
Plethora777, 2015.9.26

MCPE Viz is a command-line tool that can parse Minecraft Pocket Edition (MCPE) world files (leveldb).  It can create images from the world files to create a world overview map, and report lots of details on your world.

It can also:

* Create images of individual layers of your world

* Create movies of all layers from bedrock to build limit (FFMPEG required)

* Dump a *huge* amount of info about your world including:
  * For each 16x16 block chunk:
    * Top blocks (block type, block data, biome)
    * Histogram of all blocks in the chunk
    * And more...
  * For each world type (e.g. overworld, nether):
    * Histogram of biomes (per-block column)


## Requirements

You know how to compile things :)
Mojang's LevelDB from github (see below)
[Optional] FFMPEG for creating movies


## How to compile

### Mojang's LevelDB

The code uses Mojang's modified version of LevelDB.  Here's how to compile it (from the top dir of mcpe_viz):
`> git clone https://github.com/Mojang/leveldb-mcpe.git
`> chdir leveldb-mcpe
`> make

If all goes well, there will be a "libleveldb.a" in leveldb-mcpe/

To get it to compile on Fedora, I found that I needed to do this from leveldb-mcpe/:
`> ln -s /usr/include include/zlib

If you have compile errors, check the README* files from leveldb-mcpe for prerequisites

### Compile mcpe_viz

From the top directory of mcpe_viz:
`> make

If all goes well, you will have "mcpe_viz" in build/


## Usage

** DO NOT RUN THIS ON YOUR ORIGINAL MCPE DATA FILES **
** DO NOT RUN THIS ON YOUR ONLY BACKUP OF MCPE DATA FILES **
MAKE A COPY OF YOUR DATA AND RUN THIS AGAINST THAT COPY ONLY!

See "./mcpe_viz --help" for the most up-to-date usage info

Here's an example invocation:
'> ./mcpe_viz --grid --db /fs/d0/mcpe/another1/ --out /fs/d0/mcpe/output/mcpe_output8 > logfile

This will read the leveldb from "/fs/d0/mcpe/another1" and name output files starting with "/fs/d0/mcpe/output/mcpe_output8", and it will draw chunk boundaries on your output image.  This also dumps the *voluminous* output to "logfile".  The log file has a ton of interesting information about your world.  "grep" is your friend.

Please note that --db expects the directory which contains "level.dat".


## TODO

This is just a quick and dirty tool that I made so that I could get an overview of one of my MCPE worlds.  The code is a bit messy and there is lots still todo.  Search on 'todo' in the code to see what needs attention.
