/*
  Minecraft Pocket Edition (MCPE) World File Visualization & Reporting Tool
  (c) Plethora777, 2015.9.26

  GPL'ed code - see LICENSE

  Requires Mojang's modified LevelDB library! (see README.md for details)
  Requires libnbt++ (see README.md for details)

  To build it, use cmake


  todobig

  * xml <block> needs to be expanded to allow block_data values so that we can get complete info (e.g. wood types)
  -- block[id][block_data] = foo
  -- compare wiki re block info for mc vs mcpe
  -- same for items + entities
  -- THIS: perhaps <block ...><variant .../></block>?

  * it takes a loooong time to do a full html - 47 minutes?!
  -- try to do it as we parse the chunks?

  * parse potions etc in inventories etc

  * join chests for geojson? (pairchest) - would require that we wait to toGeoJSON until we parse all chunks
  -- put ALL chests in a map<x,y,z>; go through list of chests and put PairChest in matching sets and mark all as unprocessed; go thru list and do all, if PairChest mark both as done

  * web: popover does not show in fullscreen
  * web: how to handle multiple features at the same point? (e.g. stacked chests)
  * web: why does click on point not always work?

  * see about using block data values to get details of blocks (e.g. wood type, leaf type, etc)

  * determine proper xml entries for missing items + entities + blocks
  -- wither skeleton?

  * see if there is interesting info re colors for overview map: http://minecraft.gamepedia.com/Map_item_format

  * time to split this code up into separate files?

  * option to name output files w/ world name

  * use "solid" info from block info to do something? could it fix the light map?

  * update block xml w/ transparency info - bool or int? or just solid/nonsolid?
  * use this transparency (or something else? e.g. spawnable flag?) to determine which block is the top block for purposes of the light map
  * go and look at wiki to see the type of info that is stored per block

  * produce another light map that shows areas that ARE spawnable? (e.g. use this to make an area 100% mob spawn proof)

  * convert all printf-style stuff to streams

  ** openlayers ui
  ** ability to hover over a pixel and get info (e.g. "Jungle Biome - Watermelon @ (X,Z,Y)"); switch image layers
  ** vector: biomes (or raster that is not displayed?)
  ** vector: height? (or raster that is not displayed?)

  todo

  ** cmdline options:
  save set of slices
  save a particular slice
  draw text on slice files (e.g. Y)
  separate logfiles for overworld + nether + unknown
  options to reduce log file detail

  ** maps/instructions to get from point A (e.g. spawn) to biome type X (in blocks but also in landmark form: over 2 seas left at the birch forest etc); same for items and entities

  ** map icons for interesting things (e.g. player, remote player, villagers, item X, entity X, etc)
  

  todo win32/win64 build

  * immediate crash if using -O2?
  * leveldb close() issue -- link fails on missing stream stuff
  * leveldb -O2 build issue -- link fails on missing stream stuff
  * leveldb fread_nolock issue -- link fails; forcing msvcrXXX.a crashes on windows
  * log file: change end line to CRLF?

  ** osx build?

  */

#include <stdio.h>
#include <libgen.h>
#include <map>
#include <vector>
#include <png.h>
#include <libxml/xmlreader.h>
#include <getopt.h>
#include <sys/stat.h>
#include "leveldb/db.h"
#include "leveldb/zlib_compressor.h"

// nbt lib stuff
#include "io/stream_reader.h"
//#ifdef NBT_HAVE_ZLIB
//#include "io/izlibstream.h"
//#endif
#include "nbt_tags.h"
#include <iostream>
#include <fstream>
#include <sstream>


#ifndef htobe32
// note: this is only used on win32/win64 builds
// todo - doesn't check host endianness
inline int32_t htobe32(const int32_t src) {
  int32_t dst;
  const char* ps = (char*)&src;
  char* pd = (char*)&dst;
  pd[0]=ps[3];
  pd[1]=ps[2];
  pd[2]=ps[1];
  pd[3]=ps[0];
  return dst;
}
#endif

#ifndef be32toh
// note: this is only used on win32/win64 builds
// todo - doesn't check host endianness
inline int32_t be32toh(const int32_t src) {
  int32_t dst;
  const char* ps = (char*)&src;
  char* pd = (char*)&dst;
  pd[0]=ps[3];
  pd[1]=ps[2];
  pd[2]=ps[1];
  pd[3]=ps[0];
  return dst;
}
#endif

// help vars and funcs
const char* dirExec = nullptr;

// these hacks work around "const char*" problems
std::string mybasename( const std::string fn ) {
  char tmpstring[1025];
  memset(tmpstring,0,1025);
  strncpy(tmpstring,fn.c_str(),1024);
  std::string ret( basename(tmpstring) );
  return ret;
}
std::string mydirname( const std::string fn ) {
  char tmpstring[1025];
  memset(tmpstring,0,1025);
  strncpy(tmpstring,fn.c_str(),1024);
  std::string ret( dirname(tmpstring) );
  return ret;
}

// hacky file copying funcs
typedef std::vector< std::pair<std::string, std::string> > StringReplacementList;
int copyFileWithStringReplacement ( const std::string fnSrc, const std::string fnDest,
				    const StringReplacementList& replaceStrings ) {
  char buf[1025];

  fprintf(stderr,"  copyFileWithStringReplacement src=%s dest=%s\n", fnSrc.c_str(), fnDest.c_str());

  FILE *fpsrc = fopen(fnSrc.c_str(),"r");
  if ( ! fpsrc ) {
    fprintf(stderr,"ERROR: copyFileWithStringReplacement failed to open src (%s)\n", fnSrc.c_str());
    return -1;
  }
  FILE *fpdest = fopen(fnDest.c_str(),"w");
  if ( ! fpdest ){
    fprintf(stderr,"ERROR: copyFileWithStringReplacement failed to open dest (%s)\n", fnDest.c_str());
    fclose(fpsrc);
    return -1;
  }

  while ( ! feof(fpsrc) ) {
    memset(buf,0,1025);
    if ( fgets(buf, 1024, fpsrc) ) { 

    // look for replacement string
    bool doneFlag = false;
    for ( const auto& it : replaceStrings ) {
      char* p = strstr(buf,it.first.c_str());
      if ( p ) {
	std::string sbefore(buf,(p-buf));
	std::string safter(&p[it.first.size()]);
	if ( sbefore.size() > 0 ) {
	  fputs(sbefore.c_str(), fpdest);
	}
	fputs(it.second.c_str(), fpdest);
	if ( safter.size() > 0 ) {
	  fputs(safter.c_str(),fpdest);
	}
	doneFlag = true;
	break;
      }
    }
    if ( ! doneFlag ) {
      fputs(buf,fpdest);
    }
  }
  }
  fclose(fpsrc);
  fclose(fpdest);
  return 0;
}

int copyFile ( const std::string fnSrc, const std::string fnDest ) {
  char buf[1025];
  memset(buf,0,1025);

  fprintf(stderr,"  copyFile src=%s dest=%s\n", fnSrc.c_str(), fnDest.c_str());
  
  FILE *fpsrc = fopen(fnSrc.c_str(),"r");
  if ( ! fpsrc ) {
    fprintf(stderr,"ERROR: copyFile failed to open src (%s)\n", fnSrc.c_str());
    return -1;
  }
  FILE *fpdest = fopen(fnDest.c_str(),"w");
  if ( ! fpdest ){
    fprintf(stderr,"ERROR: copyFile failed to open dest (%s)\n", fnDest.c_str());
    fclose(fpsrc);
    return -1;
  }

  while ( ! feof(fpsrc) ) {
    if ( fgets(buf, 1024, fpsrc) ) {
      fputs(buf,fpdest);
    }
  }
  fclose(fpsrc);
  fclose(fpdest);
  return 0;
}


namespace mcpe_viz {
  namespace {

    static const std::string version("mcpe_viz v0.0.5 by Plethora777");

    std::vector<std::string> listGeoJSON;

    int32_t playerPositionImageX=0, playerPositionImageY=0;

    enum OutputType : int32_t {
      kDoOutputNone = -2,
      kDoOutputAll = -1
    };

    // dimensions
    enum DimensionType : int32_t {
      kDimIdOverworld = 0,
      kDimIdNether = 1,
      kDimIdCount = 2
    };

    const int32_t kColorDefault = 0xff00ff;
    
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
      
      // per dimension filenames
      std::string fnLayerTop[kDimIdCount];
      std::string fnLayerBiome[kDimIdCount];
      std::string fnLayerHeight[kDimIdCount];
      std::string fnLayerHeightGrayscale[kDimIdCount];
      std::string fnLayerBlockLight[kDimIdCount];
      std::string fnLayerSkyLight[kDimIdCount];
      std::string fnLayerGrass[kDimIdCount];
      std::string fnLayerRaw[kDimIdCount][128];
      
      bool doDetailParseFlag;
      int doMovie;
      int doSlices;
      int doGrid;
      int doHtml;
      int doImageBiome;
      int doImageGrass;
      int doImageHeightCol;
      int doImageHeightColGrayscale;
      // todo height image using topBlockY
      int doImageLightBlock;
      int doImageLightSky;
      bool shortRunFlag;
      bool verboseFlag;
      bool quietFlag;
      int movieX, movieY, movieW, movieH;

      bool fpLogNeedCloseFlag;
      FILE *fpLog;
      FILE *fpGeoJSON;

