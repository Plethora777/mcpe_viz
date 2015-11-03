/*
  Minecraft Pocket Edition (MCPE) World File Visualization & Reporting Tool
  (c) Plethora777, 2015.9.26

  GPL'ed code - see LICENSE

  Requires Mojang's modified LevelDB library (see README.md for details)
  Requires libnbt++ (see README.md for details)

  To build it, use cmake


  todobig

  * slime chunks
  -- web: use it as an overlay
  -- alter opacity so non-slime chunks are CLEAR
  -- iterate over all chunk space (current just does explored)
  -- don't do it for the nether?

  * add support for tiling images -- see eferris world that is too big for firefox

  ** elevation overlay is broken when we use tiles in web app (turn it on; seems to work; zoom out; it's way off)
  ---- this is the only thing keeping me from publishing tiled web app

  ** create an elevation overlay here instead of js? (work around OL bugs)

  ** do away with image coord space for web app? 
  ---- coords are crazy -- confirmed that negative-Z is north

  ** web: overlay slime chunks -- find slime chunk calculation

  ** do slime chunks overlay here?  it's very hard to do in OL because of, well, OL (can't easily use Long.js lib for Java Random number emulator)
  ---- if we do that, we might want to do chunk grid overlay here also


  * use boost for filesystem stuff?

  * try with clang; possible to mingw w/ clang?

  * check --out -- error if it is a directory

  * see reddit test1 world -- has this (could be result of bad mcedit or similar):
  -- WARNING: Did not find block variant for block(Wooden Double Slab) with blockdata=8 (0x8)

  * parse potions etc in inventories etc

  * do variants for items + entities?

  * join chests for geojson? (pairchest) - would require that we wait to toGeoJSON until we parse all chunks
  -- put ALL chests in a map<x,y,z>; go through list of chests and put PairChest in matching sets and mark all as unprocessed; go thru list and do all, if PairChest mark both as done

  * how to handle really large maps (e.g. someone that explores mane thousounds of chunks N and E so that result image is 30k x 30k)

  * simple gui -- db location; output path; output basename; checkbox: all images, html, html-most, html-all
  -- https://wiki.qt.io/Qt_for_Beginners

  * see if there is interesting info re colors for overview map: http://minecraft.gamepedia.com/Map_item_format

  * option to name output files w/ world name

  * use "solid" info from block info to do something? could it fix the light map?

  * update block xml w/ transparency info - bool or int? or just solid/nonsolid?
  * use this transparency (or something else? e.g. spawnable flag?) to determine which block is the top block for purposes of the light map
  * go and look at wiki to see the type of info that is stored per block

  * produce another light map that shows areas that ARE spawnable? (e.g. use this to make an area 100% mob spawn proof)

  * convert all printf-style stuff to streams

  * adapt for MCPC?

  * find better hsl/hsv to/from rgb funcs

  todo

  ** cmdline options:
  save set of slices
  save a particular slice
  draw text on slice files (e.g. Y)
  separate logfiles for overworld + nether + unknown
  options to reduce log file detail

  ** maps/instructions to get from point A (e.g. spawn) to biome type X (in blocks but also in landmark form: over 2 seas left at the birch forest etc); same for items and entities

  todo win32/win64 build

  * immediate crash if using -O2? (and now -O1 and -O)
  * leveldb close() issue -- link fails on missing stream stuff
  * leveldb -O2 build issue -- link fails on missing stream stuff
  * leveldb fread_nolock issue -- link fails; forcing msvcrXXX.a crashes on windows
  * log file: change end line to CRLF?

  ** osx build? cross compile tools look kinda horrible; see https://www.macports.org/

  */

// define this to use memcpy instead of manual copy of individual pixel values
// memcpy appears to be approx 1.3% faster for another1 --html-all
#define PIXEL_COPY_MEMCPY

#include <stdio.h>
#include <map>
#include <vector>
#include <algorithm>
#include <getopt.h>

#include "leveldb/db.h"
// hide innocuous warnings here
#pragma GCC diagnostic ignored "-Wshadow"
#include "leveldb/zlib_compressor.h"
#pragma GCC diagnostic pop

#include "mcpe_viz.util.h"
#include "mcpe_viz.h"
#include "mcpe_viz.nbt.h"
#include "mcpe_viz.xml.h"


namespace mcpe_viz {
  // todobig - removed anonymous namespace here


  std::string dirExec;
  
  Logger logger;

  int32_t playerPositionImageX=0, playerPositionImageY=0;
  std::string globalLevelName = "(Unknown)";

  // list of geojson items
  std::vector<std::string> listGeoJSON;

  // these are read from level.dat
  int32_t worldSpawnX = 0;
  int32_t worldSpawnY = 0;
  int32_t worldSpawnZ = 0;
  int64_t worldSeed = 0;
  
  // palettes
  int32_t palRedBlackGreen[256];

  // info lists (from XML)
  BlockInfo blockInfoList[256];
  ItemInfoList itemInfoList;
  EntityInfoList entityInfoList;
  BiomeInfoList biomeInfoList;
  EnchantmentInfoList enchantmentInfoList;
    


  enum OutputType : int32_t {
    kDoOutputNone = -2,
      kDoOutputAll = -1
      };

  enum HeightMode : int32_t {
    kHeightModeTop = 0,
      kHeightModeLevelDB = 1
      };

  // output image types
  enum ImageModeType : int32_t {
    kImageModeTerrain = 0,
      kImageModeBiome = 1,
      kImageModeGrass = 2,
      kImageModeHeightCol = 3,
      kImageModeHeightColGrayscale = 4,
      kImageModeBlockLight = 5,
      kImageModeSkyLight = 6,
      kImageModeSlimeChunks = 7
      };


    
  // all user options are stored here
  class Control {
  public:
    std::string dirLeveldb;
    std::string fnOutputBase;
    std::string fnCfg;
    std::string fnXml;
    std::string fnLog;
    std::string fnGeoJSON;
    std::string fnHtml;
    std::string fnJs;
      
    // per-dimension filenames
    std::string fnLayerTop[kDimIdCount];
    std::string fnLayerBiome[kDimIdCount];
    std::string fnLayerHeight[kDimIdCount];
    std::string fnLayerHeightGrayscale[kDimIdCount];
    std::string fnLayerBlockLight[kDimIdCount];
    std::string fnLayerSkyLight[kDimIdCount];
    std::string fnLayerSlimeChunks[kDimIdCount];
    std::string fnLayerGrass[kDimIdCount];
    std::string fnLayerRaw[kDimIdCount][128];
      
    bool doDetailParseFlag;
    int doMovie;
    int doSlices;
    int doGrid;
    int doHtml;
    int doTiles;
    int doImageBiome;
    int doImageGrass;
    int doImageHeightCol;
    int doImageHeightColGrayscale;
    int doImageLightBlock;
    int doImageLightSky;
    int doImageSlimeChunks;
    bool noForceGeoJSONFlag;
    bool shortRunFlag;
    bool colorTestFlag;
    bool verboseFlag;
    bool quietFlag;
    int movieX, movieY, movieW, movieH;

    int heightMode;

    // todobig - param?
    int tileWidth = 2048;
    int tileHeight = 512;
    
    bool fpLogNeedCloseFlag;
    FILE *fpLog;

    Control() {
      init();
    }
    ~Control() {
      if ( fpLogNeedCloseFlag ) {
	if ( fpLog != nullptr ) {
	  fclose(fpLog);
	}
      }
    }
  
    void init() {
      dirLeveldb = "";
      fnXml = "";
      fnOutputBase = "";
      fnLog = "";
      fnGeoJSON = "";
      fnHtml = "";
      fnJs = "";
      doDetailParseFlag = false;

      doMovie = kDoOutputNone;
      doSlices = kDoOutputNone;
      doGrid = kDoOutputNone;
      doHtml = 0;
      doTiles = 0;
      doImageBiome = kDoOutputNone;
      doImageGrass = kDoOutputNone;
      doImageHeightCol = kDoOutputNone;
      doImageHeightColGrayscale = kDoOutputNone;
      doImageLightBlock = kDoOutputNone;
      doImageLightSky = kDoOutputNone;
      doImageSlimeChunks = kDoOutputNone;
      noForceGeoJSONFlag = false;
	
      shortRunFlag = false;
      colorTestFlag = false;
      verboseFlag = false;
      quietFlag = false;
      movieX = movieY = movieW = movieH = 0;
      fpLogNeedCloseFlag = false;
      fpLog = stdout;

      // todo - cmdline option for this?
      heightMode = kHeightModeTop;
	
      for (int did=0; did < kDimIdCount; did++) {
	fnLayerTop[did] = "";
	fnLayerBiome[did] = "";
	fnLayerHeight[did] = "";
	fnLayerHeightGrayscale[did] = "";
	fnLayerBlockLight[did] = "";
	fnLayerSkyLight[did] = "";
	fnLayerSlimeChunks[did] = "";
	fnLayerGrass[did] = "";
	for ( int i=0; i < 128; i++ ) {
	  fnLayerRaw[did][i] = "";
	}
      }
    }

    void setupOutput() {
      if ( fnLog.compare("-") == 0 ) {
	fpLog = stdout;
	fpLogNeedCloseFlag = false;
      }
      else {
	if ( fnLog.size() == 0 ) {
	  fnLog = fnOutputBase + ".log";
	}
	fpLog = fopen(fnLog.c_str(), "w");
	if ( fpLog ) {
	  fpLogNeedCloseFlag = true;
	} else {
	  fprintf(stderr,"ERROR: Failed to create output log file (%s).  Reverting to stdout...\n", fnLog.c_str());
	  fpLog = stdout;
	  fpLogNeedCloseFlag = false;
	}
      }

      // setup logger
      logger.setStdout(fpLog);
      logger.setStderr(stderr);
	
	
      if ( doHtml ) {
	fnGeoJSON = fnOutputBase + ".geojson";
	  
	listGeoJSON.clear();

	fnHtml = fnOutputBase + ".html";
	fnJs = fnOutputBase + ".js";
      }
    }
      
  };

  Control control;


  void makePalettes() {
    // create red-green ramp; red to black and then black to green
    makeHslRamp(palRedBlackGreen,  0,  61, 0.0,0.0, 0.9,0.9, 0.8,0.1);
    makeHslRamp(palRedBlackGreen, 63, 127, 0.4,0.4, 0.9,0.9, 0.1,0.8);
    // force 62 (sea level) to gray
    palRedBlackGreen[62]=0x303030;
    // fill 128..255 with purple (we should never see this color)
    for (int i=128; i < 256; i++) {
      palRedBlackGreen[i] = kColorDefault;
    }

    // convert palette
    for (int i=0; i < 256; i++) {
      palRedBlackGreen[i] = htobe32(palRedBlackGreen[i]);
    }
  }

  // calculate an offset into mcpe chunk data for block data
  inline int _calcOffsetBlock(int x, int z, int y) {
    return (((x*16) + z)*128) + y;
  }

  // calculate an offset into mcpe chunk data for column data
  inline int _calcOffsetColumn(int x, int z) {
    // NOTE! this is the OPPOSITE of block data (oy)
    return (z*16) + x;
  }

  inline uint8_t getBlockId(const char* p, int x, int z, int y) {
    return (p[_calcOffsetBlock(x,z,y)] & 0xff);
  }

  uint8_t getBlockData(const char* p, int x, int z, int y) {
    int off =  _calcOffsetBlock(x,z,y);
    int off2 = off / 2;
    int mod2 = off % 2;
    int v = p[32768 + off2];
    if ( mod2 == 0 ) {
      return v & 0x0f;
    } else {
      return (v & 0xf0) >> 4;
    }
  }

  // a block opacity value? (e.g. glass is 0xf, water is semi (0xc) and an opaque block is 0x0)
  uint8_t getBlockSkyLight(const char* p, int x, int z, int y) {
    int off =  _calcOffsetBlock(x,z,y);
    int off2 = off / 2;
    int mod2 = off % 2;
    int v = p[32768 + 16384 + off2];
    if ( mod2 == 0 ) {
      return v & 0x0f;
    } else {
      return (v & 0xf0) >> 4;
    }
  }

  // block light is light value from torches et al -- super cool looking as an image, but it looks like block light is probably stored in air blocks which are above top block
  uint8_t getBlockBlockLight(const char* p, int x, int z, int y) {
    int off =  _calcOffsetBlock(x,z,y);
    int off2 = off / 2;
    int mod2 = off % 2;
    int v = p[32768 + 16384 + 16384 + off2];
    if ( mod2 == 0 ) {
      return v & 0x0f;
    } else {
      return (v & 0xf0) >> 4;
    }
  }

  // height of top *solid* block? (e.g. a glass block will NOT be the top block here)
  uint8_t getColData_Height(const char *buf, int x, int z) {
    int off = _calcOffsetColumn(x,z);
    int8_t v = buf[32768 + 16384 + 16384 + 16384 + off];
    return v;
  }

  // this is 4-bytes: lsb is biome, the high 3-bytes are RGB grass color
  uint32_t getColData_GrassAndBiome(const char *buf, int x, int z) {
    int off = _calcOffsetColumn(x,z) * 4;
    int32_t v;
    memcpy(&v,&buf[32768 + 16384 + 16384 + 16384 + 256 + off],4);
    return v;
  }
    
    

  int32_t myParseInt32(const char* p, int startByte) {
    int32_t ret;
    memcpy(&ret, &p[startByte], 4);
    return ret;
  }

  int8_t myParseInt8(const char* p, int startByte) {
    return (p[startByte] & 0xff);
  }

    
  bool has_key(const ItemInfoList &m, int k) {
    return  m.find(k) != m.end();
  }
  bool has_key(const EntityInfoList &m, int k) {
    return  m.find(k) != m.end();
  }
  bool has_key(const BiomeInfoList &m, int k) {
    return  m.find(k) != m.end();
  }
  bool has_key(const EnchantmentInfoList &m, int k) {
    return  m.find(k) != m.end();
  }

  
  std::string getBiomeName(int idv) {
    if ( has_key(biomeInfoList, idv) ) {
      return biomeInfoList[idv]->name;
    }
    char s[256];
    sprintf(s,"ERROR: Failed to find biome id (%d)",idv);
    return std::string(s);
  }

    
    
  class ChunkData {
  public:
    int32_t chunkX, chunkZ;
    uint8_t blocks[16][16];
    uint8_t data[16][16];
    uint32_t grassAndBiome[16][16];
    uint8_t topBlockY[16][16];
    uint8_t heightCol[16][16];
    uint8_t topLight[16][16];

    // we parse the block (et al) data in a chunk from leveldb
    ChunkData(int32_t tchunkX, int32_t tchunkZ, const char* value,
	      const std::string dimName, int* histogramGlobalBiome,
	      const bool* fastBlockHideList, const bool* fastBlockForceTopList ) {
      chunkX = tchunkX;
      chunkZ = tchunkZ;

      int16_t histogramBlock[256];
      int16_t histogramBiome[256];
      memset(histogramBlock,0,256*sizeof(int16_t));
      memset(histogramBiome,0,256*sizeof(int16_t));

      // clear the data we track
      memset(blocks,0, 16*16*sizeof(uint8_t));

      // todobig - clears are redundant?
      memset(data,0, 16*16*sizeof(uint8_t));
      //memset(grassAndBiome, 0, 16*16*sizeof(uint32_t));
      memset(topBlockY,0, 16*16*sizeof(uint8_t));
      //memset(heightCol,0, 16*16*sizeof(uint8_t));
      memset(topLight,0, 16*16*sizeof(uint8_t));
      
      // iterate over chunk space
      uint8_t blockId, biomeId;
      for (int cy=127; cy >= 0; cy--) {
	for ( int cx=0; cx < 16; cx++) {
	  for ( int cz=0; cz < 16; cz++ ) {
	    blockId = getBlockId(value, cx,cz,cy);
	    histogramBlock[blockId]++;
	    
	    // todo - check for isSolid?

	    if ( blockId != 0 ) {  // current block is NOT air
	      if ( ( blocks[cx][cz] == 0 &&  // top block is not already set
		     !fastBlockHideList[blockId] ) ||
		   fastBlockForceTopList[blockId] ) {
		
		blocks[cx][cz] = blockId;
		data[cx][cz] = getBlockData(value, cx,cz,cy);
		topBlockY[cx][cz] = cy;
		
#if 1
		// todo - we are getting the block light ABOVE this block (correct?)
		// todo - this will break if we are using force-top stuff
		int cy2 = cy;
		if ( blockInfoList[blockId].isSolid() ) {
		  // move to block above this block
		  cy2++;
		  if ( cy2 > 127 ) { cy2 = 127; }
		} else {
		  // if not solid, don't adjust
		}
		uint8_t sl = getBlockSkyLight(value, cx,cz,cy2);
		uint8_t bl = getBlockBlockLight(value, cx,cz,cy2);	
		// we combine the light nibbles into a byte
		topLight[cx][cz] = (sl << 4) | bl;
#endif
	      }
	    }
	  }
	}
      }
      
      // get per-column data
      for (int cx=0; cx < 16; cx++) {
	for (int cz=0; cz < 16; cz++) {
	  heightCol[cx][cz] = getColData_Height(value, cx,cz);
	  grassAndBiome[cx][cz] = getColData_GrassAndBiome(value, cx,cz);
	  
	  biomeId = (uint8_t)(grassAndBiome[cx][cz] & 0xFF);
	  histogramBiome[biomeId]++;
	  histogramGlobalBiome[biomeId]++;
	  
#if 0
	  // todo - testing idea about lighting - get lighting from top solid block - result is part good, part crazy
	  int ty = heightCol[cx][cz] + 1;
	  if ( ty > 127 ) { ty=127; }
	  uint8_t sl = getBlockSkyLight(value, cx,cz,ty);
	  uint8_t bl = getBlockBlockLight(value, cx,cz,ty);
	  topLight[cx][cz] = (sl << 4) | bl;
#endif
	}
      }

      if ( control.quietFlag ) {
	return;
      }
	
      // print chunk info
      logger.msg(kLogInfo1,"Top Blocks (block-id:block-data:biome-id):\n");
      // note the different use of cx/cz here
      uint32_t rawData;
      for (int cz=0; cz<16; cz++) {
	for (int cx=0; cx<16; cx++) {
	  rawData = grassAndBiome[cx][cz];
	  biomeId = (uint8_t)(rawData & 0xFF);
	  logger.msg(kLogInfo1,"%02x:%x:%02x ", (int)blocks[cx][cz], (int)data[cx][cz], (int)biomeId);
	}
	logger.msg(kLogInfo1,"\n");
      }
      logger.msg(kLogInfo1,"Block Histogram:\n");
      for (int i=0; i < 256; i++) {
	if ( histogramBlock[i] > 0 ) {
	  logger.msg(kLogInfo1,"%s-hg: %02x: %6d (%s)\n", dimName.c_str(), i, histogramBlock[i], blockInfoList[i].name.c_str());
	}
      }
      logger.msg(kLogInfo1,"Biome Histogram:\n");
      for (int i=0; i < 256; i++) {
	if ( histogramBiome[i] > 0 ) {
	  std::string biomeName( getBiomeName(i) );
	  logger.msg(kLogInfo1,"%s-hg-biome: %02x: %6d (%s)\n", dimName.c_str(), i, histogramBiome[i], biomeName.c_str());
	}
      }
      logger.msg(kLogInfo1,"Block Light (skylight:blocklight:heightcol):\n");
      for (int cz=0; cz<16; cz++) {
	for (int cx=0; cx<16; cx++) {
	  logger.msg(kLogInfo1,"%x:%x:%02x ", (int)((topLight[cx][cz] >> 4) & 0xf), (int)(topLight[cx][cz] & 0xf), (int)heightCol[cx][cz]);
	}
	logger.msg(kLogInfo1,"\n");
      }
    }
  };



  class ChunkDataList {
  public:
    std::string name;
    int32_t dimId;
    std::vector< std::unique_ptr<ChunkData> > list;
    int32_t minChunkX = 0, maxChunkX = 0;
    int32_t minChunkZ = 0, maxChunkZ = 0;
    bool chunkBoundsValid;

    int32_t histogramChunkType[256];
    int32_t histogramGlobalBiome[256];

    bool fastBlockHideList[256];
    bool fastBlockForceTopList[256];

    std::vector<int> blockForceTopList;
    std::vector<int> blockHideList;
      
    ChunkDataList() {
      name = "(UNKNOWN)";
      dimId = -1;
      chunkBoundsValid = false;
      memset(histogramChunkType,0,256*sizeof(int32_t));
      memset(histogramGlobalBiome,0,256*sizeof(int32_t));
    }

    void updateFastLists() {
      for (int bid=0; bid < 256; bid++) {
	fastBlockHideList[bid] = vectorContains(blockHideList, bid);
	fastBlockForceTopList[bid] = vectorContains(blockForceTopList, bid);
      }
    }

    void setName(std::string s) {
      name = s;
    }
      
    void clearChunkBounds() {
      minChunkX = minChunkZ = maxChunkX = maxChunkZ = 0;
      chunkBoundsValid = false;
    }

    void setChunkBoundsValid() {
      chunkBoundsValid = true;
    }
      
    void addToChunkBounds(int32_t chunkX, int32_t chunkZ) {
      minChunkX = std::min(minChunkX, chunkX);
      maxChunkX = std::max(maxChunkX, chunkX);
      minChunkZ = std::min(minChunkZ, chunkZ);
      maxChunkZ = std::max(maxChunkZ, chunkZ);
    }

    int addChunk ( int32_t chunkX, int32_t chunkZ, const char* value) {
      // todobig emplace_back? does this do a copy?
      list.push_back( std::unique_ptr<ChunkData>
		      (new ChunkData(chunkX, chunkZ, value, name,
				     histogramGlobalBiome, fastBlockHideList, fastBlockForceTopList)) );
      return 0;
    }
      
    bool checkDoForDim(int v) {
      if ( v == kDoOutputAll ) {
	return true;
      }
      if ( v == dimId ) {
	return true;
      }
      return false;
    }
      
    
    void doOutputStats() {
      logger.msg(kLogInfo1,"\n%s Statistics:\n", name.c_str());
      logger.msg(kLogInfo1,"chunk-count: %d\n", (int)list.size());
      logger.msg(kLogInfo1,"Min-dim:  %d %d\n", minChunkX, minChunkZ);
      logger.msg(kLogInfo1,"Max-dim:  %d %d\n", maxChunkX, maxChunkZ);
      int32_t dx = (maxChunkX-minChunkX+1);
      int32_t dz = (maxChunkZ-minChunkZ+1);
      logger.msg(kLogInfo1,"diff-dim: %d %d\n", dx, dz);
      logger.msg(kLogInfo1,"pixels:   %d %d\n", dx*16, dz*16);

      logger.msg(kLogInfo1,"\nGlobal Chunk Type Histogram:\n");
      for (int i=0; i < 256; i++) {
	if ( histogramChunkType[i] > 0 ) {
	  logger.msg(kLogInfo1,"hg-chunktype: %02x %6d\n", i, histogramChunkType[i]);
	}
      }

      logger.msg(kLogInfo1,"\nGlobal Biome Histogram:\n");
      for (int i=0; i < 256; i++) {
	if ( histogramGlobalBiome[i] > 0 ) {
	  logger.msg(kLogInfo1,"hg-globalbiome: %02x %6d\n", i, histogramGlobalBiome[i]);
	}
      }
    }

    std::string makeImageDescription(int imageMode, int layerNumber) {
      std::string ret = "MCPE Viz Image -- World=(" + globalLevelName +")";
      ret += " Dimension=(" + name + ")";
      ret += " Image=(";
      switch ( imageMode ) {
      case kImageModeTerrain:
	ret += "Overview Map";
	break;
      case kImageModeBiome:
	ret += "Biome Map";
	break;
      case kImageModeGrass:
	ret += "Grass Color Map";
	break;
      case kImageModeHeightCol:
	ret += "Top Block Height Map";
	break;
      case kImageModeHeightColGrayscale:
	ret += "Top Block Height Map (grayscale)";
	break;
      case kImageModeBlockLight:
	ret += "Top Block Light Map";
	break;
      case kImageModeSkyLight:
	ret += "Top Block Sky Light Map";
	break;
      case kImageModeSlimeChunks:
	ret += "Slime Chunks Map";
	break;
      case -1:
	// special marker for raw layers
	{
	  char tmpstring[256];
	  sprintf(tmpstring,"Raw Layer %d", layerNumber);
	  ret += tmpstring;
	}
	break;
      default:
	{
	  char tmpstring[256];
	  sprintf(tmpstring,"UNKNOWN: ImageMode=%d", imageMode);
	  ret += tmpstring;
	}
	break;
      }
      ret += ")";
      return ret;
    }

    int outputPNG(const std::string fname, const std::string imageDescription, uint8_t* buf, int width, int height) {
      PngWriter png;
      if ( png.init(fname, imageDescription, width, height, height) != 0 ) {
	return -1;
      }
      for(int y = 0; y < height; y++) {
	png.row_pointers[y] = &buf[ y * width * 3 ];
      }
      png_write_image(png.png, png.row_pointers);
      png.close();
      return 0;
    }
    