      Control() {
	init();
      }
      ~Control() {
	if ( fpLogNeedCloseFlag ) {
	  if ( fpLog != nullptr ) {
	    fclose(fpLog);
	  }
	}
	if ( fpGeoJSON != nullptr ) {
	  // put the geojson preamble stuff
	  fprintf(fpGeoJSON,
		  "{\n"
		  "\t\"type\": \"FeatureCollection\",\n"
		  // todo - correct way to specify this?
		  "\t\"crs\": { \"type\": \"name\", \"properties\": { \"name\": \"mcpe_viz-image\" } },\n"
		  "\n"
		  "\t\"features\": [\n"
		  );
	  int i = listGeoJSON.size();
	  for ( const auto& it: listGeoJSON ) {
	    fprintf(fpGeoJSON,"%s",it.c_str());
	    if ( --i > 0 ) {
	      fputc(',',fpGeoJSON);
	    }
	    fputc('\n',fpGeoJSON);
	  }
	  fprintf(fpGeoJSON,"]\n}\n");
	  fclose(fpGeoJSON);
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
	doImageBiome = kDoOutputNone;
	doImageGrass = kDoOutputNone;
	doImageHeightCol = kDoOutputNone;
	doImageHeightColGrayscale = kDoOutputNone;
	doImageLightBlock = kDoOutputNone;
	doImageLightSky = kDoOutputNone;
	
	shortRunFlag = false;
	verboseFlag = false;
	quietFlag = false;
	movieX = movieY = movieW = movieH = 0;
	fpLogNeedCloseFlag = false;
	fpLog = stdout;
	fpGeoJSON = nullptr;

	for (int did=0; did < kDimIdCount; did++) {
	  fnLayerTop[did] = "";
	  fnLayerBiome[did] = "";
	  fnLayerHeight[did] = "";
	  fnLayerHeightGrayscale[did] = "";
	  fnLayerBlockLight[did] = "";
	  fnLayerSkyLight[did] = "";
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

	// todo - user option for filename?
	fnGeoJSON = fnOutputBase + ".geojson";
	fpGeoJSON = fopen(fnGeoJSON.c_str(), "w");
	if ( ! fpGeoJSON ) {
	  fprintf(stderr,"ERROR: Failed to create GeoJSON output file (%s).  Reverting to stdout...\n", fnGeoJSON.c_str());
	  // todo - should be a fatal error?
	}

	listGeoJSON.clear();

	fnHtml = fnOutputBase + ".html";
	fnJs = fnOutputBase + ".js";
      }
    };

    Control control;

    
    // nbt parsing helpers
    int globalNbtListNumber=0;
    int globalNbtCompoundNumber=0;

    // output image types
    enum ImageModeType : int32_t {
      kImageModeTerrain = 0,
      kImageModeBiome = 1,
      kImageModeGrass = 2,
      kImageModeHeightCol = 3,
      kImageModeHeightColGrayscale = 4,
      kImageModeBlockLight = 5,
      kImageModeSkyLight = 6
    };

    class StandardColorInfo {
    public:
      std::string name;
      int32_t color;
      StandardColorInfo() {
	name = "(unknown)";
	setColor(kColorDefault);
      }
      StandardColorInfo& setColor(int32_t rgb) {
	// note: we DO NOT convert color because we'll probably need to adjust it
	color = rgb;
	return *this;
      }
    };
    StandardColorInfo standardColorInfo[16];

    // these are read from level.dat
    int32_t worldSpawnX = 0;
    int32_t worldSpawnY = 0;
    int32_t worldSpawnZ = 0;

    // palettes
    int32_t palRedBlackGreen[256];

    
    int32_t myParseInt32(const char* p, int startByte) {
      int32_t ret;
      memcpy(&ret, &p[startByte], 4);
      return ret;
    }

    int8_t myParseInt8(const char* p, int startByte) {
      return (p[startByte] & 0xff);
    }

    // note: super super old hsl2rgb code; origin unknown
    double _hue_to_rgb(double m1, double m2, double h) {
      while (h < 1.0) { h += 1.0; }
      while (h > 1.0) { h -= 1.0; }
      if ((h * 6.0) < 1.0) {
	return m1+(m2-m1)*h*6.0;
      }
      if ((h * 2.0) < 1.0) {
	return m2;
      }
      if ((h * 3.0) < 2.0) {
	return m1+(m2-m1)*(2.0/3.0 - h)*6.0;
      }
      return m1;
    }

    int32_t _clamp(int32_t v, int32_t minv, int32_t maxv) {
      if ( v < minv ) return minv;
      if ( v > maxv ) return maxv;
      return v;
    }
    
    int hsl2rgb ( double h, double s, double l, int32_t &r, int32_t &g, int32_t &b ) {
      double m2;
      if (l <= 0.5) {
	m2 = l * (s+1.0);
      } else {
	m2 = l + s - l * s;
      }
      double m1 = l * 2.0 - m2;
      double tr = _hue_to_rgb(m1,m2, h + 1.0/3.0);
      double tg = _hue_to_rgb(m1,m2, h);
      double tb = _hue_to_rgb(m1,m2, h - 1.0/3.0);
      r = _clamp((int)(tr * 255.0), 0, 255);
      g = _clamp((int)(tg * 255.0), 0, 255);
      b = _clamp((int)(tb * 255.0), 0, 255);
      return 0;
    }

    int makeHslRamp ( int32_t *pal, int32_t start, int32_t stop, double h1, double h2, double s1, double s2, double l1, double l2 ) {
      double steps = stop-start+1;
      double dh = (h2 - h1) / steps;
      double ds = (s2 - s1) / steps;
      double dl = (l2 - l1) / steps;
      double h=h1,s=s1,l=l1;
      int32_t r,g,b;
      for ( int i=start; i<=stop; i++ ) {
	hsl2rgb(h,s,l, r,g,b);
	int32_t c = ((r & 0xff) << 16) | ((g & 0xff) << 8) | (b & 0xff);
	pal[i] = c;
	h+=dh;
	s+=ds;
	l+=dl;
      }
      return 0;
    }
    
    void makePalettes() {
      // create red-green ramp; red to black and then black to green
      makeHslRamp(palRedBlackGreen,  0,  63, 0.0,0.0, 0.9,0.9, 0.8,0.1);
      makeHslRamp(palRedBlackGreen, 64, 127, 0.4,0.4, 0.9,0.9, 0.1,0.8);
      // force 63 (sea level) to gray
      palRedBlackGreen[63]=0x202020;
      // fill 128..255 with purple (we should never see this color)
      for (int i=128; i < 256; i++) {
	palRedBlackGreen[i] = kColorDefault;
      }

      // convert palette
      for (int i=0; i < 256; i++) {
	palRedBlackGreen[i] = htobe32(palRedBlackGreen[i]);
      }
    }

    
    inline int _calcOffset(int x, int z, int y) {
      // todo - correct calc here? shouldn't it be z*16?!
      return (((x*16) + z)*128) + y;
    }
    
    inline int _calcOffset(int x, int z) {
      // todo - correct calc here? shouldn't it be z*16?!
      return (x*16) + z;
    }

    uint8_t getBlockId(const char* p, int x, int z, int y) {
      return (p[_calcOffset(x,z,y)] & 0xff);
    }

    uint8_t getBlockData(const char* p, int x, int z, int y) {
      int off =  _calcOffset(x,z,y);
      int off2 = off / 2;
      int mod2 = off % 2;
      int v = p[32768 + off2];
      if ( mod2 == 0 ) {
	return v & 0x0f;
      } else {
	return (v & 0xf0) >> 4;
      }
    }

    // todo - this appears to actually be a block opacity value? (e.g. glass is 0xf, water is semi (0xc) and an opaque block is 0x0)
    uint8_t getBlockSkyLight(const char* p, int x, int z, int y) {
      int off =  _calcOffset(x,z,y);
      int off2 = off / 2;
      int mod2 = off % 2;
      int v = p[32768 + 16384 + off2];
      if ( mod2 == 0 ) {
	return v & 0x0f;
      } else {
	return (v & 0xf0) >> 4;
      }
    }

    // todo - block light is light value from torches et al -- super cool looking as an image, but it looks like block light is probably stored in air blocks which are above top block
    uint8_t getBlockBlockLight(const char* p, int x, int z, int y) {
      int off =  _calcOffset(x,z,y);
      int off2 = off / 2;
      int mod2 = off % 2;
      int v = p[32768 + 16384 + 16384 + off2];
      if ( mod2 == 0 ) {
	return v & 0x0f;
      } else {
	return (v & 0xf0) >> 4;
      }
    }

    // todo - height of top *solid* block? (e.g. a glass block will NOT be the top block here)
    uint8_t getColData1(const char *buf, int x, int z) {
      int off = _calcOffset(x,z);
      int8_t v = buf[32768 + 16384 + 16384 + 16384 + off];
      return v;
    }

    // todo - this is 4-bytes: lsb is biome, the high 3-bytes are RGB grass color
    int32_t getColData2(const char *buf, int x, int z) {
      int off = _calcOffset(x,z) * 4;
      int32_t v;
      memcpy(&v,&buf[32768 + 16384 + 16384 + 16384 + 256 + off],4);
      return v;
    }
    
    
    class BlockInfo {
    public:
      std::string name;
      int32_t color;
      bool lookupColorFlag;
      int32_t lookupColorOffset;
      bool colorSetFlag;
      bool solidFlag;
      int colorSetNeedCount;
      BlockInfo() {
	name = "(unknown)";
	setColor(kColorDefault); // purple
	lookupColorFlag = false;
	lookupColorOffset = 0;
	solidFlag = true;
	colorSetFlag = false;
	colorSetNeedCount = 0;
      }
      BlockInfo& setName(const std::string s) {
	name = std::string(s);
	return *this;
      }
      BlockInfo& setColor(int32_t rgb) {
	// note: we convert color storage to big endian so that we can memcpy when creating images
	color = htobe32(rgb);
	colorSetFlag = true;
	return *this;
      }
      BlockInfo& setLookupColorFlag(bool f) {
	lookupColorFlag = f;
	return *this;
      }
      BlockInfo& setLookupColorOffset(int32_t o) {
	lookupColorOffset = o;
	return *this;
      }
      BlockInfo& setSolidFlag(bool f) {
	solidFlag = f;
	return *this;
      }
      bool isSolid() { return solidFlag; }
    };

    BlockInfo blockInfoList[256];
    

    class ItemInfo {
    public:
      std::string name;
      ItemInfo(const char* n) {
	setName(n);
      }
      ItemInfo& setName (const std::string s) {
	name = std::string(s);
	return *this;
      }
    };

    std::map<int, std::unique_ptr<ItemInfo> > itemInfoList;
    bool has_key(const std::map<int, std::unique_ptr<ItemInfo> > &m, int k) {
      return  m.find(k) != m.end();
    }


    class EntityInfo {
    public:
      std::string name;
      EntityInfo(const char* n) {
	setName(n);
      }
      EntityInfo& setName (const std::string s) {
	name = std::string(s);
	return *this;
      }
    };

    std::map<int, std::unique_ptr<EntityInfo> > entityInfoList;
    bool has_key(const std::map<int, std::unique_ptr<EntityInfo> > &m, int k) {
      return  m.find(k) != m.end();
    }


    class BiomeInfo {
    public:
      std::string name;
      int32_t color;
      bool colorSetFlag;
      BiomeInfo(const char* n) {
	setName(n);
	setColor(kColorDefault);
	colorSetFlag = false;
      }
      BiomeInfo(const char* n, int32_t rgb) {
	setName(n);
	setColor(rgb);
      }
      BiomeInfo& setName (const std::string s) {
	name = std::string(s);
	return *this;
      }
      BiomeInfo& setColor(int32_t rgb) {
	// note: we convert color storage to big endian so that we can memcpy when creating images
	color = htobe32(rgb);
	colorSetFlag=true;
	return *this;
      }
    };

    std::map<int, std::unique_ptr<BiomeInfo> > biomeInfoList;
    bool has_key(const std::map<int, std::unique_ptr<BiomeInfo> > &m, int k) {
      return  m.find(k) != m.end();
    }


    class EnchantmentInfo {
    public:
      std::string name;
      std::string officialName;
      EnchantmentInfo(const char* n) {
	setName(n);
	officialName="";
      }
      EnchantmentInfo& setName (const std::string s) {
	name = std::string(s);
	return *this;
      }
      EnchantmentInfo& setOfficialName (const std::string s) {
	officialName = std::string(s);
	return *this;
      }
    };

    std::map<int, std::unique_ptr<EnchantmentInfo> > enchantmentInfoList;
    bool has_key(const std::map<int, std::unique_ptr<EnchantmentInfo> > &m, int k) {
      return  m.find(k) != m.end();
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
      ChunkData(int32_t cx, int32_t cz,
	      const uint8_t* b, const uint8_t* d, const uint32_t* xgrassAndBiome, const uint8_t* tby, const uint8_t* height,
	      const uint8_t* light) {
	chunkX=cx;
	chunkZ=cz;
	memcpy(blocks, b, 16*16);
	memcpy(data, d, 16*16);
	memcpy(grassAndBiome, xgrassAndBiome, 16*16*sizeof(uint32_t));
	memcpy(topBlockY, tby, 16*16);
	memcpy(heightCol, height, 16*16);
	memcpy(topLight, light, 16*16);
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
      int chunkCount = 0;

      int histoChunkType[256];
      int histoGlobalBiome[256];

      std::vector<int> blockForceTopList;
      std::vector<int> blockHideList;
      
      ChunkDataList() {
	name = "(UNKNOWN)";
	dimId = -1;
	chunkBoundsValid = false;
	memset(histoChunkType,0,256*sizeof(int));
	memset(histoGlobalBiome,0,256*sizeof(int));
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
      
      void putChunk ( int32_t chunkX, int32_t chunkZ,
		      const uint8_t* topBlock, const uint8_t* blockData,
		      const uint32_t* grassAndBiome, const uint8_t* topBlockY, const uint8_t* height, const uint8_t* topLight) {
	chunkCount++;
	
	minChunkX = std::min(minChunkX, chunkX);
	minChunkZ = std::min(minChunkZ, chunkZ);
	
	maxChunkX = std::max(maxChunkX, chunkX);
	maxChunkZ = std::max(maxChunkZ, chunkZ);

	std::unique_ptr<ChunkData> tmp(new ChunkData(chunkX, chunkZ, topBlock, blockData, grassAndBiome, topBlockY, height, topLight));
	list.push_back( std::move(tmp) );
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
	fprintf(control.fpLog,"\n%s Statistics:\n", name.c_str());
	fprintf(control.fpLog,"chunk-count: %d\n", chunkCount);
	fprintf(control.fpLog,"Min-dim:  %d %d\n", minChunkX, minChunkZ);
	fprintf(control.fpLog,"Max-dim:  %d %d\n", maxChunkX, maxChunkZ);
	int32_t dx = (maxChunkX-minChunkX+1);
	int32_t dz = (maxChunkZ-minChunkZ+1);
	fprintf(control.fpLog,"diff-dim: %d %d\n", dx, dz);
	fprintf(control.fpLog,"pixels:   %d %d\n", dx*16, dz*16);

	fprintf(control.fpLog,"\nGlobal Chunk Type Histogram:\n");
	for (int i=0; i < 256; i++) {
	  if ( histoChunkType[i] > 0 ) {
	    fprintf(control.fpLog,"hg-chunktype: %02x %6d\n", i, histoChunkType[i]);
	  }
	}

	fprintf(control.fpLog,"\nGlobal Biome Histogram:\n");
	for (int i=0; i < 256; i++) {
	  if ( histoGlobalBiome[i] > 0 ) {
	    fprintf(control.fpLog,"hg-globalbiome: %02x %6d\n", i, histoGlobalBiome[i]);
	  }
	}
      }

      int outputPNG(const std::string fname, uint8_t* buf, int width, int height) {
	const char* filename = fname.c_str();
	png_bytep *row_pointers;
      
	FILE *fp = fopen(filename, "wb");
	if(!fp) {
	  fprintf(stderr,"ERROR: Failed to open output file (%s)\n", filename);
	  return -1;
	}
      
	png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png) abort();
      
	png_infop info = png_create_info_struct(png);
	if (!info) abort();
      
	if (setjmp(png_jmpbuf(png))) abort();
      
	png_init_io(png, fp);
      
	// Output is 8bit depth, RGB format.
	png_set_IHDR(
		     png,
		     info,
		     width, height,
		     8,
		     PNG_COLOR_TYPE_RGB,
		     PNG_INTERLACE_NONE,
		     PNG_COMPRESSION_TYPE_DEFAULT,
		     PNG_FILTER_TYPE_DEFAULT
		     );
	png_write_info(png, info);
      
	// To remove the alpha channel for PNG_COLOR_TYPE_RGB format,
	// Use png_set_filler().
	//png_set_filler(png, 0, PNG_FILLER_AFTER);

	row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * height);
	for(int y = 0; y < height; y++) {
	  row_pointers[y] = &buf[ y * width * 3 ];
	}
      
	png_write_image(png, row_pointers);
	png_write_end(png, NULL);

	png_destroy_write_struct(&png, &info);

	free(row_pointers);
      
	fclose(fp);

	return 0;
      }

      void generateImage(const std::string fname, const ImageModeType imageMode = kImageModeTerrain) {
	const int32_t chunkOffsetX = -minChunkX;
	const int32_t chunkOffsetZ = -minChunkZ;
	
	const int32_t chunkW = (maxChunkX-minChunkX+1);
	const int32_t chunkH = (maxChunkZ-minChunkZ+1);
	const int32_t imageW = chunkW * 16;
	const int32_t imageH = chunkH * 16;
	
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

	      // todo - this conditional inside an inner loop, not so good
	      
	      if ( imageMode == kImageModeBiome ) {
		// get biome color
		int biomeId = it->grassAndBiome[cx][cz] & 0xff;
		if ( has_key(biomeInfoList, biomeId) ) {
		  color = biomeInfoList[biomeId]->color;
		} else {
		  fprintf(stderr,"ERROR: Unkown biome %d 0x%x\n", biomeId, biomeId);
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
		uint8_t c = it->heightCol[cx][cz];
		color = palRedBlackGreen[c];
	      }
	      else if ( imageMode == kImageModeHeightColGrayscale ) {
		// get height value and make it grayscale
		uint8_t c = it->heightCol[cx][cz];
		color = htobe32( (c << 16) | (c << 8) | c );
	      }
	      else if ( imageMode == kImageModeBlockLight ) {
		// get block light value and expand it (is only 4-bits)
		// todobig - why z/x here and x/z everywhere else... hmmm
		uint8_t c = (it->topLight[cz][cx] & 0x0f) << 4;
		color = htobe32( (c << 16) | (c << 8) | c );
	      }
	      else if ( imageMode == kImageModeSkyLight ) {
		// get sky light value and expand it (is only 4-bits)
		// todobig - why z/x here and x/z everywhere else... hmmm
		uint8_t c = (it->topLight[cz][cx] & 0xf0);
		color = htobe32( (c << 16) | (c << 8) | c );
	      }
	      else {
		// regular image
		int blockid = it->blocks[cz][cx];
		
		if ( blockInfoList[blockid].lookupColorFlag ) {
		  int blockdata = it->data[cz][cx];
		  color = htobe32(standardColorInfo[blockdata].color + blockInfoList[blockid].lookupColorOffset);
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

	      // copy rgb color into output image
#if 0
	      int ix = (imageX + cx);
	      int iz = (imageZ + cz);
	      int off = (iz * imageW + ix) * 3;
	      buf[ off ] = (color & 0xff0000) >> 16;
	      buf[ off + 1 ] = (color & 0xff00) >> 8;
	      buf[ off + 2 ] = (color & 0xff);
#else
	      // trickery to copy the 3-bytes of RGB
	      memcpy(&buf[ ((imageZ + cz) * imageW + (imageX + cx)) * 3], &pcolor[1], 3);
#endif

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
	outputPNG(fname, buf, imageW, imageH);

	delete [] buf;

	if ( imageMode == kImageModeTerrain ) {
	  for (int i=0; i < 256; i++) {
	    if ( blockInfoList[i].colorSetNeedCount ) {
	      fprintf(stderr,"    Need pixel color for: 0x%x '%s' (%d)\n", i, blockInfoList[i].name.c_str(), blockInfoList[i].colorSetNeedCount);
	    }
	  }
	}
      }

#if 0
      // todo - we should see about doing this as temp img files w/ one pass thru the chunks; then create png from the temp img files
      // todo - this is similar to generateMovie, but to save time we build temp img file as we go
      // todo - a valiant effort, but this uses a ridiculous amount of disk space (e.g. 25gb for "another1")
      int32_t generateSlices(leveldb::DB* db, const std::string fname) {
	const int32_t chunkOffsetX = -minChunkX;
	const int32_t chunkOffsetZ = -minChunkZ;
	
	const int chunkW = (maxChunkX-minChunkX+1);
	const int chunkH = (maxChunkZ-minChunkZ+1);
	const int imageW = chunkW * 16;
	const int imageH = chunkH * 16;

	fprintf(stderr,"  Generate Slices\n");

	uint8_t* buf = new uint8_t[ imageW * imageH * 3 ];
	memset(buf, 0, imageW*imageH*3);
	
	// create our temp files
	fprintf(stderr,"    Create temp files (dirTemp=%s)\n", control.dirTemp.c_str());
	int32_t tmpId = (int32_t)time(NULL);
	char tmpstring[1025];
	std::string listFnTemp[128];
	int fdTemp[128];
	for (int cy=0; cy < 128; cy++) {
	  sprintf(tmpstring,"%s/mcpe_viz.temp.%d.L%03d.D%d.raw",control.dirTemp.c_str(), tmpId,cy,dimId);
	  listFnTemp[cy] = tmpstring;
	  fdTemp[cy] = open(tmpstring, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	  if ( fdTemp[cy] < 0 ) {
	    fprintf(stderr,"ERROR: Failed to open temp file for write (%s) (errno=%d)\n", tmpstring, errno);
	    // clean up other temp files
	    for ( int ty=0; ty < cy; ty++ ) {
	      close(fdTemp[ty]);
	      unlink(listFnTemp[ty].c_str());
	    }
	    return -1;
	  } else {
	    // file the entire temp image with 0 (to init data that might not get touched otherwise)
	    for (int ty=0; ty < imageH; ty++) {
	      write(fdTemp[cy], buf, imageW*3);
	    }
	  }
	}
	
	leveldb::ReadOptions readOptions;
	readOptions.fill_cache=false; // may improve performance?

	std::string svalue;
	const char* pchunk = nullptr;
	
	int32_t color;
	const char *pcolor = (const char*)&color;

	fprintf(stderr,"    Read chunks...\n");
	int32_t totalChunks = list.size();
	int32_t currChunk = 0;
	for ( const auto& it : list ) {
	  int imageX = (it->chunkX + chunkOffsetX) * 16;
	  int imageZ = (it->chunkZ + chunkOffsetZ) * 16;

	  if ( ( ++currChunk % 5000) == 0 ) {
	    fprintf(stderr,"    Read chunk %d / %d\n", currChunk, totalChunks);
	  }
	  
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
	  assert(dstatus.ok());
	  pchunk = svalue.data();
	  
	  for (int cy=0; cy < 128; cy++) {

	    // todo - be more clever about the seek? (seek on z change only etc
	    lseek(fdTemp[cy], (((imageZ) * imageW) + (imageX)) * 3, SEEK_SET);
	      
	    for (int cz=0; cz < 16; cz++) {

	      for (int cx=0; cx < 16; cx++) {
		
		uint8_t blockid = getBlockId(pchunk, cx,cz,cy);

		if ( blockid == 0 && ( cy > it->topBlockY[cz][cx] ) && (dimId != kDimIdNether) ) {
		  // special handling for air -- keep existing value if we are above top block
		  // the idea is to show air underground, but hide it above so that the map is not all black pixels @ y=127
		  // however, we do NOT do this for the nether. because: the nether
		} else {
		  
		  if ( blockInfoList[blockid].lookupColorFlag ) {
		    uint8_t blockdata = getBlockData(pchunk, cx,cz,cy);
		    color = htobe32(standardColorInfo[blockdata].color + blockInfoList[blockid].lookupColorOffset);
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
		  
		  // todo - be more clever about the seek? (seek on z change only etc
		  write(fdTemp[cy],&pcolor[1],3);
		}
	      }
	    }
	  }
	}

	// we have gone thru all the chunks and created all the temp files

	// close files
	for (int cy=0; cy < 128; cy++) {
	  close(fdTemp[cy]);
	}
	  
	// now process all the temp files
	// note RGB pixels

	for (int cy=0; cy < 128; cy++) {
	  
	  fprintf(stderr,"  Creating layer %d\n", cy);
	  
	  // copy file to buf for png
	  FILE* fp = fopen(listFnTemp[cy].c_str(),"rb");
	  uint8_t* p = buf;
	  for (int iy=0; iy < imageH; iy++) {
	    fread(p, 3, imageW, fp);
	    p+=imageW*3;
	  }
	  fclose(fp);
	  
	  // create png
	  std::string fnameTmp = control.fnOutputBase + ".mcpe_viz_slice.";
	  fnameTmp += "full.";
	  fnameTmp += name;
	  fnameTmp += ".";
	  char xtmp[100];
	  sprintf(xtmp,"%03d",cy);
	  fnameTmp += xtmp;
	  fnameTmp += ".png";
	  
	  control.fnLayerRaw[dimId][cy] = fnameTmp;
	  
	  outputPNG(fnameTmp, buf, imageW, imageH);

	  // delete temp file
	  unlink(listFnTemp[cy].c_str());
	}

	delete [] buf;

	return 0;
      }
#endif
      
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

	// todobig - we *could* write image data to flat files during parseDb and then convert these flat files into png here

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
		    assert(dstatus.ok());
		    pchunk = svalue.data();
		    pchunkX = it->chunkX;
		    pchunkZ = it->chunkZ;
		  }
		 
		  uint8_t blockid = getBlockId(pchunk, cx,cz,cy);

		  if ( blockid == 0 && ( cy > it->topBlockY[cz][cx] ) && (dimId != kDimIdNether) ) {
		    // special handling for air -- keep existing value if we are above top block
		    // the idea is to show air underground, but hide it above so that the map is not all black pixels @ y=127
		    // however, we do NOT do this for the nether. because: the nether
		  } else {
		    
		    if ( blockInfoList[blockid].lookupColorFlag ) {
		      uint8_t blockdata = getBlockData(pchunk, cx,cz,cy);
		      color = htobe32(standardColorInfo[blockdata].color + blockInfoList[blockid].lookupColorOffset);
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
		  
#if 0
		    // copy rgb pixel into output image
		    int ix = (imageX + cx);
		    int iz = (imageZ + cz);
		    int off = (iz * imageW + ix) * 3;
		    buf[ off ] = (color & 0xff0000) >> 16;
		    buf[ off + 1 ] = (color & 0xff00) >> 8;
		    buf[ off + 2 ] = (color & 0xff);
#else
		    // trickery to copy the 3-bytes of RGB
		    memcpy(&buf[ (((imageZ + cz) - cropZ) * cropW + ((imageX + cx) - cropX)) * 3], &pcolor[1], 3);
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
	  
	  outputPNG(fnameTmp, buf, cropW, cropH);
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

	if ( checkDoForDim(control.doMovie) ) {
	  fprintf(stderr,"  Generate movie\n");
	  generateMovie(db, std::string(control.fnOutputBase + "." + name + ".mp4"), true, true);
	}

	if ( checkDoForDim(control.doSlices) ) {
	  fprintf(stderr,"  Generate full-size slices\n");
	  generateMovie(db, std::string(""), false, false);
	  //generateSlices(db, std::string(""));
	}
	
	// reset
	for (int i=0; i < 256; i++) {
	  blockInfoList[i].colorSetNeedCount = 0;
	}

	return 0;
      }
    };

    
    bool vectorContains( std::vector<int> &v, int i ) {
      for ( const auto& iter: v ) {
	if ( iter == i ) {
	  return true;
	}
      }
      return false;
    }

    
    std::string makeIndent(int indent, const char* hdr) {
      std::string s;
      s.append(hdr);
      for (int i=0; i < indent; i++) {
	s.append("  ");
      }
      return s;
    }

    
    std::string getBiomeName(int idv) {
      if ( has_key(biomeInfoList, idv) ) {
	return biomeInfoList[idv]->name;
      }
      char s[256];
      sprintf(s,"ERROR: Failed to find biome id (%d)",idv);
      return std::string(s);
    }

    
    // helper types for NBT
    typedef std::pair<std::string, std::unique_ptr<nbt::tag> > MyTag;
    typedef std::vector< MyTag > MyTagList;

    int parseNbtTag( const char* hdr, int& indent, const MyTag& t ) {

      fprintf(control.fpLog,"%s[%s] ", makeIndent(indent,hdr).c_str(), t.first.c_str());

      nbt::tag_type tagType = t.second->get_type();
      
      switch ( tagType ) {
      case nbt::tag_type::End:
	fprintf(control.fpLog,"TAG_END\n");
	break;
      case nbt::tag_type::Byte:
	{
	  nbt::tag_byte v = t.second->as<nbt::tag_byte>();
	  fprintf(control.fpLog,"%d 0x%x (byte)\n", v.get(), v.get());
	}
	break;
      case nbt::tag_type::Short:
	{
	  nbt::tag_short v = t.second->as<nbt::tag_short>();
	  fprintf(control.fpLog,"%d 0x%x (short)\n", v.get(), v.get());
	}
	break;
      case nbt::tag_type::Int:
	{
	  nbt::tag_int v = t.second->as<nbt::tag_int>();
	  fprintf(control.fpLog,"%d 0x%x (int)\n", v.get(), v.get());
	}
	break;
      case nbt::tag_type::Long:
	{
	  nbt::tag_long v = t.second->as<nbt::tag_long>();
	  // note: silly work around for linux vs win32 weirdness
	  fprintf(control.fpLog,"%lld 0x%llx (long)\n", (long long int)v.get(), (long long int)v.get());
	}
	break;
      case nbt::tag_type::Float:
	{
	  nbt::tag_float v = t.second->as<nbt::tag_float>();
	  fprintf(control.fpLog,"%f (float)\n", v.get());
	}
	break;
      case nbt::tag_type::Double:
	{
	  nbt::tag_double v = t.second->as<nbt::tag_double>();
	  fprintf(control.fpLog,"%lf (double)\n", v.get());
	}
	break;
      case nbt::tag_type::Byte_Array:
	{
	  nbt::tag_byte_array v = t.second->as<nbt::tag_byte_array>();
	  fprintf(control.fpLog,"[");
	  int i=0;
	  for (const auto& itt: v ) {
	    if ( i++ > 0 ) { fprintf(control.fpLog," "); }
	    fprintf(control.fpLog,"%02x", (int)itt);
	  }
	  fprintf(control.fpLog,"] (hex byte array)\n");
	}
	break;
      case nbt::tag_type::String:
	{
	  nbt::tag_string v = t.second->as<nbt::tag_string>();
	  fprintf(control.fpLog,"'%s' (string)\n", v.get().c_str());
	}
	break;
      case nbt::tag_type::List:
	{
	  nbt::tag_list v = t.second->as<nbt::tag_list>();
	  int lnum = ++globalNbtListNumber;
	  fprintf(control.fpLog,"LIST-%d {\n",lnum);
	  indent++;
	  for ( const auto& it: v ) {
	    std::unique_ptr<nbt::tag> t = it.get().clone();
	    parseNbtTag( hdr, indent, std::make_pair(std::string(""), std::move(t) ) );
	  }
	  if ( --indent < 0 ) { indent=0; }
	  fprintf(control.fpLog,"%s} LIST-%d\n", makeIndent(indent,hdr).c_str(), lnum);
	}
	break;
      case nbt::tag_type::Compound:
	{
	  nbt::tag_compound v = t.second->as<nbt::tag_compound>();
	  int cnum = ++globalNbtCompoundNumber;
	  fprintf(control.fpLog,"COMPOUND-%d {\n",cnum);
	  indent++;
	  for ( const auto& it: v ) {
	    std::unique_ptr<nbt::tag> t = it.second.get().clone();
	    parseNbtTag( hdr, indent, std::make_pair( it.first, std::move(t) ) );
	  }
	  if ( --indent < 0 ) { indent=0; }
	  fprintf(control.fpLog,"%s} COMPOUND-%d\n", makeIndent(indent,hdr).c_str(),cnum);
	}
	break;
      case nbt::tag_type::Int_Array:
	{
	  nbt::tag_int_array v = t.second->as<nbt::tag_int_array>();
	  fprintf(control.fpLog,"[");
	  int i=0;
	  for ( const auto& itt: v ) {
	    if ( i++ > 0 ) { fprintf(control.fpLog," "); }
	    fprintf(control.fpLog,"%x", itt);
	  }
	  fprintf(control.fpLog,"] (hex int array)\n");
	}
	break;
      default:
	fprintf(control.fpLog,"[ERROR: Unknown tag type = %d]\n", (int)tagType);
	break;
      }

      return 0;
    }

    
    int parseNbt( const char* hdr, const char* buf, int bufLen, MyTagList& tagList ) {
      int indent=0;
      fprintf(control.fpLog,"%sNBT Decode Start\n",makeIndent(indent,hdr).c_str());

      // these help us look at dumped nbt data and match up LIST's and COMPOUND's
      globalNbtListNumber=0;
      globalNbtCompoundNumber=0;
      
      std::istringstream is(std::string(buf,bufLen));
      nbt::io::stream_reader reader(is, endian::little);

      // remove all elements from taglist
      tagList.clear();
      
      // read all tags
      MyTag t;
      bool done = false;
      std::istream& pis = reader.get_istr();
      while ( !done && (pis) && (!pis.eof()) ) {
	try {
	  t = reader.read_tag();
	  tagList.push_back(std::move(t));
	}
	catch (std::exception& e) {
	  // check for eof which means all is well
	  if ( ! pis.eof() ) {
	    fprintf(stderr, "NBT exception: (%s) (eof=%s) (is=%s)\n"
		    , e.what()
		    , pis.eof() ? "true" : "false"
		    , (pis) ? "true" : "false"
		    );
	  }
	  done = true;
	}
      }
      
      // iterate over the tags
      for ( const auto& itt: tagList ) {
	parseNbtTag( hdr, indent, itt );
      }
      
      fprintf(control.fpLog,"%sNBT Decode End (%d tags)\n",makeIndent(indent,hdr).c_str(), (int)tagList.size());

      return 0;
    }

    void worldPointToImagePoint(int32_t dimId, float wx, float wz, int &ix, int &iy, bool geoJsonFlag);

    // todo - use this throughout parsing or is it madness? :)
    template<class T>
    class MyValue {
    public:
      T value;
      bool valid;
      MyValue() {
	clear();
      }
      void clear() {
	value=(T)0;
	valid=false;
      }
      void set(T nvalue) {
	value = nvalue;
	valid = true;
      }
      std::string toString() {
	if ( valid ) {
	  return std::string(""+value);
	} else {
	  return std::string("*Invalid Value*");
	}
      }
    };

    std::string escapeDoubleQuotes(const std::string& s) {
      std::string ret="";
      for ( const auto& ch : s ) {
	if ( ch == '"' ) {
	  ret += "\\\"";
	} else {
	  ret += ch;
	}
      }
      return ret;
    }
    
    template<class T>
    class Point2d {
    public:
      T x, y;
      bool valid;
      Point2d() {
	clear();
      }
      void set(T nx, T ny) {
	x=nx;
	y=ny;
	valid=true;
      }
      void clear(){
	x=(T)0;
	y=(T)0;
	valid=false;
      }
      std::string toString() {
	if ( valid ) {
	  std::ostringstream str;
	  str << x << ", " << y;
	  return str.str();
	} else {
	  return std::string("*Invalid-Point2d*");
	}
      }
    };

    template<class T>
    class Point3d {
    public:
      T x, y, z;
      bool valid;
      Point3d() {
	clear();
      }
      void set(T nx, T ny, T nz) {
	x=nx;
	y=ny;
	z=nz;
	valid=true;
      }
      void clear(){
	x=(T)0;
	y=(T)0;
	z=(T)0;
	valid=false;
      }
      std::string toString() {
	if ( valid ) {
	  std::ostringstream str;
	  str << x << ", " << y << ", " << z;
	  return str.str();
	} else {
	  return std::string("*Invalid-Point2d*");
	}
      }
      std::string toStringImageCoords(int32_t dimId) {
	if ( valid ) {
	  int ix, iy;
	  worldPointToImagePoint(dimId, x,z, ix,iy, false);
	  std::ostringstream str;
	  str << ix << ", " << iy;
	  return str.str();
	} else {
	  return std::string("*Invalid-Point2d*");
	}
      }
      std::string toStringWithImageCoords(int32_t dimId) {
	return std::string("(" + toString() + " @ image " + toStringImageCoords(dimId) + ")");
      }
    };

    // todo - each value in these structs should be an object that does "valid" checking
    class ParsedEnchantment {
    public:
      MyValue<int32_t> id;
      MyValue<int32_t> level;
      ParsedEnchantment() {
	clear();
      }
      void clear() {
	id.clear();
	level.clear();
      }
      int parse(nbt::tag_compound& iench) {
	if ( iench.has_key("id", nbt::tag_type::Short) ) {
	  id.set( iench["id"].as<nbt::tag_short>().get() );
	}
	if ( iench.has_key("lvl", nbt::tag_type::Short) ) {
	  level.set( iench["lvl"].as<nbt::tag_short>().get() );
	}
	return 0;
      }
      std::string toGeoJSON() {
	char tmpstring[1025];
	std::string s = "";
	if ( id.valid ) {
	  s += "\"Name\": \"";
	  if ( has_key(enchantmentInfoList, id.value) ) {
	    s += enchantmentInfoList[id.value]->name;
	  } else {
	    sprintf(tmpstring,"(UNKNOWN: id=%d 0x%x)",id.value,id.value);
	    s += tmpstring;
	  }
	  //s += "\"Level\": \"";
	  sprintf(tmpstring," (%d)\"\n", level.value);
	  s += tmpstring;
	} else {
	  s += "\"valid\": \"false\"";
	}
	return s;
      }
      std::string toString() {
	char tmpstring[1025];
	std::string s = "";
	if ( id.valid ) {
	  if ( has_key(enchantmentInfoList, id.value) ) {
	    s += enchantmentInfoList[id.value]->name;
	  } else {
	    sprintf(tmpstring,"(UNKNOWN: id=%d 0x%x)",id.value,id.value);
	    s += tmpstring;
	  }
	  sprintf(tmpstring," (%d)", level.value);
	  s += tmpstring;
	} else {
	  s += "*Invalid id*";
	}
	return s;
      }
    };
    
    class ParsedItem {
    public:
      bool valid;
      bool armorFlag;
      int32_t id;
      int32_t slot;
      int32_t damage;
      int32_t count;
      int32_t repairCost;
      std::vector< std::unique_ptr<ParsedEnchantment> > enchantmentList;
      ParsedItem() {
	clear();
      }
      void clear() {
	valid = false;
	armorFlag = false;
	id = -1;
	slot = -1;
	damage = -1;
	count = -1;
	repairCost = -1;
	enchantmentList.clear();
      }
      int parse(nbt::tag_compound& iitem) {
	if ( iitem.has_key("Count", nbt::tag_type::Byte) ) {
	  count = iitem["Count"].as<nbt::tag_byte>().get();
	}
	if ( iitem.has_key("Damage", nbt::tag_type::Short) ) {
	  damage = iitem["Damage"].as<nbt::tag_short>().get();
	}
	if ( iitem.has_key("Slot", nbt::tag_type::Byte) ) {
	  slot = iitem["Slot"].as<nbt::tag_byte>().get();
	}
	if ( iitem.has_key("id", nbt::tag_type::Short) ) {
	  id = iitem["id"].as<nbt::tag_short>().get();
	}

	// todo - other fields? 

	// look for item enchantment
	if ( iitem.has_key("tag", nbt::tag_type::Compound) ) {
	  nbt::tag_compound etag = iitem["tag"].as<nbt::tag_compound>();

	  if ( etag.has_key("RepairCost", nbt::tag_type::Int) ) {
	    repairCost = etag["RepairCost"].as<nbt::tag_int>().get();
	  }

	  if ( etag.has_key("ench", nbt::tag_type::List) ) {
	    nbt::tag_list elist = etag["ench"].as<nbt::tag_list>();

	    for ( const auto& it: elist ) {
	      nbt::tag_compound ench = it.as<nbt::tag_compound>();
	      std::unique_ptr<ParsedEnchantment> e(new ParsedEnchantment());
	      int ret = e->parse(ench);
	      if ( ret == 0 ) {
		enchantmentList.push_back( std::move(e) );
	      } else {
		// todo err?
	      }
	    }
	  }
	}
	valid = true;
	return 0;
      }
      int parseArmor(nbt::tag_compound& iarmor) {
	armorFlag = true;
	return parse(iarmor);
      }

      std::string toGeoJSON(bool swallowFlag=false, int swallowValue=0, bool showCountFlag=false) {
	std::vector<std::string> list;
	std::string s;
	char tmpstring[1025];
	
	if ( ! valid ) { return std::string(""); }
	
	if ( swallowFlag ) {
	  if ( swallowValue == id ) {
	    return std::string("");
	  }
	}
	
	s = "\"Name\": ";
	if ( id <= 255 ) {
	  s += "\"" + blockInfoList[id].name + "\"";
	} else if ( has_key(itemInfoList, id) ) {
	  s += "\"" + itemInfoList[id]->name + "\"";
	} else {
	  sprintf(tmpstring,"\"Unknown:id=%d 0x%x\"", id, id);
	  s += tmpstring;
	}
	list.push_back(s);

	// todo - not useful?
	if ( false ) {
	  if ( damage >= 0 ) {
	    sprintf(tmpstring,"\"Damage\": \"%d\"", damage);
	    list.push_back(std::string(tmpstring));
	  }
	  if ( slot >= 0 ) {
	    sprintf(tmpstring,"\"Slot\": \"%d\"", slot);
	    list.push_back(std::string(tmpstring));
	  }
	}

	if ( showCountFlag && count >= 0 ) {
	  sprintf(tmpstring,"\"Count\": \"%d\"", count);
	  list.push_back(std::string(tmpstring));
	}
	
	if ( enchantmentList.size() > 0 ) {
	  s = "\"Enchantments\": [\n";
	  int i = enchantmentList.size();
	  for ( const auto& it: enchantmentList ) {
	    s+="{ " + it->toGeoJSON() + " }";
	    if ( --i > 0 ) {
	      s += ",";
	    }
	    s+="\n";
	  }
	  s += "]\n";
	  list.push_back(s);
	}

	// combine the list and put the commas in the right spots (stupid json)
	s = "";
	int i=list.size();
	for ( const auto& iter: list ) {
	  s += iter;
	  if ( --i > 0 ) {
	    s += ",";
	  }
	  s += "\n";
	}
	
	return s;
      }
      
      std::string toString(bool swallowFlag=false, int swallowValue=0) {
	char tmpstring[1025];

	if ( ! valid ) { return std::string("*Invalid Item*"); }

	if ( swallowFlag ) {
	  if ( swallowValue == id ) {
	    return std::string("");
	  }
	}
	
	std::string s = "[";

	if ( id <= 255 ) {
	  s += "Block:" + blockInfoList[id].name;
	} else if ( has_key(itemInfoList, id) ) {
	  s += "Item:" + itemInfoList[id]->name;
	} else {
	  sprintf(tmpstring,"(UNKNOWN: id=%d 0x%x)",id,id);
	  s += tmpstring;
	}

	if ( damage >= 0 ) {
	  sprintf(tmpstring," Damage=%d", damage);
	  s += tmpstring;
	}
	if ( count >= 0 ) {
	  sprintf(tmpstring," Count=%d", count);
	  s += tmpstring;
	}
	if ( slot >= 0 ) {
	  sprintf(tmpstring," Slot=%d", slot);
	  s += tmpstring;
	}

	if ( enchantmentList.size() > 0 ) {
	  s += " Enchantments=[";
	  int i=enchantmentList.size();
	  for ( const auto& it: enchantmentList ) {
	    s += it->toString();
	    if ( --i > 0 ) {
	      s += "; "; 
	    }
	  }
	  s += "]";
	}
	
	s += "]";
	return s;
      }
    };
      
    class ParsedEntity {
    public:
      Point3d<int> bedPosition;
      Point3d<int> spawn;
      Point3d<float> pos;
      Point2d<float> rotation;
      int32_t id;
      int32_t tileId;
      int32_t dimensionId;
      bool playerLocalFlag;
      bool playerRemoteFlag;
      std::vector< std::unique_ptr<ParsedItem> > inventory;
      std::vector< std::unique_ptr<ParsedItem> > armorList;
      ParsedItem itemInHand;
      ParsedItem item;
      
      ParsedEntity() {
	clear();
      }
      void clear() {
	bedPosition.clear();
	spawn.clear();
	pos.clear();
	rotation.clear();
	id = 0;
	tileId = -1;
	dimensionId = -1;
	playerLocalFlag = false;
	playerRemoteFlag = false;
	inventory.clear();
	armorList.clear();
	itemInHand.clear();
	item.clear();
      }
      int addInventoryItem ( nbt::tag_compound &iitem ) {
	std::unique_ptr<ParsedItem> it(new ParsedItem());
	int ret = it->parse(iitem);
	inventory.push_back( std::move(it) );
	return ret;
      }
      int doItemInHand ( nbt::tag_compound &iitem ) {
	itemInHand.clear();
	return itemInHand.parse(iitem);
      }
      int doItem ( nbt::tag_compound &iitem ) {
	item.clear();
	return item.parse(iitem);
      }
      int doTile ( int ntileId ) {
	tileId = ntileId;
	return 0;
      }

      int addArmor ( nbt::tag_compound &iarmor ) {
	std::unique_ptr<ParsedItem> armor(new ParsedItem());
	int ret = armor->parseArmor(iarmor);
	if ( ret == 0 ) {
	  armorList.push_back( std::move(armor) );
	}
	return 0;
      }

      std::string toGeoJSON(int32_t forceDimensionId) {
	std::vector<std::string> list;
	std::string s = "";
	char tmpstring[1025];
	
	int ix, iy;
	worldPointToImagePoint(forceDimensionId, pos.x,pos.z, ix,iy, true);
	sprintf(tmpstring,"%d, %d",ix,iy);
	s +=
	  "{\n"
	  "\t\"type\": \"Feature\",\n"
	  "\t\"geometry\": { \"type\": \"Point\", \"coordinates\": ["
	  ;
	s += tmpstring;
	s +=
	  "] },\n"
	  "\t\"properties\": {\n"
	  ;

	if ( has_key(entityInfoList, id) ) {
	  sprintf(tmpstring,"\"Name\": \"%s\"", entityInfoList[id]->name.c_str());
	  list.push_back(std::string(tmpstring));
	} else {
	  sprintf(tmpstring,"\"Name\": \"*UNKNOWN: id=%d 0x%x\"", id,id);
	  list.push_back(std::string(tmpstring));
	}

	sprintf(tmpstring," \"id\": \"%d\"", id);
	list.push_back(std::string(tmpstring));
	
	// todo - needed?
	if ( playerLocalFlag || playerRemoteFlag ) {
	  list.push_back(std::string("\"player\": \"true\""));
	} else {
	  // list.push_back(std::string("\"player\": \"false\""));
	}

	if ( forceDimensionId >= 0 ) {
	  // getting dimension name from myWorld is more trouble than it's worth here :)
	  sprintf(tmpstring,"\"Dimension\": \"%d\"", forceDimensionId);
	  list.push_back(std::string(tmpstring));
	}

	sprintf(tmpstring, "\"Pos\": [%s]", pos.toString().c_str());
	list.push_back(std::string(tmpstring));

	sprintf(tmpstring, "\"Rotation\": [%s]", rotation.toString().c_str());
	list.push_back(std::string(tmpstring));
	
	if ( playerLocalFlag || playerRemoteFlag ) {
	  sprintf(tmpstring,"\"BedPos\": [%s]", bedPosition.toString().c_str());
	  list.push_back(std::string(tmpstring));
	  sprintf(tmpstring,"\"Spawn\": [%s]", spawn.toString().c_str());
	  list.push_back(std::string(tmpstring));
	}

	if ( armorList.size() > 0 ) {
	  std::vector<std::string> tlist;
	  for ( const auto& it: armorList ) {
	    std::string sarmor = it->toGeoJSON(true,0,false);
	    if ( sarmor.size() > 0 ) {
	      tlist.push_back(std::string("{ " + sarmor + " }"));
	    }
	  }
	  if ( tlist.size() > 0 ) {
	    std::string ts = "\"Armor\": [\n";
	    int i = tlist.size();
	    for (const auto& iter : tlist ) {
	      ts += iter;
	      if ( --i > 0 ) {
		ts += ",";
	      }
	      ts += "\n";
	    }
	    ts += "]";
	    list.push_back(ts);
	  }
	}

	if ( inventory.size() > 0 ) {
	  std::vector<std::string> tlist;
	  for ( const auto& it: inventory ) {
	    std::string sitem = it->toGeoJSON(true,0,true);
	    if ( sitem.size() > 0 ) {
	      tlist.push_back(std::string("{ " + sitem + " }"));
	    }
	  }
	  if ( tlist.size() > 0 ) {
	    std::string ts = "\"Inventory\": [\n";
	    int i = tlist.size();
	    for (const auto& iter : tlist ) {
	      ts += iter;
	      if ( --i > 0 ) {
		ts += ",";
	      }
	      ts += "\n";
	    }
	    ts += "]";
	    list.push_back(ts);
	  }
	}

	if ( itemInHand.valid ) {
	  list.push_back(std::string("\"ItemInHand\": { " + itemInHand.toGeoJSON() + " }"));
	}
	
	if ( item.valid ) {
	  list.push_back(std::string("\"Item\": { " + item.toGeoJSON() + " }"));
	}

	// todo?
	/*
	if ( tileId >= 0 ) {
	  sprintf(tmpstring," Tile=[%s (%d 0x%x)]", blockInfoList[tileId].name.c_str(), tileId, tileId);
	  s += tmpstring;
	}
	*/

	if ( list.size() > 0 ) {
	  list.push_back(std::string("\"Entity\": \"true\""));
	  int i = list.size();
	  for (const auto& iter : list ) {
	    s += iter;
	    if ( --i > 0 ) {
	      s += ",";
	    }
	    s += "\n";
	  }
	}
	s += "}\n}\n";
	
	return s;
      }
      
      // todo - this should probably be multi-line so it's not so insane looking :)
      std::string toString(const std::string hdr, int32_t forceDimensionId) {
	char tmpstring[1025];
	
	std::string s = "[";
	if ( playerLocalFlag || playerRemoteFlag ) {
	  s += "Player";
	} else {
	  s += "Mob";
	}

	if ( has_key(entityInfoList, id) ) {
	  s += " Name=" + entityInfoList[id]->name;
	} else {
	  sprintf(tmpstring," Name=(UNKNOWN: id=%d 0x%x)",id,id);
	  s += tmpstring;
	}

	int32_t actualDimensionId = forceDimensionId;
	if ( dimensionId >= 0 ) {
	  actualDimensionId = dimensionId;
	  // getting dimension name from myWorld is more trouble than it's worth here :)
	  sprintf(tmpstring," Dimension=%d", dimensionId);
	  s += tmpstring;
	}

	// hack for pre-0.12 worlds
	if ( actualDimensionId < 0 ) {
	  actualDimensionId = 0;
	}
	
	s += " Pos=" + pos.toStringWithImageCoords(actualDimensionId);
	s += " Rotation=(" + rotation.toString() + ")";

	if ( playerLocalFlag ) {

	  // output player position + rotation to user
	  // todo - option to put icon on map

	  worldPointToImagePoint(actualDimensionId, pos.x,pos.z, playerPositionImageX, playerPositionImageY, true);

	  fprintf(stderr,"Player Position: Dimension=%d Pos=%s Rotation=(%f, %f)\n", actualDimensionId, pos.toStringWithImageCoords(actualDimensionId).c_str(), rotation.x,rotation.y);
	}

	if ( playerLocalFlag || playerRemoteFlag ) {
	  // these are always in the overworld
	  s += " BedPos=" + bedPosition.toStringWithImageCoords(kDimIdOverworld);
	  s += " Spawn=" + spawn.toStringWithImageCoords(kDimIdOverworld);
	}

	if ( armorList.size() > 0 ) {
	  s += " [Armor:";
	  for ( const auto& it: armorList ) {
	    std::string sarmor = it->toString(true,0);
	    if ( sarmor.size() > 0 ) {
	      s += " " + sarmor;
	    }
	  }
	  s += "]";
	}

	if ( inventory.size() > 0 ) {
	  s += " [Inventory:";
	  for ( const auto& it: inventory ) {
	    std::string sitem = it->toString(true,0);
	    if ( sitem.size() > 0 ) {
	      s += " " + sitem;
	    }
	  }
	  s += "]";
	}

	if ( itemInHand.valid ) {
	  s += " ItemInHand=" + itemInHand.toString();
	}

	if ( item.valid ) {
	  s += " Item=" + item.toString();
	}

	if ( tileId >= 0 ) {
	  sprintf(tmpstring," Tile=[%s (%d 0x%x)]", blockInfoList[tileId].name.c_str(), tileId, tileId);
	  s += tmpstring;
	}

	// todo - falling block also has "Data" (byte)

	// todo - dropped item also has "Fire" (short); "Health" (short)
	
	s += "]";
	return s;
      }
    };
    typedef std::vector< std::unique_ptr<ParsedEntity> > ParsedEntityList;

    class ParsedTileEntity {
    public:
      Point3d<float> pos;
      Point2d<int32_t> pairChest;
      std::string id;
      std::vector< std::unique_ptr<ParsedItem> > items;
      std::vector< std::string > text;
      int32_t entityId;
      
      ParsedTileEntity() {
	clear();
      }
      void clear() {
	pos.clear();
	pairChest.clear();
	id = "";
	entityId = -1;
	items.clear();
	text.clear();
      }
      int addItem ( nbt::tag_compound &iitem ) {
	std::unique_ptr<ParsedItem> it(new ParsedItem());
	int ret = it->parse(iitem);
	items.push_back( std::move(it) );
	return ret;
      }
      int addSign ( nbt::tag_compound &tc ) {
	text.push_back( tc["Text1"].as<nbt::tag_string>().get() );
	text.push_back( tc["Text2"].as<nbt::tag_string>().get() );
	text.push_back( tc["Text3"].as<nbt::tag_string>().get() );
	text.push_back( tc["Text4"].as<nbt::tag_string>().get() );
	return 0;
      }
      int addMobSpawner ( nbt::tag_compound &tc ) {
	entityId = tc["EntityId"].as<nbt::tag_int>().get();
	// todo - how to interpret entityId? (e.g. 0xb22 -- 0x22 is skeleton, what is 0xb?)
	// todo - any of these interesting?
	/*
	  0x31-te: [] COMPOUND-1 {
	  0x31-te:   [Delay] 20 0x14 (short)
	  0x31-te:   [EntityId] 2850 0xb22 (int)
	  0x31-te:   [MaxNearbyEntities] 6 0x6 (short)
	  0x31-te:   [MaxSpawnDelay] 200 0xc8 (short)
	  0x31-te:   [MinSpawnDelay] 200 0xc8 (short)
	  0x31-te:   [RequiredPlayerRange] 16 0x10 (short)
	  0x31-te:   [SpawnCount] 4 0x4 (short)
	  0x31-te:   [SpawnRange] 4 0x4 (short)
	*/
	return 0;
      }

      std::string toGeoJSON(int32_t forceDimensionId) {
	std::vector<std::string> list;
	char tmpstring[1025];

	if ( items.size() > 0 ) {
	  list.push_back("\"Name\": \"Chest\"");
	  
	  if ( pairChest.valid ) {
	    // todo - should we keep lists and combine chests so that we can show full content of double chests?
	    list.push_back("\"pairchest\": [" + pairChest.toString() + "]");
	  }
	  
	  std::vector<std::string> tlist;
	  for ( const auto& it: items ) {
	    std::string sitem = it->toGeoJSON(true,0,true);
	    if ( sitem.size() > 0 ) {
	      tlist.push_back( sitem );
	    }
	  }
	  if ( tlist.size() > 0 ) {
	    std::string ts = "\"Items\": [\n";
	    int i = tlist.size();
	    for (const auto& iter : tlist ) {
	      ts += "{ " + iter + " }";
	      if ( --i > 0 ) {
		ts += ",";
	      }
	      ts += "\n";
	    }
	    ts += "]";
	    list.push_back(ts);
	  }
	}
	
	if ( text.size() > 0 ) {
	  list.push_back("\"Name\": \"Sign\"");
	  std::string ts = "\"Sign\": {";
	  int i = text.size();
	  int t=1;
	  for ( const auto& it: text ) {
	    // todo - think about how to handle weird chars people put in signs
	    sprintf(tmpstring,"\"Text%d\": \"%s\"", t++, escapeDoubleQuotes(it).c_str());
	    ts += tmpstring;
	    if ( --i > 0 ) {
	      ts += ", ";
	    }
	  }
	  ts += "}";
	  list.push_back(ts);
	}

	if ( entityId > 0 ) {
	  list.push_back("\"Name\": \"MobSpawner\"");
	  std::string ts = "\"MobSpawner\": {";
	  sprintf(tmpstring, "\"entityId\": \"%d (0x%x)\",", entityId, entityId);
	  ts += tmpstring;
	  
	  // todo - the entityid is weird.  lsb appears to be entity type; high bytes are ??
	  int eid = entityId & 0xff;
	  if ( has_key(entityInfoList, eid) ) {
	    ts += "\"Name\": \"" + entityInfoList[eid]->name + "\"";
	  } else {
	    sprintf(tmpstring,"\"Name\": \"(UNKNOWN: id=%d 0x%x)\"",eid,eid);
	    ts += tmpstring;
	  }
	  ts += "}\n";
	  list.push_back(ts);
	}

	if ( list.size() > 0 ) {
	  std::string s="";

	  list.push_back(std::string("\"TileEntity\": \"true\""));
	  sprintf(tmpstring,"\"Dimension\": \"%d\"", forceDimensionId);
	  list.push_back(std::string(tmpstring));

	  sprintf(tmpstring, "\"Pos\": [%s]", pos.toString().c_str());
	  list.push_back(std::string(tmpstring));
	  
	  int ix, iy;
	  worldPointToImagePoint(forceDimensionId, pos.x,pos.z, ix,iy, true);
	  sprintf(tmpstring,"%d, %d",ix,iy);
	  s +=
	    "{\n"
	    "\t\"type\": \"Feature\",\n"
	    "\t\"geometry\": { \"type\": \"Point\", \"coordinates\": ["
	    ;
	  s += tmpstring;
	  s +=
	    "] },\n"
	    "\t\"properties\": {\n"
	    ;
	  
	  int i = list.size();
	  for (const auto& iter : list ) {
	    s += iter;
	    if ( --i > 0 ) {
	      s += ",";
	    }
	    s += "\n";
	  }
	  s += "}\n}\n";
	  return s;
	}

	return std::string("");
      }
      
      // todo - this should probably be multi-line so it's not so insane looking :)
      std::string toString(const std::string hdr, int32_t dimensionId) {
	char tmpstring[1025];
	
	std::string s = "[";
	
	s += "Pos=" + pos.toStringWithImageCoords(dimensionId);

	if ( items.size() > 0 ) {
	  if ( pairChest.valid ) {
	    // todo - should we keep lists and combine chests so that we can show full content of double chests?
	    s += " PairChest=(" + pairChest.toString() + ")";
	  }
	  
	  s+=" Chest=[";
	  int i = items.size();
	  for ( const auto& it: items ) {
	    std::string sitem = it->toString(true,0);
	    --i;
	    if ( sitem.size() > 0 ) {
	      s += sitem;
	      if ( i > 0 ) {
		s += " ";
	      }
	    }
	  }
	  s += "]";
	}

	if ( text.size() > 0 ) {
	  s+=" Sign=[";
	  int i = text.size();
	  for ( const auto& it: text ) {
	    s += it;
	    if ( --i > 0 ) {
	      s += " / ";
	    }
	  }
	  s += "]";
	}

	if ( entityId > 0 ) {
	  s+= " MobSpawner=[";
	  // todo - the entityid is weird.  lsb appears to be entity type; high bytes are ??
	  int eid = entityId & 0xff;
	  if ( has_key(entityInfoList, eid) ) {
	    s += "Name=" + entityInfoList[eid]->name;
	  } else {
	    sprintf(tmpstring,"Name=(UNKNOWN: id=%d 0x%x)",eid,eid);
	    s += tmpstring;
	  }
	  s += "]";
	}
	
	s += "]";
	return s;
      }
    };
    typedef std::vector< std::unique_ptr<ParsedTileEntity> > ParsedTileEntityList;
    
    int parseNbt_entity(int32_t dimensionId, std::string dimName, std::string hdr, MyTagList &tagList, bool playerLocalFlag, bool playerRemoteFlag, ParsedEntityList &entityList) {
      entityList.clear();

      int32_t actualDimensionId = dimensionId;
      
      // this could be a list of mobs
      for ( size_t i=0; i < tagList.size(); i++ ) { 

	std::unique_ptr<ParsedEntity> entity(new ParsedEntity());
	entity->clear();
	
	// check tagList
	if ( tagList[i].second->get_type() == nbt::tag_type::Compound ) {
	  // all is good
	} else {
	  fprintf(stderr,"ERROR: parseNbt_entity() called with invalid tagList (loop=%d)\n",(int)i);
	  return -1;
	}
	
	nbt::tag_compound tc = tagList[i].second->as<nbt::tag_compound>();
	
	entity->playerLocalFlag = playerLocalFlag;
	entity->playerRemoteFlag = playerRemoteFlag;
	
	if ( tc.has_key("Armor", nbt::tag_type::List) ) {
	  nbt::tag_list armorList = tc["Armor"].as<nbt::tag_list>();
	  for ( const auto& iter: armorList ) {
	    nbt::tag_compound armor = iter.as<nbt::tag_compound>();
	    entity->addArmor(armor);
	  }
	}
	
	if ( tc.has_key("Attributes") ) {
	  // todo - parse - quite messy; interesting?
	}

	if ( playerLocalFlag || playerRemoteFlag ) {

	  if ( tc.has_key("BedPositionX", nbt::tag_type::Int) ) {
	    entity->bedPosition.set( tc["BedPositionX"].as<nbt::tag_int>().get(),
				    tc["BedPositionY"].as<nbt::tag_int>().get(),
				    tc["BedPositionZ"].as<nbt::tag_int>().get() );
	  }

	  if ( tc.has_key("SpawnX", nbt::tag_type::Int) ) {
	    entity->spawn.set( tc["SpawnX"].as<nbt::tag_int>().get(),
			      tc["SpawnY"].as<nbt::tag_int>().get(),
			      tc["SpawnZ"].as<nbt::tag_int>().get() );
	  }
      
	  if ( tc.has_key("DimensionId", nbt::tag_type::Int) ) {
	    entity->dimensionId = tc["DimensionId"].as<nbt::tag_int>().get();
	    actualDimensionId = entity->dimensionId;
	  }
      
	  if ( tc.has_key("Inventory", nbt::tag_type::List) ) {
	    nbt::tag_list inventory = tc["Inventory"].as<nbt::tag_list>();
	    for ( const auto& iter: inventory ) {
	      nbt::tag_compound iitem = iter.as<nbt::tag_compound>();
	      entity->addInventoryItem(iitem);
	    }
	  }
	} else {

	  // non-player entity

	  // todo - other interesting bits?
	
	  if ( tc.has_key("ItemInHand", nbt::tag_type::Compound) ) {
	    entity->doItemInHand( tc["ItemInHand"].as<nbt::tag_compound>() );
	  }
	  if ( tc.has_key("Item", nbt::tag_type::Compound) ) {
	    entity->doItem( tc["Item"].as<nbt::tag_compound>() );
	  }
	  if ( tc.has_key("Tile", nbt::tag_type::Byte) ) {
	    entity->doTile( tc["Tile"].as<nbt::tag_byte>() );
	  }
	}

	if ( tc.has_key("Pos", nbt::tag_type::List) ) {
	  nbt::tag_list pos = tc["Pos"].as<nbt::tag_list>();
	  entity->pos.set( pos[0].as<nbt::tag_float>().get(),
			  pos[1].as<nbt::tag_float>().get(),
			  pos[2].as<nbt::tag_float>().get() );
	}

	if ( tc.has_key("Rotation", nbt::tag_type::List) ) {
	  nbt::tag_list rotation = tc["Rotation"].as<nbt::tag_list>();
	  entity->rotation.set( rotation[0].as<nbt::tag_float>().get(),
				rotation[1].as<nbt::tag_float>().get() );
	}
      
	if ( tc.has_key("id", nbt::tag_type::Int) ) {
	  entity->id = tc["id"].as<nbt::tag_int>().get();
	}
      
	fprintf(control.fpLog, "%sParsedEntity: %s\n", dimName.c_str(), entity->toString(hdr, actualDimensionId).c_str());

	listGeoJSON.push_back( entity->toGeoJSON(actualDimensionId) );

	entityList.push_back( std::move(entity) );
      }

      return 0;
    }

    int parseNbt_tileEntity(int32_t dimensionId, std::string dimName, MyTagList &tagList, ParsedTileEntityList &tileEntityList) {
      tileEntityList.clear();
      
      // this could be a list of mobs
      for ( size_t i=0; i < tagList.size(); i++ ) { 

	bool parseFlag = false;
	
	std::unique_ptr<ParsedTileEntity> tileEntity(new ParsedTileEntity());
	tileEntity->clear();
	
	// check tagList
	if ( tagList[i].second->get_type() == nbt::tag_type::Compound ) {
	  // all is good
	} else {
	  fprintf(stderr,"ERROR: parseNbt_tileEntity() called with invalid tagList (loop=%d)\n",(int)i);
	  return -1;
	}
	
	nbt::tag_compound tc = tagList[i].second->as<nbt::tag_compound>();

	if ( tc.has_key("x", nbt::tag_type::Int) ) {
	  tileEntity->pos.set( tc["x"].as<nbt::tag_int>().get(),
			       tc["y"].as<nbt::tag_int>().get(),
			       tc["z"].as<nbt::tag_int>().get() );
	}

	if ( tc.has_key("pairx", nbt::tag_type::Int) ) {
	  tileEntity->pairChest.set( tc["pairx"].as<nbt::tag_int>().get(),
				     tc["pairz"].as<nbt::tag_int>().get() );
	}
	
	if ( tc.has_key("id", nbt::tag_type::String) ) {
	  std::string id = tc["id"].as<nbt::tag_string>().get();
	  
	  if ( id == "Sign" ) {
	    tileEntity->addSign(tc);
	    parseFlag = true;
	  }
	  else if ( id == "Chest" ) {
	    if ( tc.has_key("Items", nbt::tag_type::List) ) {
	      nbt::tag_list items = tc["Items"].as<nbt::tag_list>();
	      for ( const auto& iter: items ) {
		nbt::tag_compound iitem = iter.as<nbt::tag_compound>();
		tileEntity->addItem(iitem);
	      }
	      parseFlag = true;
	    }
	  }
	  else if ( id == "BrewingStand" ) {
	    // todo - anything interesting?
	  }
	  else if ( id == "EnchantTable" ) {
	    // todo - anything interesting?
	  }
	  else if ( id == "Furnace" ) {
	    // todo - anything interesting?
	  }
	  else if ( id == "MobSpawner" ) {
	    tileEntity->addMobSpawner(tc);
	    parseFlag = true;
	  }
	  else {
	    fprintf(control.fpLog,"ERROR: Unknown tileEntity id=(%s)\n", id.c_str());
	  }
	}

	if ( parseFlag ) {
	  fprintf(control.fpLog, "%sParsedTileEntity: %s\n", dimName.c_str(), tileEntity->toString("", dimensionId).c_str());

	  std::string json = tileEntity->toGeoJSON(dimensionId);
	  if ( json.size() > 0 ) {
	    listGeoJSON.push_back( json );
	  }
	  
	  tileEntityList.push_back( std::move(tileEntity) );
	}
      }
      
      return 0;
    }

    class ParsedPortal {
    public:
      Point3d<int> pos;
      int32_t dimId;
      int32_t span;
      int32_t xa, za;
      
      ParsedPortal() {
	clear();
      }
      void clear() {
	pos.clear();
	dimId = -1;
	span = xa = za = 0;
      }
      int add ( nbt::tag_compound &tc ) {
	dimId = tc["DimId"].as<nbt::tag_int>().get();
	span = tc["Span"].as<nbt::tag_byte>().get();
	int tpx = tc["TpX"].as<nbt::tag_int>().get();
	int tpy = tc["TpY"].as<nbt::tag_int>().get();
	int tpz = tc["TpZ"].as<nbt::tag_int>().get();
	pos.set(tpx,tpy,tpz);
	xa = tc["Xa"].as<nbt::tag_byte>().get();
	za = tc["Za"].as<nbt::tag_byte>().get();
	return 0;
      }
      std::string toGeoJSON() {
	std::vector<std::string> list;
	char tmpstring[1025];

	// note: we fake this as a tile entity so that it is easy to deal with in js
	list.push_back(std::string("\"TileEntity\": \"true\""));

	list.push_back("\"Name\": \"NetherPortal\"");

	sprintf(tmpstring,"\"DimId\": \"%d\"", dimId);
	list.push_back(tmpstring);

	sprintf(tmpstring,"\"Span\": \"%d\"", span);
	list.push_back(tmpstring);

	sprintf(tmpstring,"\"Xa\": \"%d\"", xa);
	list.push_back(tmpstring);

	sprintf(tmpstring,"\"Za\": \"%d\"", za);
	list.push_back(tmpstring);
	
	if ( list.size() > 0 ) {
	  std::string s="";

	  sprintf(tmpstring,"\"Dimension\": \"%d\"", dimId);
	  list.push_back(std::string(tmpstring));

	  sprintf(tmpstring, "\"Pos\": [%s]", pos.toString().c_str());
	  list.push_back(std::string(tmpstring));
	  
	  int ix, iy;
	  worldPointToImagePoint(dimId, pos.x,pos.z, ix,iy, true);
	  sprintf(tmpstring,"%d, %d",ix,iy);
	  s +=
	    "{\n"
	    "\t\"type\": \"Feature\",\n"
	    "\t\"geometry\": { \"type\": \"Point\", \"coordinates\": ["
	    ;
	  s += tmpstring;
	  s +=
	    "] },\n"
	    "\t\"properties\": {\n"
	    ;
	  
	  int i = list.size();
	  for (const auto& iter : list ) {
	    s += iter;
	    if ( --i > 0 ) {
	      s += ",";
	    }
	    s += "\n";
	  }
	  s += "}\n}\n";
	  return s;
	}

	return std::string("");
      }
      
      // todo - this should probably be multi-line so it's not so insane looking :)
      std::string toString() {
	char tmpstring[1025];

	std::string s = "[";

	s += "Nether Portal";
	
	s += " Pos=" + pos.toStringWithImageCoords(dimId);
	
	sprintf(tmpstring," DimId=%d", dimId);
	s += tmpstring;

	sprintf(tmpstring," Span=%d", span);
	s += tmpstring;

	sprintf(tmpstring," Xa=%d", xa);
	s += tmpstring;

	sprintf(tmpstring," Za=%d", za);
	s += tmpstring;
	
	s += "]";
	return s;
      }
    };
    typedef std::vector< std::unique_ptr<ParsedPortal> > ParsedPortalList;

    int parseNbt_portals(MyTagList &tagList, ParsedPortalList &portalList) {
      portalList.clear();
      
      // this could be a list of mobs
      for ( size_t i=0; i < tagList.size(); i++ ) { 

	// check tagList
	if ( tagList[i].second->get_type() == nbt::tag_type::Compound ) {
	  nbt::tag_compound tc = tagList[i].second->as<nbt::tag_compound>();
	  if ( tc.has_key("data", nbt::tag_type::Compound) ) {
	    nbt::tag_compound td = tc["data"].as<nbt::tag_compound>();
	    if ( td.has_key("PortalRecords", nbt::tag_type::List) ) {
	      // all is good
	      nbt::tag_list plist = td["PortalRecords"].as<nbt::tag_list>();
	      
	      for ( const auto& it : plist ) {
		nbt::tag_compound pc = it.as<nbt::tag_compound>();

		std::unique_ptr<ParsedPortal> portal(new ParsedPortal());
		portal->clear();
		
		portal->add( pc );
		
		fprintf(control.fpLog, "ParsedPortal: %s\n", portal->toString().c_str());
		
		std::string json = portal->toGeoJSON();
		if ( json.size() > 0 ) {
		  listGeoJSON.push_back( json );
		}
		
		portalList.push_back( std::move(portal) );
	      }
	    }
	  }
	}
      }
      
      return 0;
    }

    
    int printKeyValue(const char* key, int key_size, const char* value, int value_size, bool printKeyAsStringFlag) {
      fprintf(control.fpLog,"WARNING: Unknown Record: key_size=%d key_string=[%s] key_hex=[", key_size, 
	      (printKeyAsStringFlag ? key : "(SKIPPED)"));
      for (int i=0; i < key_size; i++) {
	if ( i > 0 ) { fprintf(control.fpLog," "); }
	fprintf(control.fpLog,"%02x",((int)key[i] & 0xff));
      }
      fprintf(control.fpLog,"] value_size=%d value_hex=[",value_size);
      for (int i=0; i < value_size; i++) {
	if ( i > 0 ) { fprintf(control.fpLog," "); }
	fprintf(control.fpLog,"%02x",((int)value[i] & 0xff));
      }
      fprintf(control.fpLog,"]\n");
      return 0;
    }

    
    class MyWorld {
    public:
      leveldb::DB* db;
      leveldb::Options dbOptions;
      leveldb::ReadOptions dbReadOptions;
      
      ChunkDataList chunkList[kDimIdCount];
      
      MyWorld() {
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
      ~MyWorld() {
	dbClose();
	delete dbOptions.compressors[0];
      }
      
      int dbOpen(std::string dirDb) {
	// todobig - leveldb read-only? snapshot?
	fprintf(stderr,"DB Open: dir=%s\n",dirDb.c_str());
	leveldb::Status status = leveldb::DB::Open(dbOptions, std::string(dirDb+"/db"), &db);
	fprintf(stderr,"DB Open Status: %s\n", status.ToString().c_str()); fflush(stderr);
	assert(status.ok());
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
	leveldb::Iterator* iter = db->NewIterator(dbReadOptions);
	for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
	  leveldb::Slice skey = iter->key();
	  int key_size = skey.size();
	  const char* key = skey.data();
	  
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
	      chunkList[0].addToChunkBounds(chunkX, chunkZ);
	    }
	  }
	  else if ( key_size == 13 ) {
	    chunkX = myParseInt32(key, 0);
	    chunkZ = myParseInt32(key, 4);
	    chunkDimId = myParseInt32(key, 8);
	    chunkType = myParseInt8(key, 12);
	    
	    // sanity checks
	    if ( chunkType == 0x30 ) {
	      chunkList[chunkDimId].addToChunkBounds(chunkX, chunkZ);
	    }
	  }
	}

	assert(iter->status().ok());  // Check for any errors found during the scan
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

      leveldb::Status parseDb () {
	int histo[256];
	int histoBiome[256];
	uint8_t topBlock[16][16];
	uint8_t topData[16][16];
	uint32_t grassAndBiome[16][16];
	uint8_t topSkyLight[16][16];
	uint8_t topLight[16][16];
	uint8_t topBlockY[16][16];
	uint8_t topBlockLight[16][16];
	uint8_t colData1[16][16];
	int32_t colData2[16][16];

	char tmpstring[256];

	int32_t chunkX=-1, chunkZ=-1, chunkDimId=-1, chunkType=-1;
      
	calcChunkBounds();

	// report hide and force lists
	{
	  fprintf(stderr,"Active 'hide-top' and 'force-top':\n");
	  int itemCt = 0;
	  int32_t blockId;
	  for (int dimId=0; dimId < kDimIdCount; dimId++) {
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
	MyTagList tagList;
	int recordCt = 0;
	leveldb::Iterator* iter = db->NewIterator(dbReadOptions);
	for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {

	  // note: we get the raw buffer early to avoid overhead (maybe?)
	  leveldb::Slice skey = iter->key();
	  int key_size = (int)skey.size();
	  const char* key = skey.data();

	  leveldb::Slice svalue = iter->value();
	  int value_size = svalue.size();
	  const char* value = svalue.data();

	  ++recordCt;
	  if ( control.shortRunFlag && recordCt > 1000 ) {
	    break;
	  }
	  if ( (recordCt % 10000) == 0 ) {
	    fprintf(stderr, "  Reading records: %d\n", recordCt);
	  }

	  std::string r;
	  fprintf(control.fpLog,"\n");

	  if ( strncmp(key,"BiomeData",key_size) == 0 ) {
	    // 0x61 +"BiomeData" -- snow accum? -- overworld only?
	    fprintf(control.fpLog,"BiomeData value:\n");
	    parseNbt("BiomeData: ", value, value_size, tagList);
	    // todo - parse tagList?
	  }
	  else if ( strncmp(key,"Overworld",key_size) == 0 ) {
	    fprintf(control.fpLog,"Overworld value:\n");
	    parseNbt("Overworld: ", value, value_size, tagList);
	    // todo - parse tagList?
	  }
	  else if ( strncmp(key,"~local_player",key_size) == 0 ) {
	    fprintf(control.fpLog,"Local Player value:\n");
	    int ret = parseNbt("Local Player: ", value, value_size, tagList);
	    if ( ret == 0 ) { 
	      ParsedEntityList entityList;
	      parseNbt_entity(-1, "","Local Player:", tagList, true, false, entityList);
	    }
	  }
	  else if ( (key_size>=7) && (strncmp(key,"player_",7) == 0) ) {
	    // note: key contains player id (e.g. "player_-1234")
	    std::string playerRemoteId = &key[strlen("player_")];
	    fprintf(control.fpLog,"Remote Player (id=%s) value:\n",playerRemoteId.c_str());
	    int ret = parseNbt("Remote Player: ", value, value_size, tagList);
	    if ( ret == 0 ) {
	      ParsedEntityList entityList;
	      parseNbt_entity(-1, "","Remote Player:", tagList, false, true, entityList);
	    }
	  }
	  else if ( strncmp(key,"villages",key_size) == 0 ) {
	    fprintf(control.fpLog,"Villages value:\n");
	    parseNbt("villages: ", value, value_size, tagList);
	    // todo - parse tagList?
	  }
	  else if ( strncmp(key,"Nether",key_size) == 0 ) {
	    fprintf(control.fpLog,"Nether value:\n");
	    parseNbt("Nether: ", value, value_size, tagList);
	    // todo - parse tagList?
	  }
	  else if ( strncmp(key,"portals",key_size) == 0 ) {
	    fprintf(control.fpLog,"portals value:\n");
	    int ret = parseNbt("portals: ", value, value_size, tagList);
	    if ( ret == 0 ) {
	      ParsedPortalList portalList;
	      parseNbt_portals(tagList,portalList);
	    }
	  }
			 
	  else if ( key_size == 9 || key_size == 13 ) {

	    // this is probably a record we want to parse

	    std::string dimName;
	    if ( key_size == 9 ) {
	      chunkX = myParseInt32(key, 0);
	      chunkZ = myParseInt32(key, 4);
	      chunkDimId = kDimIdOverworld;
	      chunkType = myParseInt8(key, 8);
	      dimName = "overworld";
	    }
	    else if ( key_size == 13 ) {
	      chunkX = myParseInt32(key, 0);
	      chunkZ = myParseInt32(key, 4);
	      chunkDimId = myParseInt32(key, 8);
	      chunkType = myParseInt8(key, 12);
	      dimName = "nether";

	      // check for new dim id's
	      if ( chunkDimId != kDimIdNether ) {
		fprintf(stderr, "HEY! Found new chunkDimId=0x%x\n", chunkDimId);
	      }
	    }
	  
	    chunkList[chunkDimId].histoChunkType[chunkType]++;

	    r = dimName + "-chunk: ";
	    sprintf(tmpstring,"%d %d (type=0x%02x)", chunkX, chunkZ, chunkType);
	    r += tmpstring;
	    if ( true ) {
	      // show approximate image coordinates for chunk
	      int chunkOffsetX = -chunkList[chunkDimId].minChunkX;
	      int chunkOffsetZ = -chunkList[chunkDimId].maxChunkZ;
	      int imageX = (chunkX + chunkOffsetX) * 16;
	      int imageZ = (chunkZ + chunkOffsetZ) * 16;
	      int ix = (imageX);
	      int iz = (imageZ);
	      sprintf(tmpstring," (image %d %d)",ix,iz);
	      r+=tmpstring;
	    }
	    r += "\n";
	    fprintf(control.fpLog,r.c_str());

	    switch ( chunkType ) {
	    case 0x30:
	      // terrain data

	      // clear data
	      memset(histo,0,256*sizeof(int));
	      memset(histoBiome,0,256*sizeof(int));
	      memset(topBlock,0, 16*16*sizeof(uint8_t));
	      memset(topData,0, 16*16*sizeof(uint8_t));
	      memset(grassAndBiome,0, 16*16*sizeof(uint32_t));
	      memset(topSkyLight,0, 16*16*sizeof(uint8_t));
	      memset(topBlockLight,0, 16*16*sizeof(uint8_t));
	      memset(topLight,0, 16*16*sizeof(uint8_t));
	      
	      // iterate over chunk area, get data etc
	      for (int cy=127; cy >= 0; cy--) {
		for ( int cz=0; cz < 16; cz++ ) {
		  for ( int cx=0; cx < 16; cx++) {
		    uint8_t blockId = getBlockId(value, cx,cz,cy);
		    histo[blockId]++;

		    // todo - check for isSolid?
		    if ( blockId != 0 ) {  // current block is NOT air
		      if ( ( topBlock[cz][cx] == 0 &&  // top block is not already set
			     !vectorContains(chunkList[chunkDimId].blockHideList, blockId) ) || // blockId is not in hide list
			   vectorContains(chunkList[chunkDimId].blockForceTopList, blockId) // see if we want to force a block
			   ) {
			topBlock[cz][cx] = blockId;
			topData[cz][cx] = getBlockData(value, cx,cz,cy);
			topSkyLight[cz][cx] = getBlockSkyLight(value, cx,cz,cy);
			topBlockLight[cz][cx] = getBlockBlockLight(value, cx,cz,cy);
			topBlockY[cz][cx] = cy;

			// todo topLight here
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
			topLight[cz][cx] = (sl << 4) | bl;
#endif
		      }
		    }
		  }
		}
	      }

	      memset(colData1, 0, 16*16*sizeof(uint8_t));
	      memset(colData2, 0, 16*16*sizeof(int32_t));
	      for (int cz=0; cz < 16; cz++) {
		for (int cx=0; cx < 16; cx++) {
		  colData1[cz][cx] = getColData1(value, cx,cz);
		  colData2[cz][cx] = getColData2(value, cx,cz);

#if 0
		  // todo - testing idea about lighting - get lighting from top solid block - result is part good part crazy
		  int ty = colData1[cz][cx] + 1;
		  if ( ty > 127 ) { ty=127; }
		  uint8_t sl = getBlockSkyLight(value, cx,cz,ty);
		  uint8_t bl = getBlockBlockLight(value, cx,cz,ty);
		  topLight[cz][cx] = (sl << 4) | bl;
#endif
		}
	      }

	    
	      // print chunk info
	      fprintf(control.fpLog,"Top Blocks (block-id:block-data:biome-id):\n");
	      for (int cz=0; cz<16; cz++) {
		for (int cx=0; cx<16; cx++) {
		  int32_t rawData = colData2[cz][cx];
		  int biomeId = (int)(rawData & 0xFF);
		  histoBiome[biomeId]++;
		  chunkList[chunkDimId].histoGlobalBiome[biomeId]++;
		  grassAndBiome[cz][cx] = rawData;
		  fprintf(control.fpLog,"%02x:%x:%02x ", (int)topBlock[cz][cx], (int)topData[cz][cx], (int)biomeId);
		}
		fprintf(control.fpLog,"\n");
	      }
	      fprintf(control.fpLog,"Block Histogram:\n");
	      for (int i=0; i < 256; i++) {
		if ( histo[i] > 0 ) {
		  fprintf(control.fpLog,"%s-hg: %02x: %6d (%s)\n", dimName.c_str(), i, histo[i], blockInfoList[i].name.c_str());
		}
	      }
	      fprintf(control.fpLog,"Biome Histogram:\n");
	      for (int i=0; i < 256; i++) {
		if ( histoBiome[i] > 0 ) {
		  std::string biomeName( getBiomeName(i) );
		  fprintf(control.fpLog,"%s-hg-biome: %02x: %6d (%s)\n", dimName.c_str(), i, histoBiome[i], biomeName.c_str());
		}
	      }
	      fprintf(control.fpLog,"Block Light (skylight:blocklight):\n");
	      for (int cz=0; cz<16; cz++) {
		for (int cx=0; cx<16; cx++) {
		  fprintf(control.fpLog,"%x:%x ", (int)topSkyLight[cz][cx], (int)topBlockLight[cz][cx]);
		}
		fprintf(control.fpLog,"\n");
	      }
	      // todo - grass-color is in high 3 bytes of coldata2
	      // todo - need to show this?
	      fprintf(control.fpLog,"Column Data (height-col:biome):\n");
	      for (int cz=0; cz<16; cz++) {
		for (int cx=0; cx<16; cx++) {
		  int biomeId = (int)(colData2[cz][cx] & 0xFF);
		  fprintf(control.fpLog,"%x:%02x ", (int)colData1[cz][cx], biomeId);
		}
		fprintf(control.fpLog,"\n");
	      }

	      // store chunk
	      chunkList[chunkDimId].putChunk(chunkX, chunkZ,
					     &topBlock[0][0], &topData[0][0],
					     &grassAndBiome[0][0], &topBlockY[0][0],
					     &colData1[0][0], &topLight[0][0]);

	      break;

	    case 0x31:
	      {
		fprintf(control.fpLog,"%s 0x31 chunk (tile entity data):\n", dimName.c_str());
		int ret = parseNbt("0x31-te: ", value, value_size, tagList);
		if ( ret == 0 ) { 
		  ParsedTileEntityList tileEntityList;
		  parseNbt_tileEntity(chunkDimId, dimName+"-", tagList, tileEntityList);
		}
	      }
	      break;

	    case 0x32:
	      {
		fprintf(control.fpLog,"%s 0x32 chunk (entity data):\n", dimName.c_str());
		int ret = parseNbt("0x32-e: ", value, value_size, tagList);
		if ( ret == 0 ) {
		  ParsedEntityList entityList;
		  parseNbt_entity(chunkDimId, dimName+"-", "Mob:", tagList, false, false, entityList);
		}
	      }
	      break;

	    case 0x33:
	      // todo - this appears to be info on blocks that can move: water + lava + fire + sand + gravel
	      fprintf(control.fpLog,"%s 0x33 chunk (tick-list):\n", dimName.c_str());
	      parseNbt("0x33-tick: ", value, value_size, tagList);
	      // todo - parse tagList?
	      // todo - could show location of active fires
	      break;

	    case 0x34:
	      fprintf(control.fpLog,"%s 0x34 chunk (TODO - UNKNOWN RECORD)\n", dimName.c_str());
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
	      fprintf(control.fpLog,"%s 0x35 chunk (TODO - UNKNOWN RECORD)\n", dimName.c_str());
	      printKeyValue(key,key_size,value,value_size,false);
	      /*
		0x35 ?? -- both dimensions -- length 3,5,7,9,11 -- appears to be: b0 (count of items) b1..bn (2-byte ints) 
		-- there are 2907 in "another1"
		-- to examine data:
		cat xee | grep "WARNING: Unknown key size" | grep " 35\]" | cut -b75- | sort | nl
	      */
	      break;

	    case 0x76:
	      {
		// this record is not very interesting we usually hide it
		// note: it would be interesting if this is not == 2 (as of MCPE 0.12.x it is always 2)
		if ( control.verboseFlag || (value[0] != 2) ) { 
		  fprintf(control.fpLog,"%s 0x76 chunk (world format version): v=%d\n", dimName.c_str(), (int)(value[0]));
		}
	      }
	      break;

	    default:
	      fprintf(control.fpLog,"WARNING: %s unknown chunk - size=%d type=0x%x length=%d\n", dimName.c_str(),
		      key_size, chunkType, value_size);
	      printKeyValue(key,key_size,value,value_size,true);
	      if ( false ) {
		if ( value_size > 10 ) {
		  parseNbt("UNK: ", value, value_size, tagList);
		  // todo - parse tagList?
		}
	      }
	      break;
	    }
	  }
	  else {
	    fprintf(control.fpLog,"WARNING: Unknown chunk - key_size=%d value_size=%d\n", key_size, value_size);
	    printKeyValue(key,key_size,value,value_size,true);
	    if ( false ) { 
	      // try to nbt decode
	      fprintf(control.fpLog,"WARNING: Attempting NBT Decode:\n");
	      parseNbt("WARNING: ", value, value_size, tagList);
	      // todo - parse tagList?
	    }
	  }
	}
	fprintf(stderr,"Read %d records\n", recordCt);
	fprintf(stderr,"Status: %s\n", iter->status().ToString().c_str());
      
	assert(iter->status().ok());  // Check for any errors found during the scan

	delete iter;

	return leveldb::Status::OK();
      }
      
      int doOutput() {
	calcChunkBounds();
	for (int i=0; i < kDimIdCount; i++) {
	  chunkList[i].doOutput(db);
	}

	if ( control.doHtml ) {
	  char tmpstring[1025];

	  fprintf(stderr,"Do Output: html viewer\n");
	  
	  sprintf(tmpstring,"%s/mcpe_viz.html.template", dirExec);
	  std::string fnHtmlSrc = tmpstring;

	  sprintf(tmpstring,"%s/mcpe_viz.js", dirExec);
	  std::string fnJsSrc = tmpstring;

	  sprintf(tmpstring,"%s/mcpe_viz.css", dirExec);
	  std::string fnCssSrc = tmpstring;
	  
	  // create html file -- need to substitute one variable (extra js file)
	  StringReplacementList replaceStrings;
	  replaceStrings.push_back( std::make_pair( std::string("%JSFILE%"), std::string(mybasename(control.fnJs.c_str())) ) );
	  copyFileWithStringReplacement(fnHtmlSrc, control.fnHtml, replaceStrings);
	  
	  // create javascript file w/ filenames etc
	  FILE *fp = fopen(control.fnJs.c_str(),"w");
	  if ( fp ) {
	    fprintf(fp,
		    "// mcpe_viz javascript helper file -- created by mcpe_viz program\n"
		    "var dimensionInfo = {\n"
		    );
	    for (int did=0; did < kDimIdCount; did++) {
	      fprintf(fp, "\"%d\": {\n", did);
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

	      // todo - a diff geojson for overworld + nether?
	      fprintf(fp,"  fnGeoJSON: \"%s\",\n", mybasename(control.fnGeoJSON).c_str());
	      fprintf(fp,"  fnLayerTop: \"%s\",\n", mybasename(control.fnLayerTop[did]).c_str());
	      fprintf(fp,"  fnLayerBiome: \"%s\",\n", mybasename(control.fnLayerBiome[did]).c_str());
	      fprintf(fp,"  fnLayerHeight: \"%s\",\n", mybasename(control.fnLayerHeight[did]).c_str());
	      fprintf(fp,"  fnLayerBlockLight: \"%s\",\n", mybasename(control.fnLayerBlockLight[did]).c_str());
	      fprintf(fp,"  fnLayerGrass: \"%s\",\n", mybasename(control.fnLayerGrass[did]).c_str());
	      
	      fprintf(fp,"  listLayers: [\n");
	      for (int i=0; i < 128; i++) {
	        fprintf(fp, "    \"%s\",\n", mybasename(control.fnLayerRaw[did][i]).c_str());
	      }
	      fprintf(fp,"  ]\n");
	      if ( (did+1) < kDimIdCount ) {
		fprintf(fp,"},\n");
	      } else {
		fprintf(fp,"}\n");
	      }
	    }
	    fprintf(fp,"};\n");

	    // write color info
	    fprintf(fp,
		    "// a lookup table for identifying blocks in the web ui\n"
		    "// key is color (decimal), value is block name\n"
		    "// hacky? it sure is!\n"
		    );
	    
	    fprintf(fp,"blockColorLUT = {\n");
	    for (int i=0; i < 256; i++) {
	      if ( blockInfoList[i].lookupColorFlag ) {
		for (int sc=0; sc < 16; sc++) {
		  int xcolor =  standardColorInfo[sc].color + blockInfoList[i].lookupColorOffset;
		  std::string xname = blockInfoList[i].name + ", " + standardColorInfo[sc].name;
		  fprintf(fp,"\"%d\":\"%s\",\n", xcolor, xname.c_str());
		}
	      } else {
		if ( blockInfoList[i].colorSetFlag ) {
		  fprintf(fp,"\"%d\":\"%s\",\n", be32toh(blockInfoList[i].color), blockInfoList[i].name.c_str());
		}
	      }
	    }
	    // last, put the catch-all
	    fprintf(fp,"\"%d\": \"*UNKNOWN BLOCK*\" };\n",kColorDefault);

	    fclose(fp);
	    
	  } else {
	    // todo err
	    fprintf(stderr,"ERROR: Failed to open javascript output file (fn=%s)\n", control.fnJs.c_str());
	  }
	  
	  // copy helper files to destination directory
	  // todo -- mcpe_viz.js + mcpe_viz.css
	  std::string dirDest = mydirname(control.fnOutputBase);
	  
	  if ( dirDest.size() > 0 && dirDest != "." ) {
	    // todo are we sure this is a diff dir?
	    // todo check errors
	    sprintf(tmpstring,"%s/%s", dirDest.c_str(), mybasename(fnJsSrc).c_str());
	    std::string fnJsDest = tmpstring;
	    copyFile(fnJsSrc, fnJsDest);

	    sprintf(tmpstring,"%s/%s", dirDest.c_str(), mybasename(fnCssSrc).c_str());
	    std::string fnCssDest = tmpstring;
	    copyFile(fnCssSrc, fnCssDest);
	  } else {
	    // todo - warn user?
	  }
	  
	}
	
	return 0;
      }
    };

    MyWorld myWorld;

    void worldPointToImagePoint(int32_t dimId, float wx, float wz, int &ix, int &iy, bool geoJsonFlag) {
      // hack to avoid using wrong dim on pre-0.12 worlds
      if ( dimId < 0 ) { dimId = 0; }
      
      const int32_t chunkOffsetX = -myWorld.chunkList[dimId].minChunkX;
      const int32_t chunkOffsetZ = -myWorld.chunkList[dimId].minChunkZ;

      if ( geoJsonFlag ) {
	const int32_t chunkH = (myWorld.chunkList[dimId].maxChunkZ - myWorld.chunkList[dimId].minChunkZ + 1);
	const int32_t imageH = chunkH * 16;
	
	ix = wx + (chunkOffsetX * 16);
	// todo - correct calc here?
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

	MyTagList tagList;
	ret = parseNbt("level.dat: ", buf, bufLen, tagList);
	
	if ( ret == 0 ) {
	  nbt::tag_compound tc = tagList[0].second->as<nbt::tag_compound>();

	  worldSpawnX = tc["SpawnX"].as<nbt::tag_int>().get();
	  worldSpawnY = tc["SpawnY"].as<nbt::tag_int>().get();
	  worldSpawnZ = tc["SpawnZ"].as<nbt::tag_int>().get();
	  fprintf(stderr, "  Found World Spawn: x=%d y=%d z=%d\n", worldSpawnX, worldSpawnY, worldSpawnZ);
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

      fprintf(stderr,"  Level name is [%s]\n", (strlen(buf) > 0 ) ? buf : "(UNKNOWN)");
      fprintf(control.fpLog,"\nlevelname.txt: Level name is [%s]\n", (strlen(buf) > 0 ) ? buf : "(UNKNOWN)");
      fclose(fp);

      return 0;
    }

    
    int file_exists(const char* fn) {
      struct stat buf;
      int ret = stat(fn, &buf);
      return (ret == 0);
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
	    myWorld.chunkList[dimId].blockHideList.push_back(blockId);
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
	    myWorld.chunkList[dimId].blockForceTopList.push_back(blockId);
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


    int xmlGetInt(xmlNodePtr cur, const xmlChar* p, bool &valid) {
      valid=false;
      int ret;
      xmlChar* prop = xmlGetProp(cur,p);
      if ( prop ) {
	// see if it is hexadecimal
	if ( sscanf((char*)prop,"0x%x",&ret) == 1 ) {
	  xmlFree(prop);
	  valid=true;
	  return ret;
	}
	// try decimal
	if ( sscanf((char*)prop,"%d",&ret) == 1 ) {
	  xmlFree(prop);
	  valid=true;
	  return ret;
	}
	xmlFree(prop);
      }
      // todo - verbose only?
      // todo - show more context
      // fprintf(stderr,"WARNING: Failed xmlGetInt k=[%s]\n", (char*)p);
      return 0;
    }

    bool xmlGetBool(xmlNodePtr cur, const xmlChar* p, bool defaultValue, bool &valid) {
      valid = false;
      xmlChar* prop = xmlGetProp(cur,p);
      if ( prop ) {
	if ( strcasecmp((char*)prop,"true") ) {
	  valid = true;
	  xmlFree(prop);
	  return true;
	}
	if ( strcasecmp((char*)prop,"1") ) {
	  valid = true;
	  xmlFree(prop);
	  return true;
	}
	if ( strcasecmp((char*)prop,"false") ) {
	  valid = true;
	  xmlFree(prop);
	  return false;
	}
	if ( strcasecmp((char*)prop,"0") ) {
	  valid = true;
	  xmlFree(prop);
	  return false;
	}
	xmlFree(prop);
      }
      return defaultValue;
    }
    
    std::string xmlGetString(xmlNodePtr cur, const xmlChar* p, bool &valid) {
      valid = false;
      std::string s("(EMPTY)");
      xmlChar* prop = xmlGetProp(cur,p);
      if ( prop ) {
	s = (char*)prop;
	xmlFree(prop);
	valid=true;
      }
      return s;
    }
    
    int doParseXML_blocklist(xmlNodePtr cur) {
      cur = cur->xmlChildrenNode;
      while (cur != NULL) {
	if ( xmlStrcmp(cur->name, (const xmlChar *)"block") == 0 ) {

	  bool idValid, nameValid, colorValid, lookupColorFlagValid, lookupColorOffsetValid, solidFlagValid;
	  
	  int id = xmlGetInt(cur, (const xmlChar*)"id", idValid);
	  std::string name = xmlGetString(cur, (const xmlChar*)"name", nameValid);
	  int color = xmlGetInt(cur, (const xmlChar*)"color", colorValid);
	  bool lookupColorFlag = xmlGetBool(cur, (const xmlChar*)"lookupColor", false, lookupColorFlagValid);
	  int lookupColorOffset = xmlGetInt(cur, (const xmlChar*)"lookupColorOffset", lookupColorOffsetValid);
	  bool solidFlag = xmlGetBool(cur, (const xmlChar*)"solid", true, solidFlagValid);

	  // create data
	  if ( idValid && nameValid ) {
	    BlockInfo& b = blockInfoList[id].setName(name);
	    if ( colorValid ) {
	      b.setColor(color);
	    }
	    b.setLookupColorFlag(lookupColorFlag);
	    if ( lookupColorOffsetValid ) {
	      b.setLookupColorOffset(lookupColorOffset);
	    }
	    b.setSolidFlag(solidFlag);
	  } else {
	    // todo error
	    fprintf(stderr,"WARNING: Did not find valid id and name for block: (0x%x) (%s) (0x%x) (%s)\n"
		    , id
		    , name.c_str()
		    , color
		    , lookupColorFlag ? "true" : "false"
		    );
	  }
	}
	cur = cur->next;
      }
      return 0;
    }

    int doParseXML_standardcolorlist(xmlNodePtr cur) {
      cur = cur->xmlChildrenNode;
      while (cur != NULL) {
	if ( xmlStrcmp(cur->name, (const xmlChar *)"standardcolor") == 0 ) {

	  bool idValid, nameValid, colorValid;

	  int id = xmlGetInt(cur, (const xmlChar*)"id", idValid);
	  std::string name = xmlGetString(cur, (const xmlChar*)"name", nameValid);
	  int color = xmlGetInt(cur, (const xmlChar*)"color", colorValid);

	  // create data
	  if ( idValid && nameValid && colorValid ) {
	    if ( id >= 0 && id < 16 ) { 
	      standardColorInfo[id].name = name;
	      standardColorInfo[id].color = color;
	    } else {
	      fprintf(stderr,"WARNING: Found out of range standard color (id=%d)\n",id);
	    }
	  } else {
	    // todo error
	    fprintf(stderr,"WARNING: Did not find valid id, name and color for standardcolor: (0x%x) (0x%x)\n"
		    , id
		    , color
		    );
	  }
	}
	cur = cur->next;
      }
      return 0;
    }

    int doParseXML_itemlist(xmlNodePtr cur) {
      cur = cur->xmlChildrenNode;
      while (cur != NULL) {
	if ( xmlStrcmp(cur->name, (const xmlChar *)"item") == 0 ) {

	  bool idValid, nameValid;
	  
	  int id = xmlGetInt(cur, (const xmlChar*)"id", idValid);
	  std::string name = xmlGetString(cur, (const xmlChar*)"name", nameValid);

	  // create data
	  if ( idValid && nameValid ) {
	    itemInfoList.insert( std::make_pair(id, std::unique_ptr<ItemInfo>(new ItemInfo(name.c_str()))) );
	  } else {
	    // todo error
	    fprintf(stderr,"WARNING: Did not find valid id and name for item: (0x%x) (%s)\n"
		    , id
		    , name.c_str()
		    );
	  }
	}
	cur = cur->next;
      }
      return 0;
    }

    int doParseXML_entitylist(xmlNodePtr cur) {
      cur = cur->xmlChildrenNode;
      while (cur != NULL) {
	if ( xmlStrcmp(cur->name, (const xmlChar *)"entity") == 0 ) {

	  bool idValid, nameValid;
	  
	  int id = xmlGetInt(cur, (const xmlChar*)"id", idValid);
	  std::string name = xmlGetString(cur, (const xmlChar*)"name", nameValid);

	  // create data
	  if ( idValid && nameValid ) {
	    entityInfoList.insert( std::make_pair(id, std::unique_ptr<EntityInfo>(new EntityInfo(name.c_str()))) );
	  } else {
	    // todo error
	    fprintf(stderr,"WARNING: Did not find valid id and name for entity: (0x%x) (%s)\n"
		    , id
		    , name.c_str()
		    );
	  }
	}
	cur = cur->next;
      }
      return 0;
    }

    int doParseXML_enchantmentlist(xmlNodePtr cur) {
      cur = cur->xmlChildrenNode;
      while (cur != NULL) {
	if ( xmlStrcmp(cur->name, (const xmlChar *)"enchantment") == 0 ) {

	  bool idValid, nameValid, officialNameValid;
	  
	  int id = xmlGetInt(cur, (const xmlChar*)"id", idValid);
	  std::string name = xmlGetString(cur, (const xmlChar*)"name", nameValid);
	  std::string officialName = xmlGetString(cur, (const xmlChar*)"officialName", officialNameValid);
	  
	  // create data
	  if ( idValid && nameValid ) {
	    std::unique_ptr<EnchantmentInfo> b(new EnchantmentInfo(name.c_str()));
	    if ( officialNameValid ) {
	      b->setOfficialName(officialName);
	    }
	    enchantmentInfoList.insert( std::make_pair(id, std::move(b)) );
	  } else {
	    // todo error
	    fprintf(stderr,"WARNING: Did not find valid id and name for enchantment: (0x%x) (%s)\n"
		    , id
		    , name.c_str()
		    );
	  }
	}
	cur = cur->next;
      }
      return 0;
    }

      int doParseXML_biomelist(xmlNodePtr cur) {
      cur = cur->xmlChildrenNode;
      while (cur != NULL) {
	if ( xmlStrcmp(cur->name, (const xmlChar *)"biome") == 0 ) {

	  bool idValid, nameValid, colorValid;
	  
	  int id = xmlGetInt(cur, (const xmlChar*)"id", idValid);
	  std::string name = xmlGetString(cur, (const xmlChar*)"name", nameValid);
	  int color = xmlGetInt(cur, (const xmlChar*)"color", colorValid);

	  // create data
	  if ( idValid && nameValid ) {
	    std::unique_ptr<BiomeInfo> b(new BiomeInfo(name.c_str()));
	    if ( colorValid ) {
	      b->setColor(color);
	    }
	    biomeInfoList.insert( std::make_pair(id, std::move(b)) );
	  } else {
	    // todo error
	    fprintf(stderr,"WARNING: Did not find valid id and name for biome: (0x%x) (%s)\n"
		    , id
		    , name.c_str()
		    );
	  }
	}
	cur = cur->next;
      }
      return 0;
    }
    
    int doParseXML_xml(xmlNodePtr cur) {
      cur = cur->xmlChildrenNode;
      while (cur != NULL) {

	// todo - should count warning/errors and return this info
	
	if ( xmlStrcmp(cur->name, (const xmlChar *)"blocklist") == 0 ) {
	  doParseXML_blocklist(cur);
	}
	if ( xmlStrcmp(cur->name, (const xmlChar *)"standardcolorlist") == 0 ) {
	  doParseXML_standardcolorlist(cur);
	}
	if ( xmlStrcmp(cur->name, (const xmlChar *)"itemlist") == 0 ) {
	  doParseXML_itemlist(cur);
	}
	if ( xmlStrcmp(cur->name, (const xmlChar *)"entitylist") == 0 ) {
	  doParseXML_entitylist(cur);
	}
	if ( xmlStrcmp(cur->name, (const xmlChar *)"biomelist") == 0 ) {
	  doParseXML_biomelist(cur);
	}
	if ( xmlStrcmp(cur->name, (const xmlChar *)"enchantmentlist") == 0 ) {
	  doParseXML_enchantmentlist(cur);
	}
	
	cur = cur->next;
      }
      return 0;
    }
    
    int doParseXml( const std::string fn ) {
      xmlDocPtr doc;
      xmlNodePtr cur;

      if ( ! file_exists(fn.c_str()) ) {
	return -1;
      }
      
      doc = xmlParseFile(fn.c_str());
      if (doc == NULL ) {
	// fprintf(stderr,"ERROR: XML Document not parsed successfully.\n");
	return 1;
      }

      // todobig - use verboseflag to show all items as they are processed
      fprintf(stderr,"Reading XML from %s\n", fn.c_str());
      
      int ret = 2;
      cur = xmlDocGetRootElement(doc);
      while (cur != NULL) {
	if ( xmlStrcmp(cur->name, (const xmlChar *)"xml") == 0 ) {
	  ret = doParseXML_xml(cur);
	}
	cur = cur->next;
      }

      xmlFreeDoc(doc);
      xmlCleanupParser();
      
      return ret;
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
	      "  --html-all               Create html, javascript, and *all* image files to use as a fancy viewer (slow)\n"
	      //"  --dir-temp dir           Directory for temp files (useful for --slices, use a fast, local directory)\n"
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
	      // todo - re-enable when we use these:
	      "  --verbose                verbose output\n"
	      //"  --quiet                  supress normal output, continue to output warning and error messages\n"
	      "  --help                   this info\n"
	      );
    }

    // todo rename
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
    
    int parse_args ( int argc, char **argv, Control& control ) {

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

	{"shortrun", no_argument, NULL, '$'}, // this is just for testing
    
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
		myWorld.chunkList[dimId].blockHideList.push_back(blockId);
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
		myWorld.chunkList[dimId].blockForceTopList.push_back(blockId);
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

	case '=':
	  // html most
	  control.doHtml = true;
	  control.doImageBiome = 
	    control.doImageGrass = 
	    control.doImageHeightCol = 
	    control.doImageHeightColGrayscale =
	    control.doImageLightBlock = 
	    control.doImageLightSky = kDoOutputAll;
	  break;

	case '_':
	  // html all
	  control.doHtml = true;
	  control.doImageBiome = 
	    control.doImageGrass = 
	    control.doImageHeightCol = 
	    control.doImageHeightColGrayscale =
	    control.doImageLightBlock = 
	    control.doImageLightSky = kDoOutputAll;
	  control.doSlices = kDoOutputAll;
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

	case 'A':
	  control.doImageBiome = 
	    control.doImageGrass = 
	    control.doImageHeightCol = 
	    control.doImageHeightColGrayscale =
	    control.doImageLightBlock = 
	    control.doImageLightSky = parseDimIdOptArg(optarg);
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
      
	case 'v': 
	  control.verboseFlag = true; 
	  break;
	case 'q':
	  control.quietFlag = true;
	  //setLogLevelMask ( klogWarning | klogError | klogFatalError ); // set immediately
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

  }  // namespace
}  // namespace leveldb


  
int main ( int argc, char **argv ) {

  fprintf(stderr,"%s\n",mcpe_viz::version.c_str());

  dirExec = dirname(argv[0]);
  
  int ret = mcpe_viz::parse_args(argc, argv, mcpe_viz::control);
  if (ret != 0) {
    mcpe_viz::print_usage(basename(argv[0]));
    return ret;
  }

  ret = mcpe_viz::parseXml();
  if ( ret != 0 ) {
    fprintf(stderr,"ERROR: Failed to parse XML file.  Exiting...\n");
    fprintf(stderr,"** Hint: Make sure that mcpe_viz.xml is in any of: current dir, exec dir, ~/.mcpe_viz/\n");
    return -1;
  }
  
  mcpe_viz::parseConfigFile();
  
  ret = mcpe_viz::parseLevelFile(std::string(mcpe_viz::control.dirLeveldb + "/level.dat"));
  if ( ret != 0 ) {
    fprintf(stderr,"ERROR: Failed to parse level.dat file.  Exiting...\n");
    fprintf(stderr,"** Hint: --db must point to the dir which contains level.dat\n");
    return -1;
  }

  ret = mcpe_viz::parseLevelName(std::string(mcpe_viz::control.dirLeveldb + "/levelname.txt"));
  if ( ret != 0 ) {
    fprintf(stderr,"WARNING: Failed to parse levelname.txt file.\n");
    fprintf(stderr,"** Hint: --db must point to the dir which contains levelname.txt\n");
  }
  
  mcpe_viz::makePalettes();

  mcpe_viz::myWorld.dbOpen(std::string(mcpe_viz::control.dirLeveldb));

  // todo - we must do this, for now - we could get clever about this later
  if ( true || mcpe_viz::control.doDetailParseFlag ) {
    mcpe_viz::myWorld.parseDb();
  }

  mcpe_viz::myWorld.doOutput();

  mcpe_viz::myWorld.dbClose();
  
  fprintf(stderr,"Done.\n");

  return 0;
}