    void generateImage(const std::string fname, const ImageModeType imageMode = kImageModeTerrain) {
      const int32_t chunkOffsetX = -minChunkX;
      const int32_t chunkOffsetZ = -minChunkZ;
	
      const int32_t chunkW = (maxChunkX-minChunkX+1);
      const int32_t chunkH = (maxChunkZ-minChunkZ+1);
      const int32_t imageW = chunkW * 16;
      const int32_t imageH = chunkH * 16;

      JavaRandom rnd;
      int32_t slimeChunkX = 0, slimeChunkZ = 0;
      bool slimeChunkFlag = false, slimeChunkInit = false;
      
      // note RGB pixels
      uint8_t *buf = new uint8_t[ imageW * imageH * 3 ];
      memset(buf, 0, imageW*imageH*3);

      int32_t color;
      const char *pcolor = (const char*)&color;
      for ( const auto& it: list ) {
	int32_t imageX = (it->chunkX + chunkOffsetX) * 16;
	int32_t imageZ = (it->chunkZ + chunkOffsetZ) * 16;

	int32_t worldX = it->chunkX * 16;
	int32_t worldZ = it->chunkZ * 16;
	  
	for (int cz=0; cz < 16; cz++) {
	  for (int cx=0; cx < 16; cx++) {

	    // todobig - we could do EVERYTHING (but initial key scan) in one pass:
	    //   do images here, then iterate over chunkspace again looking for items that populate geojson list
	      
	    // todo - this big conditional inside an inner loop, not so good

	    if ( imageMode == kImageModeBiome ) {
	      // get biome color
	      int biomeId = it->grassAndBiome[cx][cz] & 0xff;
	      if ( has_key(biomeInfoList, biomeId) ) {
		color = biomeInfoList[biomeId]->color;
	      } else {
		fprintf(stderr,"ERROR: Unknown biome %d 0x%x\n", biomeId, biomeId);
		color = htobe32(0xff2020);
	      }
	    }
	    else if ( imageMode == kImageModeGrass ) {
	      // get grass color
	      int32_t grassColor = it->grassAndBiome[cx][cz] >> 8;
	      color = htobe32(grassColor);
	    }
	    else if ( imageMode == kImageModeHeightCol ) {
	      // get height value and use red-black-green palette
	      if ( control.heightMode == kHeightModeTop ) {
		uint8_t c = it->topBlockY[cx][cz];
		color = palRedBlackGreen[c];
	      } else {
		uint8_t c = it->heightCol[cx][cz];
		color = palRedBlackGreen[c];
	      }
	    }
	    else if ( imageMode == kImageModeHeightColGrayscale ) {
	      // get height value and make it grayscale
	      if ( control.heightMode == kHeightModeTop ) {
		uint8_t c = it->topBlockY[cx][cz];
		color = (c << 24) | (c << 16) | (c << 8);
	      } else {
		uint8_t c = it->heightCol[cx][cz];
		color = (c << 24) | (c << 16) | (c << 8);
	      }
	    }
	    else if ( imageMode == kImageModeBlockLight ) {
	      // get block light value and expand it (is only 4-bits)
	      uint8_t c = (it->topLight[cx][cz] & 0x0f) << 4;
	      color = (c << 24) | (c << 16) | (c << 8);
	    }
	    else if ( imageMode == kImageModeSkyLight ) {
	      // get sky light value and expand it (is only 4-bits)
	      uint8_t c = (it->topLight[cx][cz] & 0xf0);
	      color = (c << 24) | (c << 16) | (c << 8);
	    }
	    else if ( imageMode == kImageModeSlimeChunks ) {
	      if ( slimeChunkInit && slimeChunkX == it->chunkX && slimeChunkZ == it->chunkZ ) {
		// we already have our flag
	      } else {
		/*
		Random rnd = new Random(seed +
					(long) (xPosition * xPosition * 0x4c1906) +
					(long) (xPosition * 0x5ac0db) +
					(long) (zPosition * zPosition) * 0x4307a7L +
					(long) (zPosition * 0x5f24f) ^ 0x3ad8025f);
		return rnd.nextInt(10) == 0;
		*/
		slimeChunkInit = true;
		slimeChunkX = it->chunkX;
		slimeChunkZ = it->chunkZ;
		int64_t seed =
		  ( worldSeed +
		    (int64_t) (slimeChunkX * slimeChunkX * (int64_t)0x4c1906) +
		    (int64_t) (slimeChunkX * (int64_t)0x5ac0db) +
		    (int64_t) (slimeChunkZ * slimeChunkZ * (int64_t)0x4307a7) +
		    (int64_t) (slimeChunkZ * (int64_t)0x5f24f)
		    )
		  ^ 0x3ad8025f
		  ;
	      
		rnd.setSeed(seed);
		slimeChunkFlag = (rnd.nextInt(10) == 0);
	      }
	      if ( slimeChunkFlag ) {
		color = (0xff << 16);
	      } else {
		color = 0;
	      }
	    }
	    else {
	      // regular image
	      int blockid = it->blocks[cx][cz];
		
	      if ( blockInfoList[blockid].hasVariants() ) {
		// we need to get blockdata
		int blockdata = it->data[cx][cz];
		bool vfound = false;
		for (const auto& itbv : blockInfoList[blockid].variantList) {
		  if ( itbv->blockdata == blockdata ) {
		    vfound = true;
		    color = itbv->color;
		    break;
		  }
		}
		if ( ! vfound ) {
		  // todo - warn once per id/blockdata or the output volume could get ridiculous
		  fprintf(stderr,"WARNING: Did not find block variant for block(%s) with blockdata=%d (0x%x)\n"
			  , blockInfoList[blockid].name.c_str()
			  , blockdata
			  , blockdata
			  );
		}
	      } else {
		color = blockInfoList[blockid].color;
		if ( ! blockInfoList[blockid].colorSetFlag ) {
		  blockInfoList[blockid].colorSetNeedCount++;
		}
	      }
	    }

	    // do grid lines
	    if ( checkDoForDim(control.doGrid) && (cx==0 || cz==0) ) {
	      if ( (it->chunkX == 0) && (it->chunkZ == 0) && (cx == 0) && (cz == 0) ) {
		color = htobe32(0xeb3333);
	      } else {
		color = htobe32(0xc1ffc4);
	      }
	    }

#ifdef PIXEL_COPY_MEMCPY
	    memcpy(&buf[ ((imageZ + cz) * imageW + (imageX + cx)) * 3], &pcolor[1], 3);
#else
	    // todo - any use in optimizing the offset calc?
	    buf[((imageZ + cz) * imageW + (imageX + cx)) * 3] = pcolor[1];
	    buf[((imageZ + cz) * imageW + (imageX + cx)) * 3 + 1] = pcolor[2];
	    buf[((imageZ + cz) * imageW + (imageX + cx)) * 3 + 2] = pcolor[3];
#endif

	    // report interesting coordinates
	    if ( dimId == kDimIdOverworld && imageMode == kImageModeTerrain ) {
	      int ix = (imageX + cx);
	      int iz = (imageZ + cz);
	      int wx = (worldX + cx);
	      int wz = (worldZ + cz);
	      if ( (wx == 0) && (wz == 0) ) {
		fprintf(stderr,"    Info: World (0, 0) is at image (%d, %d)\n", ix,iz);
	      }
	      if ( (wx == worldSpawnX) && (wz == worldSpawnZ) ) {
		fprintf(stderr,"    Info: World Spawn (%d, %d) is at image (%d, %d)\n", worldSpawnX, worldSpawnZ, ix, iz);
	      }
	    }
	  }
	}
      }
	
      // output the image
      outputPNG(fname, makeImageDescription(imageMode,0), buf, imageW, imageH);

      delete [] buf;

      // report items that need to have their color set properly (in the XML file)
      if ( imageMode == kImageModeTerrain ) {
	for (int i=0; i < 256; i++) {
	  if ( blockInfoList[i].colorSetNeedCount ) {
	    fprintf(stderr,"    Need pixel color for: 0x%x '%s' (%d)\n", i, blockInfoList[i].name.c_str(), blockInfoList[i].colorSetNeedCount);
	  }
	}
      }
    }


    // a run on old code (generateMovie):
    // > time ./mcpe_viz --db another1/ --out test-all1/out1 --log log.tall1 --html-all
    // 2672.248u 5.461s 45:47.69 97.4% 0+0k 72+1424304io 0pf+0w
    // approx 45 minutes

    // a run on this new code (generateSlices):
    // 957.146u 4.463s 16:30.58 97.0%  0+0k 2536+1424304io 1pf+0w
    // approx 17 minutes

    // 823.212u 13.623s 14:23.86 96.8% 0+0k 2536+1423088io 1pf+0w
    // 830.614u 13.871s 14:52.42 94.6% 0+0k 2536+1423088io 1pf+0w

    // change memcpy to byte copy:
    // 828.757u 13.842s 14:29.31 96.9% 0+0k 2536+1422656io 1pf+0w

    // with -O3
    // 827.337u 13.430s 14:27.58 96.9% 0+0k 2568+1422656io 1pf+0w

    // with -O3 and png zlib set to 1 (default is 9) and filter set to NONE
    //   with zlib=9 == 405M    test-all2
    //   with zlib=1 == 508M    test-all2-zlib1/
    // 392.597u 13.598s 7:07.25 95.0%  0+0k 2568+1633896io 1pf+0w

    // as above, but disabled setting filter to NONE
    //   with zlib=1 == 572M    test-all2-zlib1/
    // 660.763u 13.799s 11:40.99 96.2% 0+0k 2568+1764624io 1pf+0w

    // 402.214u 13.937s 7:26.63 93.1%  0+0k 2568+1633896io 1pf+0w

    // without -O3, with zlib=1 and filter NONE
    // 404.518u 13.369s 7:25.09 93.8%  0+0k 2536+1633896io 1pf+0w

    // so we're at ~6.5x faster now

    // 2015.10.24:
    // 372.432u 13.435s 6:50.66 93.9%  0+0k 419456+1842944io 210pf+0w
    
    int generateSlices(leveldb::DB* db) {
      const int32_t chunkOffsetX = -minChunkX;
      const int32_t chunkOffsetZ = -minChunkZ;

      const int chunkW = (maxChunkX-minChunkX+1);
      const int chunkH = (maxChunkZ-minChunkZ+1);
      const int imageW = chunkW * 16;
      const int imageH = chunkH * 16;

      char keybuf[128];
      int keybuflen;
      int32_t kw = dimId;
      uint8_t kt = 0x30;
      leveldb::Status dstatus;
	
      fprintf(stderr,"    Writing all 128 images in one pass\n");
	  
      leveldb::ReadOptions readOptions;
      readOptions.fill_cache=false; // may improve performance?

      std::string svalue;
      const char* ochunk = nullptr;
      const char* pchunk = nullptr;
	
      int32_t color;
      const char *pcolor = (const char*)&color;

      // create png helpers
      PngWriter png[128];
      for (int cy=0; cy<128; cy++) {
	std::string fnameTmp = control.fnOutputBase + ".mcpe_viz_slice.full.";
	fnameTmp += name;
	fnameTmp += ".";
	sprintf(keybuf,"%03d",cy);
	fnameTmp += keybuf;
	fnameTmp += ".png";

	control.fnLayerRaw[dimId][cy] = fnameTmp;
	  
	png[cy].init(fnameTmp, makeImageDescription(-1,cy), imageW, imageH, 16);
      }
	
      // create row buffers
      uint8_t* rbuf[128];
      for (int cy=0; cy<128; cy++) {
	rbuf[cy] = new uint8_t[(imageW*3)*16];
	// setup row pointers
	for (int cz=0; cz<16; cz++) {
	  png[cy].row_pointers[cz] = &rbuf[cy][(cz*imageW)*3];
	}
      }

      // create a helper buffer which contains topBlockY for the entire image
      uint8_t currTopBlockY = 127;
      uint8_t* tbuf = new uint8_t[imageW * imageH];
      memset(tbuf,127,imageW*imageH);
      for (const auto& it : list) {
	int32_t ix = (it->chunkX + chunkOffsetX) * 16;
	int32_t iz = (it->chunkZ + chunkOffsetZ) * 16;
	for (int cz=0; cz < 16; cz++) {
	  for (int cx=0; cx < 16; cx++) {
	    tbuf[(iz+cz)*imageW + (ix+cx)] = it->topBlockY[cx][cz];
	  }
	}
      };
	
      int foundCt = 0, notFoundCt2 = 0;
      uint8_t blockid, blockdata;
	  
      // we operate on sets of 16 rows (which is one chunk high) of image z
      int runCt = 0;
      for (int32_t imageZ=0, chunkZ=minChunkZ; imageZ < imageH; imageZ += 16, chunkZ++) {

	if ( (runCt++ % 20) == 0 ) {
	  fprintf(stderr,"    Row %d of %d\n", imageZ, imageH);
	}
	    
	for (int32_t imageX=0, chunkX=minChunkX; imageX < imageW; imageX += 16, chunkX++) {

	  // construct key to get the chunk
	  if ( dimId == kDimIdOverworld ) {
	    //overworld
	    memcpy(&keybuf[0],&chunkX,sizeof(int32_t));
	    memcpy(&keybuf[4],&chunkZ,sizeof(int32_t));
	    memcpy(&keybuf[8],&kt,sizeof(uint8_t));
	    keybuflen=9;
	  } else {
	    // nether (and probably any others that are added)
	    memcpy(&keybuf[0],&chunkX,sizeof(int32_t));
	    memcpy(&keybuf[4],&chunkZ,sizeof(int32_t));
	    memcpy(&keybuf[8],&kw,sizeof(int32_t));
	    memcpy(&keybuf[12],&kt,sizeof(uint8_t));
	    keybuflen=13;
	  }

	  dstatus = db->Get(readOptions, leveldb::Slice(keybuf,keybuflen), &svalue);
	  if ( ! dstatus.ok() ) {
	    notFoundCt2++;
	    // fprintf(stderr,"WARNING: Did not find chunk in leveldb x=%d z=%d status=%s\n", chunkX, chunkZ, dstatus.ToString().c_str());
	    // we need to clear this area
	    for (int cy=0; cy < 128; cy++) {
	      for (int cz=0; cz < 16; cz++) {
		memset(&rbuf[cy][((cz*imageW)+imageX)*3], 0, 16*3);
	      }
	    }
	    continue;
	  }

	  pchunk = svalue.data();
	  ochunk = pchunk;
	  foundCt++;
	      
	  // we step through the chunk in the natural order to speed things up
	  for (int cx=0; cx < 16; cx++) {
	    for (int cz=0; cz < 16; cz++) {
	      currTopBlockY = tbuf[(imageZ+cz)*imageW + imageX+cx];
	      for (int cy=0; cy < 128; cy++) {
		blockid = *(pchunk++);

		if ( blockid == 0 && (cy > currTopBlockY) && (dimId != kDimIdNether) ) {

		  // special handling for air -- keep existing value if we are above top block
		  // the idea is to show air underground, but hide it above so that the map is not all black pixels @ y=127
		  // however, we do NOT do this for the nether. because: the nether

		  // we need to copy this pixel from another layer
		  memcpy(&rbuf[ cy            ][((cz*imageW) + imageX + cx)*3],
			 &rbuf[ currTopBlockY ][((cz*imageW) + imageX + cx)*3],
			 3);
		      
		} else {
		    
		  if ( blockInfoList[blockid].hasVariants() ) {
		    // we need to get blockdata
		    blockdata = getBlockData(ochunk, cx,cz,cy);
		    bool vfound = false;
		    for (const auto& itbv : blockInfoList[blockid].variantList) {
		      if ( itbv->blockdata == blockdata ) {
			vfound = true;
			color = itbv->color;
			break;
		      }
		    }
		    if ( ! vfound ) {
		      // todo - warn once per id/blockdata or the output volume could get ridiculous
		      fprintf(stderr,"WARNING: Did not find block variant for block(%s) with blockdata=%d (0x%x)\n"
			      , blockInfoList[blockid].name.c_str()
			      , blockdata
			      , blockdata
			      );
		    }
		  } else {
		    color = blockInfoList[blockid].color;
		  }
		    
#ifdef PIXEL_COPY_MEMCPY
		  memcpy(&rbuf[cy][((cz*imageW) + imageX + cx)*3], &pcolor[1], 3);
#else
		  // todo - any use in optimizing the offset calc?
		  rbuf[cy][((cz*imageW) + imageX + cx)*3] = pcolor[1];
		  rbuf[cy][((cz*imageW) + imageX + cx)*3 + 1] = pcolor[2];
		  rbuf[cy][((cz*imageW) + imageX + cx)*3 + 2] = pcolor[3];
#endif
		}
	      }
	    }
	  }
	}
	  
	// put the png rows
	// todo - png lib is SLOW - worth it to alloc a larger window (16-row increments) and write in batches?
	for (int cy=0; cy<128; cy++) {
	  png_write_rows(png[cy].png, png[cy].row_pointers, 16);
	}
      }
	
      for (int cy=0; cy<128; cy++) {
	delete [] rbuf[cy];
	png[cy].close();
      }

      delete [] tbuf;
	
      // fprintf(stderr,"    Chunk Info: Found = %d / Not Found (our list) = %d / Not Found (leveldb) = %d\n", foundCt, notFoundCt1, notFoundCt2);
	
      return 0;
    }

      
    int generateMovie(leveldb::DB* db, const std::string fname, bool makeMovieFlag, bool useCropFlag ) {
      const int32_t chunkOffsetX = -minChunkX;
      const int32_t chunkOffsetZ = -minChunkZ;
	
      const int chunkW = (maxChunkX-minChunkX+1);
      const int chunkH = (maxChunkZ-minChunkZ+1);
      const int imageW = chunkW * 16;
      const int imageH = chunkH * 16;

      int divisor = 1;
      if ( dimId == kDimIdNether ) { 
	// if nether, we divide coordinates by 8
	divisor = 8; 
      }

      int cropX, cropZ, cropW, cropH;

      if ( useCropFlag ) {
	cropX = control.movieX / divisor;
	cropZ = control.movieY / divisor;
	cropW = control.movieW / divisor;
	cropH = control.movieH / divisor;
      } else {
	cropX = cropZ = 0;
	cropW = imageW;
	cropH = imageH;
      }
	
      // note RGB pixels
      uint8_t* buf = new uint8_t[ cropW * cropH * 3 ];
      memset(buf, 0, cropW*cropH*3);

      // todobig - we *could* write image data to flat files during parseDb and then convert 
      //   these flat files into png here (but temp disk space requirements are *huge*); could try gzwrite etc

      leveldb::ReadOptions readOptions;
      readOptions.fill_cache=false; // may improve performance?

      std::string svalue;
      const char* pchunk = nullptr;
      int32_t pchunkX = 0;
      int32_t pchunkZ = 0;
	
      int32_t color;
      const char *pcolor = (const char*)&color;
      for (int cy=0; cy < 128; cy++) {
	// todo - make this part a func so that user can ask for specific slices from the cmdline?
	fprintf(stderr,"  Layer %d\n", cy);
	for ( const auto& it : list ) {
	  int imageX = (it->chunkX + chunkOffsetX) * 16;
	  int imageZ = (it->chunkZ + chunkOffsetZ) * 16;

	  for (int cz=0; cz < 16; cz++) {
	    int iz = (imageZ + cz);

	    for (int cx=0; cx < 16; cx++) {
	      int ix = (imageX + cx);

	      if ( !useCropFlag || ((ix >= cropX) && (ix < (cropX + cropW)) && (iz >= cropZ) && (iz < (cropZ + cropH))) ) {

		if ( pchunk==nullptr || (pchunkX != it->chunkX) || (pchunkZ != it->chunkZ) ) {
		  // get the chunk
		  // construct key
		  char keybuf[20];
		  int keybuflen;
		  int32_t kx = it->chunkX, kz=it->chunkZ, kw=dimId;
		  uint8_t kt=0x30;
		  switch (dimId) {
		  case kDimIdOverworld:
		    //overworld
		    memcpy(&keybuf[0],&kx,sizeof(int32_t));
		    memcpy(&keybuf[4],&kz,sizeof(int32_t));
		    memcpy(&keybuf[8],&kt,sizeof(uint8_t));
		    keybuflen=9;
		    break;
		  default:
		    // nether
		    memcpy(&keybuf[0],&kx,sizeof(int32_t));
		    memcpy(&keybuf[4],&kz,sizeof(int32_t));
		    memcpy(&keybuf[8],&kw,sizeof(int32_t));
		    memcpy(&keybuf[12],&kt,sizeof(uint8_t));
		    keybuflen=13;
		    break;
		  }
		  leveldb::Slice key(keybuf,keybuflen);
		  leveldb::Status dstatus = db->Get(readOptions, key, &svalue);
		  if (!dstatus.ok()) {
		    fprintf(stderr,"WARNING: LevelDB operation returned status=%s\n",dstatus.ToString().c_str());
		  }
		  pchunk = svalue.data();
		  pchunkX = it->chunkX;
		  pchunkZ = it->chunkZ;
		}
		 
		uint8_t blockid = getBlockId(pchunk, cx,cz,cy);

		if ( blockid == 0 && ( cy > it->topBlockY[cx][cz] ) && (dimId != kDimIdNether) ) {
		  // special handling for air -- keep existing value if we are above top block
		  // the idea is to show air underground, but hide it above so that the map is not all black pixels @ y=127
		  // however, we do NOT do this for the nether. because: the nether
		} else {
		    
		  if ( blockInfoList[blockid].hasVariants() ) {
		    // we need to get blockdata
		    int blockdata = it->data[cx][cz];
		    bool vfound = false;
		    for (const auto& itbv : blockInfoList[blockid].variantList) {
		      if ( itbv->blockdata == blockdata ) {
			vfound = true;
			color = itbv->color;
			break;
		      }
		    }
		    if ( ! vfound ) {
		      // todo - warn once per id/blockdata or the output volume could get ridiculous
		      fprintf(stderr,"WARNING: Did not find block variant for block(%s) with blockdata=%d (0x%x)\n"
			      , blockInfoList[blockid].name.c_str()
			      , blockdata
			      , blockdata
			      );
		    }
		  } else {
		    color = blockInfoList[blockid].color;
		  }
		  
		  // do grid lines
		  if ( checkDoForDim(control.doGrid) && (cx==0 || cz==0) ) {
		    if ( (it->chunkX == 0) && (it->chunkZ == 0) && (cx == 0) && (cz == 0) ) {
		      // highlight (0,0)
		      color = htobe32(0xeb3333);
		    } else {
		      color = htobe32(0xc1ffc4);
		    }
		  }
		  
#ifdef PIXEL_COPY_MEMCPY
		  memcpy(&buf[ (((imageZ + cz) - cropZ) * cropW + ((imageX + cx) - cropX)) * 3], &pcolor[1], 3);
#else
		  // todo - any use in optimizing the offset calc?
		  buf[ (((imageZ + cz) - cropZ) * cropW + ((imageX + cx) - cropX)) * 3] = pcolor[1];
		  buf[ (((imageZ + cz) - cropZ) * cropW + ((imageX + cx) - cropX)) * 3 + 1] = pcolor[2];
		  buf[ (((imageZ + cz) - cropZ) * cropW + ((imageX + cx) - cropX)) * 3 + 2] = pcolor[3];
#endif
		}
	      }
	    }
	  }
	}

	// output the image
	std::string fnameTmp = control.fnOutputBase + ".mcpe_viz_slice.";
	if ( !makeMovieFlag) {
	  fnameTmp += "full.";
	}
	fnameTmp += name;
	fnameTmp += ".";
	char xtmp[100];
	sprintf(xtmp,"%03d",cy);
	fnameTmp += xtmp;
	fnameTmp += ".png";

	control.fnLayerRaw[dimId][cy] = fnameTmp;
	  
	outputPNG(fnameTmp, makeImageDescription(-1,cy), buf, cropW, cropH);
      }

      delete [] buf;

      if ( makeMovieFlag ) {
	// "ffmpeg" method
	std::string fnameTmp = control.fnOutputBase + ".mcpe_viz_slice.";	
	fnameTmp += name;
	fnameTmp += ".%03d.png";
	  
	// todo - ffmpeg on win32? need bin path option?
	// todo - provide other user options for ffmpeg cmd line params?
	std::string cmdline = std::string("ffmpeg -y -framerate 1 -i " + fnameTmp + " -c:v libx264 -r 30 ");
	cmdline += fname;
	int ret = system(cmdline.c_str());
	if ( ret != 0 ) {
	  fprintf(stderr,"Failed to create movie ret=(%d) cmd=(%s)\n",ret,cmdline.c_str());
	}
	
	// todo - delete temp slice files? cmdline option to NOT delete
      }

      return 0;
    }

      
    int doOutput(leveldb::DB* db) {
      fprintf(stderr,"Do Output: %s\n",name.c_str());
	
      doOutputStats();
	
      fprintf(stderr,"  Generate Image\n");
      control.fnLayerTop[dimId] = std::string(control.fnOutputBase + "." + name + ".map.png");
      generateImage(control.fnLayerTop[dimId]);
	
      if ( checkDoForDim(control.doImageBiome) ) {
	fprintf(stderr,"  Generate Biome Image\n");
	control.fnLayerBiome[dimId] = std::string(control.fnOutputBase + "." + name + ".biome.png");
	generateImage(control.fnLayerBiome[dimId], kImageModeBiome);
      }
      if ( checkDoForDim(control.doImageGrass) ) {
	fprintf(stderr,"  Generate Grass Image\n");
	control.fnLayerGrass[dimId] = std::string(control.fnOutputBase + "." + name + ".grass.png");
	generateImage(control.fnLayerGrass[dimId], kImageModeGrass);
      }
      if ( checkDoForDim(control.doImageHeightCol) ) {
	fprintf(stderr,"  Generate Height Column Image\n");
	control.fnLayerHeight[dimId] = std::string(control.fnOutputBase + "." + name + ".height_col.png");
	generateImage(control.fnLayerHeight[dimId], kImageModeHeightCol);
      }
      if ( checkDoForDim(control.doImageHeightColGrayscale) ) {
	fprintf(stderr,"  Generate Height Column (grayscale) Image\n");
	control.fnLayerHeightGrayscale[dimId] = std::string(control.fnOutputBase + "." + name + ".height_col_grayscale.png");
	generateImage(control.fnLayerHeightGrayscale[dimId], kImageModeHeightColGrayscale);
      }
      if ( checkDoForDim(control.doImageLightBlock) ) {
	fprintf(stderr,"  Generate Block Light Image\n");
	control.fnLayerBlockLight[dimId] = std::string(control.fnOutputBase + "." + name + ".light_block.png");
	generateImage(control.fnLayerBlockLight[dimId], kImageModeBlockLight);
      }
      if ( checkDoForDim(control.doImageLightSky) ) {
	fprintf(stderr,"  Generate Sky Light Image\n");
	control.fnLayerSkyLight[dimId] = std::string(control.fnOutputBase + "." + name + ".light_sky.png");
	generateImage(control.fnLayerSkyLight[dimId], kImageModeSkyLight);
      }
      if ( checkDoForDim(control.doImageSlimeChunks) ) {
	fprintf(stderr,"  Generate Slime Chunks Image\n");
	control.fnLayerSlimeChunks[dimId] = std::string(control.fnOutputBase + "." + name + ".slime_chunks.png");
	generateImage(control.fnLayerSlimeChunks[dimId], kImageModeSlimeChunks);
      }

      if ( checkDoForDim(control.doMovie) ) {
	fprintf(stderr,"  Generate movie\n");
	generateMovie(db, std::string(control.fnOutputBase + "." + name + ".mp4"), true, true);
      }

      if ( checkDoForDim(control.doSlices) ) {
	fprintf(stderr,"  Generate full-size slices\n");
	generateSlices(db);
      }
	
      // reset
      for (int i=0; i < 256; i++) {
	blockInfoList[i].colorSetNeedCount = 0;
      }

      return 0;
    }
  };

  
    
  int printKeyValue(const char* key, int key_size, const char* value, int value_size, bool printKeyAsStringFlag) {
    logger.msg(kLogInfo1,"WARNING: Unknown Record: key_size=%d key_string=[%s] key_hex=[", key_size, 
	       (printKeyAsStringFlag ? key : "(SKIPPED)"));
    for (int i=0; i < key_size; i++) {
      if ( i > 0 ) { logger.msg(kLogInfo1," "); }
      logger.msg(kLogInfo1,"%02x",((int)key[i] & 0xff));
    }
    logger.msg(kLogInfo1,"] value_size=%d value_hex=[",value_size);
    for (int i=0; i < value_size; i++) {
      if ( i > 0 ) { logger.msg(kLogInfo1," "); }
      logger.msg(kLogInfo1,"%02x",((int)value[i] & 0xff));
    }
    logger.msg(kLogInfo1,"]\n");
    return 0;
  }


  // note: this is an attempt to remove "bad" chunks as seen in "nyan.zip" world
  inline bool legalChunkPos ( int32_t chunkX, int32_t chunkZ ) {
    if ( (uint32_t)chunkX == 0x80000000 && (uint32_t)chunkZ == 0x80000000 ) {
      return false;
    }
    return true;
  }
    


  class McpeWorld {
  public:
    leveldb::DB* db;
    leveldb::Options dbOptions;
    leveldb::ReadOptions dbReadOptions;
      
    ChunkDataList chunkList[kDimIdCount];
      
    McpeWorld() {
      db = nullptr;
      dbOptions.compressors[0] = new leveldb::ZlibCompressor();
      dbOptions.create_if_missing = false;
      dbReadOptions.fill_cache = false;
      for (int i=0; i < kDimIdCount; i++) {
	chunkList[i].dimId = i;
	chunkList[i].clearChunkBounds();
      }
      chunkList[kDimIdOverworld].setName("overworld");
      chunkList[kDimIdNether].setName("nether");
    }
    ~McpeWorld() {
      dbClose();
      delete dbOptions.compressors[0];
    }
      
    int dbOpen(std::string dirDb) {
      // todobig - leveldb read-only? snapshot?
      fprintf(stderr,"DB Open: dir=%s\n",dirDb.c_str());
      leveldb::Status dstatus = leveldb::DB::Open(dbOptions, std::string(dirDb+"/db"), &db);
      fprintf(stderr,"DB Open Status: %s\n", dstatus.ToString().c_str()); fflush(stderr);
      if (!dstatus.ok()) {
	fprintf(stderr,"ERROR: LevelDB operation returned status=%s\n",dstatus.ToString().c_str());
	exit(-2);
      }
      dbReadOptions.fill_cache=false; // may improve performance?
      return 0;
    }
    int dbClose() {
      if ( db != nullptr ) {
	delete db;
	db = nullptr;
      }
      return 0;
    }

    int calcChunkBounds() {
      // see if we already calculated bounds
      bool passFlag = true;
      for (int i=0; i < kDimIdCount; i++) {
	if ( ! chunkList[i].chunkBoundsValid ) {
	  passFlag = false;
	}
      }
      if ( passFlag ) {
	return 0;
      }

      // clear bounds
      for (int i=0; i < kDimIdCount; i++) {
	chunkList[i].clearChunkBounds();
      }

      int32_t chunkX=-1, chunkZ=-1, chunkDimId=-1, chunkType=-1;
	
      fprintf(stderr,"Scan keys to get world boundaries\n");
      int recordCt = 0;

      // todobig - is there a faster way to enumerate the keys?
      leveldb::Iterator* iter = db->NewIterator(dbReadOptions);
      leveldb::Slice skey;
      int key_size;
      const char* key;
      for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
	skey = iter->key();
	key_size = skey.size();
	key = skey.data();
	  
	++recordCt;
	if ( control.shortRunFlag && recordCt > 1000 ) {
	  break;
	}
	  
	if ( key_size == 9 ) {
	  chunkX = myParseInt32(key, 0);
	  chunkZ = myParseInt32(key, 4);
	  chunkType = myParseInt8(key, 8);
	    
	  // sanity checks
	  if ( chunkType == 0x30 ) {
	    if ( legalChunkPos(chunkX,chunkZ) ) {
	      chunkList[0].addToChunkBounds(chunkX, chunkZ);
	    }
	  }
	}
	else if ( key_size == 13 ) {
	  chunkX = myParseInt32(key, 0);
	  chunkZ = myParseInt32(key, 4);
	  chunkDimId = myParseInt32(key, 8);
	  chunkType = myParseInt8(key, 12);
	    
	  // sanity checks
	  if ( chunkType == 0x30 ) {
	    if ( legalChunkPos(chunkX,chunkZ) ) {
	      chunkList[chunkDimId].addToChunkBounds(chunkX, chunkZ);
	    }
	  }
	}
      }

      if (!iter->status().ok()) {
	fprintf(stderr,"WARNING: LevelDB operation returned status=%s\n",iter->status().ToString().c_str());
      }
      delete iter;

      // mark bounds valid
      for (int i=0; i < kDimIdCount; i++) {
	chunkList[i].setChunkBoundsValid();

	const int32_t chunkW = (chunkList[i].maxChunkX - chunkList[i].minChunkX + 1);
	const int32_t chunkH = (chunkList[i].maxChunkZ - chunkList[i].minChunkZ + 1);
	const int32_t imageW = chunkW * 16;
	const int32_t imageH = chunkH * 16;

	fprintf(stderr,"  Bounds (chunk): DimId=%d X=(%d %d) Z=(%d %d)\n"
		, i
		, chunkList[i].minChunkX, chunkList[i].maxChunkX
		, chunkList[i].minChunkZ, chunkList[i].maxChunkZ
		);
	fprintf(stderr,"  Bounds (pixel): DimId=%d X=(%d %d) Z=(%d %d) Image=(%d %d)\n"
		, i
		, chunkList[i].minChunkX*16, chunkList[i].maxChunkX*16
		, chunkList[i].minChunkZ*16, chunkList[i].maxChunkZ*16
		, imageW, imageH
		);
      }

      fprintf(stderr,"  %d records\n", recordCt);
	
      return 0;
    }


    // this is where we go through every item in the leveldb, we parse interesting things as we go
    int parseDb () {

      char tmpstring[256];

      int32_t chunkX=-1, chunkZ=-1, chunkDimId=-1, chunkType=-1;

      // we make sure that we know the chunk bounds before we start so that we can translate world coords to image coords
      calcChunkBounds();

      // report hide and force lists
      {
	fprintf(stderr,"Active 'hide-top' and 'force-top':\n");
	int itemCt = 0;
	int32_t blockId;
	for (int dimId=0; dimId < kDimIdCount; dimId++) {
	  chunkList[dimId].updateFastLists();
	  for ( const auto& iter : chunkList[dimId].blockHideList ) {
	    blockId = iter;
	    fprintf(stderr,"  'hide-top' block: %s - %s (dimId=%d blockId=%d (0x%02x))\n", chunkList[dimId].name.c_str(), blockInfoList[blockId].name.c_str(), dimId, blockId, blockId);
	    itemCt++;
	  }

	  for ( const auto& iter : chunkList[dimId].blockForceTopList ) {
	    blockId = iter;
	    fprintf(stderr,"  'force-top' block: %s - %s (dimId=%d blockId=%d (0x%02x))\n", chunkList[dimId].name.c_str(), blockInfoList[blockId].name.c_str(), dimId, blockId, blockId);
	    itemCt++;
	  }
	}
	if ( itemCt == 0 ) {
	  fprintf(stderr,"None\n");
	}
      }
	    
      fprintf(stderr,"Parse all leveldb records\n");

      MyNbtTagList tagList;
      int recordCt = 0, ret;

      leveldb::Slice skey, svalue;
      int key_size, value_size;
      const char* key;
      const char* value;
      std::string dimName, chunkstr;

      leveldb::Iterator* iter = db->NewIterator(dbReadOptions);
      for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {

	// note: we get the raw buffer early to avoid overhead (maybe?)
	skey = iter->key();
	key_size = (int)skey.size();
	key = skey.data();

	svalue = iter->value();
	value_size = svalue.size();
	value = svalue.data();

	++recordCt;
	if ( control.shortRunFlag && recordCt > 1000 ) {
	  break;
	}
	if ( (recordCt % 10000) == 0 ) {
	  fprintf(stderr, "  Reading records: %d\n", recordCt);
	}

	logger.msg(kLogInfo1,"\n");

	// we look at the key to determine what we have, some records have text keys

	if ( strncmp(key,"BiomeData",key_size) == 0 ) {
	  // 0x61 +"BiomeData" -- snow accum? -- overworld only?
	  logger.msg(kLogInfo1,"BiomeData value:\n");
	  parseNbt("BiomeData: ", value, value_size, tagList);
	  // todo - parse tagList? snow accumulation amounts
	}

	else if ( strncmp(key,"Overworld",key_size) == 0 ) {
	  logger.msg(kLogInfo1,"Overworld value:\n");
	  parseNbt("Overworld: ", value, value_size, tagList);
	  // todo - parse tagList? a list of "LimboEntities"
	}

	else if ( strncmp(key,"~local_player",key_size) == 0 ) {
	  logger.msg(kLogInfo1,"Local Player value:\n");
	  ret = parseNbt("Local Player: ", value, value_size, tagList);
	  if ( ret == 0 ) { 
	    parseNbt_entity(-1, "",tagList, true, false);
	  }
	}

	else if ( (key_size>=7) && (strncmp(key,"player_",7) == 0) ) {
	  // note: key contains player id (e.g. "player_-1234")
	  std::string playerRemoteId = &key[strlen("player_")];
	  logger.msg(kLogInfo1,"Remote Player (id=%s) value:\n",playerRemoteId.c_str());
	  ret = parseNbt("Remote Player: ", value, value_size, tagList);
	  if ( ret == 0 ) {
	    parseNbt_entity(-1, "",tagList, false, true);
	  }
	}

	else if ( strncmp(key,"villages",key_size) == 0 ) {
	  logger.msg(kLogInfo1,"Villages value:\n");
	  parseNbt("villages: ", value, value_size, tagList);
	  // todo - parse tagList? usually empty, unless player is in range of village; test that!
	}

	else if ( strncmp(key,"mVillages",key_size) == 0 ) {
	  // todobig todohere -- new for 0.13?
	  logger.msg(kLogInfo1,"mVillages value:\n");
	  parseNbt("mVillages: ", value, value_size, tagList);
	  // todo - parse tagList?
	}

	else if ( strncmp(key,"Nether",key_size) == 0 ) {
	  logger.msg(kLogInfo1,"Nether value:\n");
	  parseNbt("Nether: ", value, value_size, tagList);
	  // todo - parse tagList?  list of LimboEntities
	}

	else if ( strncmp(key,"portals",key_size) == 0 ) {
	  logger.msg(kLogInfo1,"portals value:\n");
	  ret = parseNbt("portals: ", value, value_size, tagList);
	  if ( ret == 0 ) {
	    parseNbt_portals(tagList);
	  }
	}
			 
	else if ( key_size == 9 || key_size == 13 ) {

	  // these are probably chunk records, we parse the key and determine what we've got

	  if ( key_size == 9 ) {
	    // overworld chunk
	    chunkX = myParseInt32(key, 0);
	    chunkZ = myParseInt32(key, 4);
	    chunkDimId = kDimIdOverworld;
	    chunkType = myParseInt8(key, 8);
	    dimName = "overworld";
	  }
	  else if ( key_size == 13 ) {
	    // non-overworld chunk
	    chunkX = myParseInt32(key, 0);
	    chunkZ = myParseInt32(key, 4);
	    chunkDimId = myParseInt32(key, 8);
	    chunkType = myParseInt8(key, 12);
	    dimName = "nether";

	    // check for new dim id's
	    if ( chunkDimId != kDimIdNether ) {
	      fprintf(stderr, "HEY! Found new chunkDimId=0x%x -- we are not prepared for that -- skipping chunk\n", chunkDimId);
	      continue;
	    }
	  }

	  // we check for corrupt chunks
	  if ( ! legalChunkPos(chunkX,chunkZ) ) {
	    fprintf(stderr,"WARNING: Found a chunk with invalid chunk coordinates cx=%d cz=%d\n", chunkX, chunkZ);
	    continue;
	  }

	  chunkList[chunkDimId].histogramChunkType[chunkType]++;

	  // report info about the chunk
	  chunkstr = dimName + "-chunk: ";
	  sprintf(tmpstring,"%d %d (type=0x%02x)", chunkX, chunkZ, chunkType);
	  chunkstr += tmpstring;
	  if ( true ) {
	    // show approximate image coordinates for chunk
	    int imageX = (chunkX - chunkList[chunkDimId].minChunkX) * 16;
	    int imageZ = (chunkZ - chunkList[chunkDimId].maxChunkZ) * 16;
	    sprintf(tmpstring," (image %d %d)", imageX, imageZ);
	    chunkstr+=tmpstring;
	  }
	  chunkstr += "\n";
	  logger.msg(kLogInfo1, "%s", chunkstr.c_str());

	  // see what kind of chunk we have
	  switch ( chunkType ) {
	  case 0x30:
	    // chunk block data
	    // we do the parsing in the destination object to save memcpy's
	    chunkList[chunkDimId].addChunk(chunkX,chunkZ,value);
	    break;

	  case 0x31:
	    // tile entity record (e.g. a chest)
	    logger.msg(kLogInfo1,"%s 0x31 chunk (tile entity data):\n", dimName.c_str());
	    ret = parseNbt("0x31-te: ", value, value_size, tagList);
	    if ( ret == 0 ) { 
	      parseNbt_tileEntity(chunkDimId, dimName+"-", tagList);
	    }
	    break;

	  case 0x32:
	    // entity record (e.g. a mob)
	    logger.msg(kLogInfo1,"%s 0x32 chunk (entity data):\n", dimName.c_str());
	    ret = parseNbt("0x32-e: ", value, value_size, tagList);
	    if ( ret == 0 ) {
	      parseNbt_entity(chunkDimId, dimName+"-", tagList, false, false);
	    }
	    break;

	  case 0x33:
	    // todo - this appears to be info on blocks that can move: water + lava + fire + sand + gravel
	    logger.msg(kLogInfo1,"%s 0x33 chunk (tick-list):\n", dimName.c_str());
	    parseNbt("0x33-tick: ", value, value_size, tagList);
	    // todo - parse tagList?
	    // todobig - could show location of active fires
	    break;

	  case 0x34:
	    logger.msg(kLogInfo1,"%s 0x34 chunk (TODO - UNKNOWN RECORD)\n", dimName.c_str());
	    printKeyValue(key,key_size,value,value_size,false);
	    /* 
	       0x34 ?? does not appear to be NBT data -- overworld only? -- perhaps: b0..3 (count); for each: (int32_t) (int16_t) 
	       -- there are 206 of these in "another1" world
	       -- something to do with snow?
	       -- to examine data:
	       cat xee | grep "WARNING: Unknown key size" | grep " 34\]" | cut -b75- | sort | nl
	    */
	    break;

	  case 0x35:
	    logger.msg(kLogInfo1,"%s 0x35 chunk (TODO - UNKNOWN RECORD)\n", dimName.c_str());
	    printKeyValue(key,key_size,value,value_size,false);
	    /*
	      0x35 ?? -- both dimensions -- length 3,5,7,9,11 -- appears to be: b0 (count of items) b1..bn (2-byte ints) 
	      -- there are 2907 in "another1"
	      -- to examine data:
	      cat xee | grep "WARNING: Unknown key size" | grep " 35\]" | cut -b75- | sort | nl
	    */
	    break;

	  case 0x76:
	    // todo - this is chunk version information?
	    {
	      // this record is not very interesting we usually hide it
	      // note: it would be interesting if this is not == 2 (as of MCPE 0.12.x it is always 2)
	      if ( control.verboseFlag || (value[0] != 2) ) { 
		logger.msg(kLogInfo1,"%s 0x76 chunk (world format version): v=%d\n", dimName.c_str(), (int)(value[0]));
	      }
	    }
	    break;

	  default:
	    logger.msg(kLogInfo1,"WARNING: %s unknown chunk - size=%d type=0x%x length=%d\n", dimName.c_str(),
		       key_size, chunkType, value_size);
	    printKeyValue(key,key_size,value,value_size,true);
	    if ( false ) {
	      if ( value_size > 10 ) {
		parseNbt("UNK: ", value, value_size, tagList);
	      }
	    }
	    break;
	  }
	}
	else {
	  logger.msg(kLogInfo1,"WARNING: Unknown chunk - key_size=%d value_size=%d\n", key_size, value_size);
	  printKeyValue(key,key_size,value,value_size,true);
	  if ( false ) { 
	    // try to nbt decode
	    logger.msg(kLogInfo1,"WARNING: Attempting NBT Decode:\n");
	    parseNbt("WARNING: ", value, value_size, tagList);
	  }
	}
      }
      fprintf(stderr,"Read %d records\n", recordCt);
      fprintf(stderr,"Status: %s\n", iter->status().ToString().c_str());
      
      if (!iter->status().ok()) {
	fprintf(stderr,"WARNING: LevelDB operation returned status=%s\n",iter->status().ToString().c_str());
      }
      delete iter;

      return 0;
    }

    // todobig - move to util.h
    class PngTiler {
    public:
      std::string filename;
      int tileWidth;
      int tileHeight;
      std::string dirOutput;
	
      PngTiler(const std::string fn, int tileW, int tileH, const std::string dirOut) {
	filename = fn;
	tileWidth = tileW;
	tileHeight = tileH;
	dirOutput = dirOut;
      }

      int doTile() {
	// todobig - store tile filenames?

	// todobig todohere:
	// open src file
	// determine number of tiles
	// iterate over src image space
	// close src file
	char tmpstring[256];

	// open source file
	PngReader pngSrc;
	pngSrc.init(filename);
	pngSrc.read();

	int srcW = pngSrc.getWidth();
	int srcH = pngSrc.getHeight();

	int numPngW = (int)ceil((double)srcW / (double)tileWidth);

	PngWriter *pngOut = new PngWriter[numPngW];
	uint8_t **buf;
	buf = new uint8_t*[numPngW];
	for (int i=0; i < numPngW; i++) {
	  buf[i] = new uint8_t[tileWidth * tileHeight * 3];
	}
	
	bool initPngFlag = false;
	int tileCounterY=0;

	for (int sy=0; sy < srcH; sy++) {

	  // initialize png helpers
	  if ( ! initPngFlag ) {
	    initPngFlag = true;
	    for (int i=0; i < numPngW; i++) {
	      sprintf(tmpstring,"%s/%s.%d.%d.png", dirOutput.c_str(), mybasename(filename).c_str(),
		      tileCounterY, i);
	      std::string fname = tmpstring;
	      pngOut[i].init(fname, "MCPE Viz Image Tile", tileWidth, tileHeight, tileHeight);

	      // clear buffer
	      memset(&buf[i][0], 0, tileWidth * tileHeight * 3);
	      
	      // setup row_pointers
	      for (int ty=0; ty < tileHeight; ty++) {
		pngOut[i].row_pointers[ty] = &buf[i][ty*tileWidth * 3];
	      }
	    }
	    tileCounterY++;
	  }

	  uint8_t *srcbuf = pngSrc.row_pointers[sy];

	  int tileOffsetY = sy % tileHeight;
	  
	  // todobig - step in tileWidth and memcpy as we go - need to check the last one for out of bounds
	  for (int sx=0; sx < srcW; sx++) {
	    int tileCounterX = sx / tileWidth;
	    int tileOffsetX = sx % tileWidth;
	    memcpy(&buf[tileCounterX][((tileOffsetY * tileWidth) + tileOffsetX) * 3], &srcbuf[sx*3], 3);
	  }
	  
	  // write tile png files when they are ready
	  if ( ((sy+1) % tileHeight) == 0 ) {
	    // write pngs
	    for (int i=0; i < numPngW; i++) {
	      png_write_image(pngOut[i].png, pngOut[i].row_pointers);
	      pngOut[i].close();
	    }
	    initPngFlag = false;
	  }
	}

	// close final tiles
	if ( initPngFlag ) {
	  // write pngs
	  for (int i=0; i < numPngW; i++) {
	    png_write_image(pngOut[i].png, pngOut[i].row_pointers);
	    pngOut[i].close();
	  }
	}

	delete [] pngOut;

	for (int i=0; i < numPngW; i++) {
	  delete [] buf[i];
	}
	delete [] buf;

	pngSrc.close();
	
	return 0;
      }
      
    };

    int doOutput_Tile_image(const std::string& fn) {
      if ( fn.size() <= 0 ) {
	return -1;
      }
      
      std::string dirOut = mydirname(control.fnOutputBase) + "/tiles";
      // todobig - check if dir exists first
#ifdef WINVER
      mkdir(dirOut.c_str());
#else
      mkdir(dirOut.c_str(),0755);
#endif

      fprintf(stderr,"Creating tiles for %s...\n", mybasename(fn).c_str());
      PngTiler pngTiler(fn, control.tileWidth, control.tileHeight, dirOut);
      if ( pngTiler.doTile() == 0 ) {
	// all is good
      } else {
	// todobig - error
      }

      return 0;
    }

    int doOutput_Tile() {
      // todobig todohere - should we tile no matter what? would make js side easier
      if ( ! control.doTiles ) {
	return 0;
      }

      for (int dimid=0; dimid < kDimIdCount; dimid++) {
	doOutput_Tile_image(control.fnLayerTop[dimid]);
	doOutput_Tile_image(control.fnLayerBiome[dimid]);
	doOutput_Tile_image(control.fnLayerHeight[dimid]);
	doOutput_Tile_image(control.fnLayerHeightGrayscale[dimid]);
	doOutput_Tile_image(control.fnLayerBlockLight[dimid]);
	doOutput_Tile_image(control.fnLayerSkyLight[dimid]);
	doOutput_Tile_image(control.fnLayerSlimeChunks[dimid]);
	doOutput_Tile_image(control.fnLayerGrass[dimid]);
	for (int cy=0; cy<128; cy++) {
	  doOutput_Tile_image(control.fnLayerRaw[dimid][cy]);
	}
      }

      return 0;
    }

    
    std::string makeTileURL(const std::string fn) {
      std::string ret = mybasename(fn);
      if ( ! control.doTiles ) {
	return ret;
      }
      if ( ret.size() > 1 ) {
	return "tiles/" + ret + ".{y}.{x}.png";
      }
      return "";
    }

    
    int doOutput_html() {
      char tmpstring[1025];
	
      fprintf(stderr,"Do Output: html viewer\n");
	
      sprintf(tmpstring,"%s/mcpe_viz.html.template", dirExec.c_str());
      std::string fnHtmlSrc = tmpstring;
	
      sprintf(tmpstring,"%s/mcpe_viz.js", dirExec.c_str());
      std::string fnJsSrc = tmpstring;
	
      sprintf(tmpstring,"%s/mcpe_viz.css", dirExec.c_str());
      std::string fnCssSrc = tmpstring;
	  
      // create html file -- need to substitute one variable (extra js file)
      StringReplacementList replaceStrings;

      if ( control.noForceGeoJSONFlag ) {
	// we do not include the geojson file
	replaceStrings.push_back( std::make_pair( std::string("%JSFILE%"),
						  "<script src=\"" +
						  std::string(mybasename(control.fnJs.c_str())) +
						  "\"></script>"
						  )
				  );
      } else {
	// we do include the geojson file
	replaceStrings.push_back( std::make_pair( std::string("%JSFILE%"),
						  "<script src=\"" +
						  std::string(mybasename(control.fnJs.c_str())) +
						  "\"></script>\n" +
						  "<script src=\"" +
						  std::string(mybasename(control.fnGeoJSON.c_str())) +
						  "\"></script>"
						  )
				  );
      }
      copyFileWithStringReplacement(fnHtmlSrc, control.fnHtml, replaceStrings);
	  
      // create javascript file w/ filenames etc
      FILE *fp = fopen(control.fnJs.c_str(),"w");
      if ( fp ) {
	time_t xtime = time(NULL);
	char timebuf[256];
	ctime_r(&xtime, timebuf);
	// todo - this is hideous.
	// fix time string
	char *p = strchr(timebuf,'\n');
	if ( p ) { *p = 0; }

	fprintf(fp,
		"// mcpe_viz javascript helper file -- created by mcpe_viz program\n"
		"var worldName = '%s';\n"
		"var worldSeed = %lld;\n"
		"var creationTime = '%s';\n"
		"var loadGeoJSONFlag = %s;\n"
		"var fnGeoJSON = '%s';\n"
		"var tileW = %d;\n"
		"var tileH = %d;\n"
		"var dimensionInfo = {\n"
		, escapeString(globalLevelName.c_str(), "'").c_str()
		, (long long int)worldSeed
		, escapeString(timebuf,"'").c_str()
		, control.noForceGeoJSONFlag ? "true" : "false"
		, mybasename(control.fnGeoJSON).c_str()
		, control.tileWidth
		, control.tileHeight
		);
	for (int did=0; did < kDimIdCount; did++) {
	  fprintf(fp, "'%d': {\n", did);
	  fprintf(fp,"  minWorldX: %d,\n", chunkList[did].minChunkX*16);
	  fprintf(fp,"  maxWorldX: %d + 15,\n", chunkList[did].maxChunkX*16);
	  fprintf(fp,"  minWorldY: %d,\n", chunkList[did].minChunkZ*16);
	  fprintf(fp,"  maxWorldY: %d + 15,\n", chunkList[did].maxChunkZ*16);
	  
	  int32_t px, py;
	  if ( did == kDimIdNether ) {
	    px = playerPositionImageX / 8;
	    py = playerPositionImageY / 8;
	  } else {
	    px = playerPositionImageX;
	    py = playerPositionImageY;
	  }
	  fprintf(fp,"  playerPosX: %d,\n", px);
	  fprintf(fp,"  playerPosY: %d,\n", py);

	  // todobig todohere - need conditional on adding {y}{x}
	  fprintf(fp,"  fnLayerTop: '%s',\n", makeTileURL(control.fnLayerTop[did]).c_str());
	  fprintf(fp,"  fnLayerBiome: '%s',\n", makeTileURL(control.fnLayerBiome[did]).c_str());
	  fprintf(fp,"  fnLayerHeight: '%s',\n", makeTileURL(control.fnLayerHeight[did]).c_str());
	  fprintf(fp,"  fnLayerHeightGrayscale: '%s',\n", makeTileURL(control.fnLayerHeightGrayscale[did]).c_str());
	  fprintf(fp,"  fnLayerBlockLight: '%s',\n", makeTileURL(control.fnLayerBlockLight[did]).c_str());
	  fprintf(fp,"  fnLayerSlimeChunks: '%s',\n", makeTileURL(control.fnLayerSlimeChunks[did]).c_str());
	  fprintf(fp,"  fnLayerGrass: '%s',\n", makeTileURL(control.fnLayerGrass[did]).c_str());
	      
	  fprintf(fp,"  listLayers: [\n");
	  for (int i=0; i < 128; i++) {
	    fprintf(fp, "    '%s',\n", makeTileURL(control.fnLayerRaw[did][i]).c_str());
	  }
	  fprintf(fp,"  ]\n");
	  if ( (did+1) < kDimIdCount ) {
	    fprintf(fp,"},\n");
	  } else {
	    fprintf(fp,"}\n");
	  }
	}
	fprintf(fp,"};\n");

	// write block color info
	fprintf(fp,
		"// a lookup table for identifying blocks in the web app\n"
		"// key is color (decimal), value is block name\n"
		"// hacky? it sure is!\n"
		);
	    
	fprintf(fp,"var blockColorLUT = {\n");
	for (int i=0; i < 256; i++) {
	  if ( blockInfoList[i].hasVariants() ) {
	    // we need to get blockdata
	    for (const auto& itbv : blockInfoList[i].variantList) {
	      fprintf(fp,"'%d': '%s',\n", be32toh(itbv->color), escapeString(itbv->name,"'").c_str());
	    }
	  } else {
	    if ( blockInfoList[i].colorSetFlag ) {
	      fprintf(fp,"'%d': '%s',\n", be32toh(blockInfoList[i].color), escapeString(blockInfoList[i].name,"'").c_str());
	    }
	  }
	}
	// last, put the catch-all
	fprintf(fp,"'%d': '*UNKNOWN BLOCK*'\n};\n",kColorDefault);

	// write biome color info
	fprintf(fp,
		"// a lookup table for identifying biomes in the web app\n"
		"// key is color (decimal), value is biome name\n"
		"// hacky? it sure is!\n"
		);
	    
	fprintf(fp,"var biomeColorLUT = {\n");
	for ( const auto& it : biomeInfoList ) {
	  if ( it.second->colorSetFlag ) {
	    fprintf(fp,"'%d': '%s (id=%d (0x%x))',\n"
		    , be32toh(it.second->color)
		    , escapeString(it.second->name,"'").c_str()
		    , it.first
		    , it.first
		    );
	  }
	}
	// last, put the catch-all
	fprintf(fp,"'%d': '*UNKNOWN BIOME*'\n};\n",kColorDefault);
	    
	fclose(fp);
	    
      } else {
	fprintf(stderr,"ERROR: Failed to open javascript output file (fn=%s)\n", control.fnJs.c_str());
      }
	  
      // copy helper files to destination directory
      std::string dirDest = mydirname(control.fnOutputBase);
	  
      if ( dirDest.size() > 0 && dirDest != "." ) {
	// todo - how to be sure that this is a diff dir?
	sprintf(tmpstring,"%s/%s", dirDest.c_str(), mybasename(fnJsSrc).c_str());
	std::string fnJsDest = tmpstring;
	copyFile(fnJsSrc, fnJsDest);

	sprintf(tmpstring,"%s/%s", dirDest.c_str(), mybasename(fnCssSrc).c_str());
	std::string fnCssDest = tmpstring;
	copyFile(fnCssSrc, fnCssDest);
      } else {
	// if same dir, don't copy files
      }

      return 0;
    }

      
    int doOutput_colortest() {
      fprintf(stderr,"Do Output: html colortest\n");
	
      std::string fnOut = control.fnOutputBase + ".colortest.html";
      FILE *fp = fopen(fnOut.c_str(), "w");
	
      if ( !fp ) {
	fprintf(stderr, "ERROR: failed to open output file (%s)\n", fnOut.c_str());
	return -1;
      } 
	  
      // put start of html file
      fprintf(fp,
	      "<!doctype html>\n"
	      "<html><head><title>MCPE Viz Color Test</title>\n"
	      "<style>"
	      ".section { width: 100%%; padding: 2em 2em; }"
	      ".colorBlock { width: 100%%; padding: 0.5em 2em; }"
	      ".darkBlock  { color: #ffffff; }"
	      "</style>"
	      "</head>"
	      "<body>"
	      );
	
      // create list of all colors and sort them by HSL
      std::vector< std::unique_ptr<ColorInfo> > webColorList;

      webColorList.clear();
      for (int i=0; i < 256; i++) {
	if ( blockInfoList[i].hasVariants() ) {
	  for (const auto& itbv : blockInfoList[i].variantList) {
	    webColorList.push_back( std::unique_ptr<ColorInfo>
				    ( new ColorInfo(itbv->name, be32toh(itbv->color)) ) );
	  }
	}
	else {
	  if ( blockInfoList[i].colorSetFlag ) {
	    webColorList.push_back( std::unique_ptr<ColorInfo>
				    ( new ColorInfo(blockInfoList[i].name, be32toh(blockInfoList[i].color)) ) );
	  }
	}
      }

      std::sort(webColorList.begin(), webColorList.end(), compareColorInfo);
      fprintf(fp, "<div class=\"section\">Block Colors</div>");
      for (const auto& it : webColorList) {
	fprintf(fp, "%s\n", it->toHtml().c_str());
      }



      webColorList.clear();
      for ( const auto& it : biomeInfoList ) {
	if ( it.second->colorSetFlag ) {
	  // webColorList.emplace_back(blockInfoList[i].name, (int32_t)be32toh(blockInfoList[i].color));
	  webColorList.push_back( std::unique_ptr<ColorInfo>( new ColorInfo(it.second->name, be32toh(it.second->color)) ) );
	}
      }

      std::sort(webColorList.begin(), webColorList.end(), compareColorInfo);
      fprintf(fp, "<div class=\"section\">Biome Colors</div>");
      for (const auto& it : webColorList) {
	fprintf(fp, "%s\n", it->toHtml().c_str());
      }
	
      fprintf(fp,"\n</body></html>\n");
      fclose(fp);
      return 0;
    }


    int doOutput_GeoJSON() {

      if ( false ) { 
#if 0
	// todobig - this would be lovely but does not work when run on windows (browser does not like the gzip'ed geojson file)
	
	// we output gzip'ed data (saves a ton of disk+bandwidth for very little cost)
	
	gzFile_s* fpGeoJSON = gzopen(control.fnGeoJSON.c_str(), "w");
	if ( ! fpGeoJSON ) {
	  fprintf(stderr,"ERROR: Failed to create GeoJSON output file (%s).\n", control.fnGeoJSON.c_str());
	  return -1;
	}
	
	// set params for gzip
	//gzsetparams(fpGeoJSON, Z_BEST_COMPRESSION, Z_DEFAULT_STRATEGY);
	
	// put the geojson preamble stuff
	if ( ! control.noForceGeoJSONFlag ) {
	  gzprintf(fpGeoJSON, "var geojson =\n" );
	}
	gzprintf(fpGeoJSON,
		 "{ \"type\": \"FeatureCollection\",\n"
		 // todo - correct way to specify this?
		 "\"crs\": { \"type\": \"name\", \"properties\": { \"name\": \"mcpe_viz-image\" } },\n"
		 "\"features\": [\n"
		 );

	// put the list with correct commas (silly)
	int i = listGeoJSON.size();
	for ( const auto& it: listGeoJSON ) {
	  gzputs(fpGeoJSON, it.c_str());
	  if ( --i > 0 ) {
	    gzputc(fpGeoJSON,',');
	  }
	  gzputc(fpGeoJSON,'\n');
	}

	// close out the geojson properly
	if ( control.noForceGeoJSONFlag ) {
	  gzprintf(fpGeoJSON,"] }\n");
	} else {
	  gzprintf(fpGeoJSON,"] };\n");
	}
	  
	gzclose(fpGeoJSON);
#endif
      } else {

	// plain text file version
	
	FILE* fpGeoJSON = fopen(control.fnGeoJSON.c_str(), "w");
	if ( ! fpGeoJSON ) {
	  fprintf(stderr,"ERROR: Failed to create GeoJSON output file (%s).\n", control.fnGeoJSON.c_str());
	  return -1;
	}
	
	// put the geojson preamble stuff
	if ( ! control.noForceGeoJSONFlag ) {
	  fprintf(fpGeoJSON, "var geojson =\n" );
	}
	fprintf(fpGeoJSON,
		"{ \"type\": \"FeatureCollection\",\n"
		// todo - correct way to specify this?
		"\"crs\": { \"type\": \"name\", \"properties\": { \"name\": \"mcpe_viz-image\" } },\n"
		"\"features\": [\n"
		);

	// put the list with correct commas (silly)
	int i = listGeoJSON.size();
	for ( const auto& it: listGeoJSON ) {
	  fputs(it.c_str(), fpGeoJSON);
	  if ( --i > 0 ) {
	    fputc(',',fpGeoJSON);
	  }
	  fputc('\n',fpGeoJSON);
	}

	// close out the geojson properly
	if ( control.noForceGeoJSONFlag ) {
	  fprintf(fpGeoJSON,"] }\n");
	} else {
	  fprintf(fpGeoJSON,"] };\n");
	}
	  
	fclose(fpGeoJSON);
	
      }
      return 0;
    }

      
    int doOutput() {
      calcChunkBounds();
      for (int i=0; i < kDimIdCount; i++) {
	chunkList[i].doOutput(db);
      }

      if ( control.doHtml ) {
	doOutput_Tile();
	doOutput_html();
	doOutput_GeoJSON();
      }
	
      if ( control.colorTestFlag ) {
	doOutput_colortest();
      }
	
      return 0;
    }
  };

  McpeWorld world;

    
  void worldPointToImagePoint(int32_t dimId, float wx, float wz, int &ix, int &iy, bool geoJsonFlag) {
    // hack to avoid using wrong dim on pre-0.12 worlds
    if ( dimId < 0 ) { dimId = 0; }
      
    const int32_t chunkOffsetX = -world.chunkList[dimId].minChunkX;
    const int32_t chunkOffsetZ = -world.chunkList[dimId].minChunkZ;

    if ( geoJsonFlag ) {
      const int32_t chunkH = (world.chunkList[dimId].maxChunkZ - world.chunkList[dimId].minChunkZ + 1);
      const int32_t imageH = chunkH * 16;
	
      ix = wx + (chunkOffsetX * 16);
      // todobig - correct calc here?
      iy = (imageH-1) - (wz + (chunkOffsetZ * 16));
    } else {
      ix = wx + (chunkOffsetX * 16);
      iy = wz + (chunkOffsetZ * 16);
    }

    // adjust for nether
    /*
      if ( dimId == kDimIdNether ) {
      ix /= 8;
      iy /= 8;
      }
    */
  }
    
    
  int parseLevelFile(const std::string fname) {
    FILE *fp = fopen(fname.c_str(), "rb");
    if(!fp) {
      return -1;
    }

    int32_t fVersion;
    int32_t bufLen;
    fread(&fVersion, sizeof(int32_t), 1, fp);
    fread(&bufLen, sizeof(int32_t), 1, fp);

    fprintf(stderr,"parseLevelFile: name=%s version=%d len=%d\n", fname.c_str(), fVersion, bufLen);

    int ret = -2;
    if ( bufLen > 0 ) { 
      // read content
      char* buf = new char[bufLen];
      fread(buf,1,bufLen,fp);
      fclose(fp);

      MyNbtTagList tagList;
      ret = parseNbt("level.dat: ", buf, bufLen, tagList);
	
      if ( ret == 0 ) {
	nbt::tag_compound tc = tagList[0].second->as<nbt::tag_compound>();

	worldSpawnX = tc["SpawnX"].as<nbt::tag_int>().get();
	worldSpawnY = tc["SpawnY"].as<nbt::tag_int>().get();
	worldSpawnZ = tc["SpawnZ"].as<nbt::tag_int>().get();
	fprintf(stderr, "  Found World Spawn: x=%d y=%d z=%d\n", worldSpawnX, worldSpawnY, worldSpawnZ);

	worldSeed = tc["RandomSeed"].as<nbt::tag_long>().get();
      }

      delete [] buf;
    } else {
      fclose(fp);
    }
      
    return ret;
  }

    
  int parseLevelName(const std::string fname) {
    FILE *fp = fopen(fname.c_str(), "r");
    if(!fp) {
      return -1;
    }

    char buf[1025];
    memset(buf,0,1025);
    fgets(buf,1024,fp);

    globalLevelName = buf;
      
    fprintf(stderr,"  Level name is [%s]\n", (strlen(buf) > 0 ) ? buf : "(UNKNOWN)");
    logger.msg(kLogInfo1,"\nlevelname.txt: Level name is [%s]\n", (strlen(buf) > 0 ) ? buf : "(UNKNOWN)");
    fclose(fp);

    return 0;
  }

    
  int doParseConfigFile ( const std::string fn ) {
    if ( ! file_exists(fn.c_str()) ) {
      return -1;
    }

    // todo - this should use streams

    const char* hdr = "";
    int indent = 1;
      
    FILE *fp = fopen(fn.c_str(), "r");
    if ( ! fp ) {
      fprintf(stderr,"ERROR: Failed to open file (%s)\n", fn.c_str());
      return 1;
    }

    fprintf(stderr,"Reading config from %s\n", fn.c_str());
      
    char buf[1025], *p;
    while ( !feof(fp) ) {
      memset(buf,0,1025);
      fgets(buf,1024,fp);

      // remove comments and newlines
      if ( (p=strchr(buf,'#')) ) {
	*p = 0;
      }
      if ( (p=strchr(buf,'\n')) ) {
	*p = 0;
      }
      if ( (p=strchr(buf,'\r')) ) {
	*p = 0;
      }
	
      if ( (p=strstr(buf,"hide-top:")) ) {
	int32_t dimId = -1;
	int blockId = -1;
	int pass = false;
	if ( sscanf(&p[9],"%d 0x%x", &dimId, &blockId) == 2 ) {
	  pass = true;
	}
	else if ( sscanf(&p[9],"%d %d", &dimId, &blockId) == 2 ) {
	  pass = true;
	}
	// check dimId
	if ( dimId < kDimIdOverworld || dimId >= kDimIdCount ) {
	  pass = false;
	}
	if ( pass ) {
	  // add to hide list
	  world.chunkList[dimId].blockHideList.push_back(blockId);
	} else {
	  fprintf(stderr,"%sERROR: Failed to parse cfg item 'hide-top': [%s]\n", makeIndent(indent,hdr).c_str(), buf);
	}
      }

      else if ( (p=strstr(buf,"force-top:")) ) {
	int32_t dimId = -1;
	int blockId = -1;
	int pass = false;
	if ( sscanf(&p[10],"%d 0x%x", &dimId, &blockId) == 2 ) {
	  pass = true;
	}
	else if ( sscanf(&p[10],"%d %d", &dimId, &blockId) == 2 ) {
	  pass = true;
	}
	// check dimId
	if ( dimId < kDimIdOverworld || dimId >= kDimIdCount ) {
	  pass = false;
	}
	if ( pass ) {
	  // add to hide list
	  world.chunkList[dimId].blockForceTopList.push_back(blockId);
	} else {
	  fprintf(stderr,"%sERROR: Failed to parse cfg item 'hide': [%s]\n", makeIndent(indent,hdr).c_str(), buf);
	}
      }

      else {
	if ( strlen(buf) > 0 ) {
	  fprintf(stderr,"%sWARNING: Unparsed config line: [%s]\n", makeIndent(indent,hdr).c_str(),buf);
	}
      }
	
    }

    fclose(fp);
    return 0;
  }


  int parseConfigFile () {
    // parse cfg files in this order:
    // -- option specified on command-line
    // -- master dir
    // -- exec dir
    // -- local dir
    std::string fn;

    // as specified on cmdline
    if ( control.fnCfg.size() > 0 ) {
      if ( doParseConfigFile( control.fnCfg ) == 0 ) {
	return 0;
      }
    }

    // default config file
    // todo - how to support on win32? %HOMEPATH%?
    if ( getenv("HOME") ) {
      std::string fnHome = getenv("HOME");
      fnHome += "/.mcpe_viz/mcpe_viz.cfg";
      if ( doParseConfigFile( fnHome ) == 0 ) {
	return 0;
      }
    }
      
    // same dir as exec
    fn = dirExec;
    fn += "/mcpe_viz.cfg";
    if ( doParseConfigFile( fn ) == 0 ) {
      return 0;
    }

    // local dir
    fn = "./mcpe_viz.cfg";
    if ( doParseConfigFile( fn ) == 0 ) {
      return 0;
    }

    //fprintf(stderr,"WARNING: Did not find a valid config file\n");
    return -1;
  }



  int parseXml ( ) {
    // parse xml file in this order:
    // -- option specified on command-line
    // -- master dir
    // -- exec dir
    // -- local dir
    std::string fn;
    int ret;

    // as specified on cmdline
    if ( control.fnXml.length() > 0 ) {
      ret = doParseXml(control.fnXml);
      if ( ret >= 0 ) {
	return ret;
      }
    }

    // default config file
    // todo - how to support on win32? %HOMEPATH%?
    if ( getenv("HOME") ) {
      std::string fnHome = getenv("HOME");
      fnHome += "/.mcpe_viz/mcpe_viz.xml";
      ret = doParseXml( fnHome );
      if ( ret >= 0 ) {
	return ret;
      }
    }

    // same dir as exec
    fn = dirExec;
    fn += "/mcpe_viz.xml";
    ret = doParseXml( fn );
    if ( ret >= 0 ) {
      return ret;
    }

    // local dir
    fn = "./mcpe_viz.xml";
    ret = doParseXml( fn );
    if ( ret >= 0 ) {
      return ret;
    }

    fprintf(stderr,"ERROR: Did not find a valid XML file\n");
    return -1;
  }
    

    
  void print_usage(const char* fn) {
    fprintf(stderr,"Usage:\n\n");
    fprintf(stderr,"  %s [required parameters] [options]\n\n",fn);
    fprintf(stderr,"Required Parameters:\n"
	    "  --db dir                 Directory which holds world files (level.dat is in this dir)\n"
	    "  --out fn-part            Filename base for output file(s)\n"
	    "\n"
	    );
    fprintf(stderr,"Options:\n"
	    //"  --detail                 Log extensive details about the world to the log file\n"
	    "  --html                   Create html and javascript files to use as a fancy viewer\n"
	    "  --html-most              Create html, javascript, and most image files to use as a fancy viewer\n"
	    "  --html-all               Create html, javascript, and *all* image files to use as a fancy viewer\n"
	    //"  --dir-temp dir           Directory for temp files (useful for --slices, use a fast, local directory)\n"
	    "  --tiles[=tilew,tileh]    Create tiles in subdirectory tiles/ (useful for LARGE worlds)\n"
	    "\n"
	    "  --hide-top=did,bid       Hide a block from top block (did=dimension id, bid=block id)\n"
	    "  --force-top=did,bid      Force a block to top block (did=dimension id, bid=block id)\n"
	    "\n"
	    "  (note: [=did] is optional dimension-id - if not specified, do all dimensions; 0=Overworld; 1=Nether)\n"
	    "  --grid[=did]             Display chunk grid on top of images\n"
	    "\n"
	    "  --all-image[=did]        Create all image types\n"
	    "  --biome[=did]            Create a biome map image\n"
	    "  --grass[=did]            Create a grass color map image\n"
	    "  --height-col[=did]       Create a height column map image (red is below sea; gray is sea; green is above sea)\n"
	    "  --height-col-gs[=did]    Create a height column map image (grayscale)\n"
	    "  --blocklight[=did]       Create a block light map image\n"
	    "  --skylight[=did]         Create a sky light map image\n"
	    "\n"
	    "  --slices[=did]           Create slices (one image for each layer)\n"
	    "  --movie[=did]            Create movie of layers\n"
	    "  --movie-dim x,y,w,h      Integers describing the bounds of the movie (UL X, UL Y, WIDTH, HEIGHT)\n"
	    "\n"
	    "  --xml fn                 XML file containing data definitions\n"
	    "  --log fn                 Send log to a file\n"
	    "\n"
	    "  --no-force-geojson       Don't load geojson in html because we are going to use a web server (or Firefox)\n"
	    "\n"
	    "  --verbose                verbose output\n"
	    "  --quiet                  supress normal output, continue to output warning and error messages\n"
	    "  --help                   this info\n"
	    );
  }

  int parseDimIdOptArg(const char* arg) {
    int did = kDoOutputAll;
    if ( arg ) {
      did = atoi(arg);

      // sanity check
      if ( did >= kDimIdOverworld && did < kDimIdCount ) {
	// all is good
      } else {
	fprintf(stderr,"ERROR: Invalid dimension-id supplied (%d), defaulting to Overworld only\n", did);
	did=kDimIdOverworld;
      }
    } else {
      // if no arg, we want output for all dimensions
    }
    return did;
  }
    
  int parse_args ( int argc, char **argv ) {

    static struct option longoptlist[] = {
      {"db", required_argument, NULL, 'D'},
      {"out", required_argument, NULL, 'O'},

      {"xml", required_argument, NULL, 'X'},
      {"log", required_argument, NULL, 'L'},

      {"detail", no_argument, NULL, '@'},

      {"hide-top", required_argument, NULL, 'H'},
      {"force-top", required_argument, NULL, 'F'},
	
      {"all-image", optional_argument, NULL, 'A'},
      {"biome", optional_argument, NULL, 'B'},
      {"grass", optional_argument, NULL, 'g'},
      {"height-col", optional_argument, NULL, 'd'},
      {"height-col-gs", optional_argument, NULL, '#'},
      {"blocklight", optional_argument, NULL, 'b'},
      {"skylight", optional_argument, NULL, 's'},
    
      {"slices", optional_argument, NULL, '('},

      {"movie", optional_argument, NULL, 'M'},
      {"movie-dim", required_argument, NULL, '*'},
	
      {"grid", optional_argument, NULL, 'G'},

      {"html", no_argument, NULL, ')'},
      {"html-most", no_argument, NULL, '='},
      {"html-all", no_argument, NULL, '_'},
      {"no-force-geojson", no_argument, NULL, ':'},

      {"tiles", optional_argument, NULL, '['},

      {"shortrun", no_argument, NULL, '$'}, // this is just for testing
      {"colortest", no_argument, NULL, '!'}, // this is just for testing
    
      {"verbose", no_argument, NULL, 'v'},
      {"quiet", no_argument, NULL, 'q'},
      {"help", no_argument, NULL, 'h'},
      {NULL, no_argument, NULL, 0}
    };

    int option_index = 0;
    int optc;
    int errct=0;

    control.init();

    while ((optc = getopt_long_only (argc, argv, "", longoptlist, &option_index)) != -1) {
      switch (optc) {
      case 'O':
	control.fnOutputBase = optarg;
	break;      
      case 'X':
	control.fnXml = optarg;
	break;      
      case 'L':
	control.fnLog = optarg;
	break;      
      case 'D':
	control.dirLeveldb = optarg;
	break;
      case '@':
	control.doDetailParseFlag = true;
	break;

      case 'H':
	{
	  bool pass = false;
	  int32_t dimId, blockId;
	  if ( sscanf(optarg,"%d,0x%x", &dimId, &blockId) == 2 ) {
	    pass = true;
	  }
	  else if ( sscanf(optarg,"%d,%d", &dimId, &blockId) == 2 ) {
	    pass = true;
	  }

	  if ( pass ) {
	    // check dimId
	    if ( dimId < kDimIdOverworld || dimId >= kDimIdCount ) {
	      pass = false;
	    }
	    if ( pass ) {
	      world.chunkList[dimId].blockHideList.push_back(blockId);
	    }
	  }

	  if ( ! pass ) {
	    fprintf(stderr,"ERROR: Failed to parse --hide-top %s\n",optarg);
	    errct++;
	  }
	}
	break;
	  
      case 'F':
	{
	  bool pass = false;
	  int32_t dimId, blockId;
	  if ( sscanf(optarg,"%d,0x%x", &dimId, &blockId) == 2 ) {
	    pass = true;
	  }
	  else if ( sscanf(optarg,"%d,%d", &dimId, &blockId) == 2 ) {
	    pass = true;
	  }

	  if ( pass ) {
	    // check dimId
	    if ( dimId < kDimIdOverworld || dimId >= kDimIdCount ) {
	      pass = false;
	    }
	    if ( pass ) {
	      world.chunkList[dimId].blockForceTopList.push_back(blockId);
	    }
	  }

	  if ( ! pass ) {
	    fprintf(stderr,"ERROR: Failed to parse --force-top %s\n",optarg);
	    errct++;
	  }
	}
	break;
	  
      case 'G':
	control.doGrid = parseDimIdOptArg(optarg);
	break;

      case ')':
	control.doHtml = true;
	break;

      case '[':
	control.doTiles = true;
	{
	  int tw, th;
	  if ( optarg ) {
	    if ( sscanf(optarg,"%d,%d",&tw,&th) == 2 ) {
	      control.tileWidth = tw;
	      control.tileHeight = th;
	      fprintf(stderr,"Overriding tile dimensions: %d x %d\n", control.tileWidth, control.tileHeight);
	    }
	  }
	}
	break;
	
      case '=':
	// html most
	control.doHtml = true;
	control.doImageBiome = 
	  control.doImageGrass = 
	  control.doImageHeightCol = 
	  control.doImageHeightColGrayscale =
	  control.doImageLightBlock = 
	  control.doImageLightSky =
	  control.doImageSlimeChunks =
	  kDoOutputAll;
	break;

      case '_':
	// html all
	control.doHtml = true;
	control.doImageBiome = 
	  control.doImageGrass = 
	  control.doImageHeightCol = 
	  control.doImageHeightColGrayscale =
	  control.doImageLightBlock = 
	  control.doImageLightSky =
	  control.doImageSlimeChunks =
	  kDoOutputAll;
	control.doSlices = kDoOutputAll;
	break;

      case ':':
	control.noForceGeoJSONFlag = true;
	break;
	  
      case 'B':
	control.doImageBiome = parseDimIdOptArg(optarg);
	break;
      case 'g':
	control.doImageGrass = parseDimIdOptArg(optarg);
	break;
      case 'd':
	control.doImageHeightCol = parseDimIdOptArg(optarg);
	break;
      case '#':
	control.doImageHeightColGrayscale = parseDimIdOptArg(optarg);
	break;
      case 'b':
	control.doImageLightBlock = parseDimIdOptArg(optarg);
	break;
      case 's':
	control.doImageLightSky = parseDimIdOptArg(optarg);
	break;

	// todobig option for slime chunks
	
      case 'A':
	control.doImageBiome = 
	  control.doImageGrass = 
	  control.doImageHeightCol = 
	  control.doImageHeightColGrayscale =
	  control.doImageLightBlock = 
	  control.doImageLightSky =
	  control.doImageSlimeChunks =
	  parseDimIdOptArg(optarg);
	break;
      
      case '(':
	control.doSlices = parseDimIdOptArg(optarg);
	break;
      case 'M':
	control.doMovie = parseDimIdOptArg(optarg);
	break;
      case '*':
	// movie dimensions
	if ( sscanf(optarg,"%d,%d,%d,%d", &control.movieX, &control.movieY, &control.movieW, &control.movieH) == 4 ) {
	  // good
	} else {
	  fprintf(stderr,"ERROR: Failed to parse --movie-dim\n");
	  errct++;
	}
	break;

      case '$':
	control.shortRunFlag = true;
	break;
      case '!':
	control.colorTestFlag = true;
	break;
      
      case 'v': 
	control.verboseFlag = true; 
	break;
      case 'q':
	control.quietFlag = true;
	logger.setLogLevelMask(kLogQuiet);
	break;

	/* Usage */
      default:
	fprintf(stderr,"ERROR: Unrecognized option: '%c'\n",optc);
      case 'h':
	return -1;
      }
    }

    // verify/test args
    if ( control.dirLeveldb.length() <= 0 ) {
      errct++;
      fprintf(stderr,"ERROR: Must specify --db\n");
    }
    if ( control.fnOutputBase.length() <= 0 ) {
      errct++;
      fprintf(stderr,"ERROR: Must specify --out\n");
    }

    if ( errct <= 0 ) {
      control.setupOutput();
    }
    
    return errct;
  }
  
  int init(int argc, char** argv) {
    int ret;

    ret = parse_args(argc, argv);
    if (ret != 0) {
      print_usage(basename(argv[0]));
      return ret;
    }
    
    ret = parseXml();
    if ( ret != 0 ) {
      fprintf(stderr,"ERROR: Failed to parse XML file.  Exiting...\n");
      fprintf(stderr,"** Hint: Make sure that mcpe_viz.xml is in any of: current dir, exec dir, ~/.mcpe_viz/\n");
      return -1;
    }
    
    parseConfigFile();
    
    ret = parseLevelFile(std::string(control.dirLeveldb + "/level.dat"));
    if ( ret != 0 ) {
      fprintf(stderr,"ERROR: Failed to parse level.dat file.  Exiting...\n");
      fprintf(stderr,"** Hint: --db must point to the dir which contains level.dat\n");
      return -1;
    }
    
    ret = parseLevelName(std::string(control.dirLeveldb + "/levelname.txt"));
    if ( ret != 0 ) {
      fprintf(stderr,"WARNING: Failed to parse levelname.txt file.\n");
      fprintf(stderr,"** Hint: --db must point to the dir which contains levelname.txt\n");
    }
    
    makePalettes();

    return 0;
  }
  
}  // namespace mcpe_viz


  
int main ( int argc, char **argv ) {

  fprintf(stderr,"%s\n", mcpe_viz::mcpe_viz_version.c_str());

  mcpe_viz::dirExec = dirname(argv[0]);

  int ret = mcpe_viz::init(argc,argv);
  if ( ret != 0 ) {
    return -1;
  }

  mcpe_viz::world.dbOpen(std::string(mcpe_viz::control.dirLeveldb));

  // todobig - we must do this, for now - we could get clever about this later
  // todobig - we could call this deepParseDb() and only do it if the user wanted it
  if ( true || mcpe_viz::control.doDetailParseFlag ) {
    mcpe_viz::world.parseDb();
  }

  mcpe_viz::world.doOutput();

  mcpe_viz::world.dbClose();
  
  fprintf(stderr,"Done.\n");

  return 0;
}
