/*
  MCPE World File Visualizer
  (c) Plethora777, 2015.9.26

  GPL'ed code - see LICENSE

  This requires Mojang's modified LevelDB library! (see README.md for details)
  This requires libnbt++ (see README.md for details)

  To build it, use cmake


  todobig

  * use "solid" info from block info to do something? could it fix the light map?

  * update block xml w/ transparency info - bool or int? or just solid/nonsolid?
  * use this transparency (or something else? e.g. spawnable flag?) to determine which block is the top block for purposes of the light map
  * go and look at wiki to see the type of info that is stored per block

  * produce another light map that shows areas that ARE spawnable? (e.g. use this to make an area 100% mob spawn proof)


  * convert all printf-style stuff to streams

  ** openlayers ui -- ability to hover over a pixel and get info (e.g. "Jungle Biome - Watermelon @ (X,Z,Y)"); switch image layers
  ** create vector layers to store info about items and entities; controls to filter display of these


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

#include <sys/stat.h>


#ifndef htobe32
// note: this is only used on win32 builds
// borrowed from /usr/include/bits/byteswap.h
/*
  #define htobe32(x)						\
  ((((x) & 0xff000000) >> 24) | (((x) & 0x00ff0000) >>  8) |	\
  (((x) & 0x0000ff00) <<  8) | (((x) & 0x000000ff) << 24))
*/
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


namespace mcpe_viz {
  namespace {

    static const std::string version("mcpe_viz v0.0.4 by Plethora777");

    enum {
      kDoOutputNone = -2,
      kDoOutputAll = -1
    };
      
    // all user options are stored here
    class Control {
    public:
      std::string dirLeveldb;
      std::string fnOutputBase;
      std::string fnCfg;
      std::string fnXml;
      std::string fnLog;
      bool doDetailParseFlag;
      int doMovie;
      int doGrid;
      int doImageBiome;
      int doImageGrass;
      int doImageHeightCol;
      int doImageHeightColGrayscale;
      // todo height using topBlockY
      int doImageBlockLight;
      int doImageSkyLight;
      bool shortRunFlag;
      bool verboseFlag;
      bool quietFlag;
      int movieX, movieY, movieW, movieH;

      bool fpLogNeedCloseFlag;
      FILE *fpLog;

      Control() {
	init();
      }
      ~Control() {
	if ( fpLogNeedCloseFlag ) {
	  fclose(fpLog);
	}
      }
  
      void init() {
	dirLeveldb = "";
	fnXml = "";
	fnOutputBase = "";
	fnLog = "";
	doDetailParseFlag = false;

	doMovie = kDoOutputNone;
	doGrid = kDoOutputNone;
	doImageBiome = kDoOutputNone;
	doImageGrass = kDoOutputNone;
	doImageHeightCol = kDoOutputNone;
	doImageHeightColGrayscale = kDoOutputNone;
	doImageBlockLight = kDoOutputNone;
	doImageSkyLight = kDoOutputNone;
	
	shortRunFlag = false;
	verboseFlag = false;
	quietFlag = false;
	movieX = movieY = movieW = movieH = 0;
	fpLogNeedCloseFlag = false;
	fpLog = stdout;
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
      }
    };

    Control control;

    
    // world types
    enum {
      kWorldIdOverworld = 0,
      kWorldIdNether = 1,
      kWorldIdCount = 2
    };

    // nbt parsing modes
    enum { 
      kNbtModePlain = 0,
      kNbtModeEntity = 100,
      kNbtModeItem = 200
    };

    // output image types
    enum {
      kImageModeTerrain = 0,
      kImageModeBiome = 1,
      kImageModeGrass = 2,
      kImageModeHeightCol = 3,
      kImageModeHeightColGrayscale = 4,
      kImageModeBlockLight = 5,
      kImageModeSkyLight = 6
    };
    
    int32_t standardColorInfo[16];

    // these are read from level.dat
    int32_t worldSpawnX = 0;
    int32_t worldSpawnY = 0;
    int32_t worldSpawnZ = 0;

    // palettes
    int32_t palRedBlackGreen[256];

    
    int myParseInt32(const char* p, int startByte) {
      int ret;
      memcpy(&ret, &p[startByte], 4);
      return ret;
    }

    int myParseInt8(const char* p, int startByte) {
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

    int _clamp(int v, int minv, int maxv) {
      if ( v < minv ) return minv;
      if ( v > maxv ) return maxv;
      return v;
    }
    
    int hsl2rgb ( double h, double s, double l, int &r, int &g, int &b ) {
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

    int makeHslRamp ( int *pal, int start, int stop, double h1, double h2, double s1, double s2, double l1, double l2 ) {
      double steps = stop-start+1;
      double dh = (h2 - h1) / steps;
      double ds = (s2 - s1) / steps;
      double dl = (l2 - l1) / steps;
      double h=h1,s=s1,l=l1;
      int r,g,b;
      for ( int i=start; i<=stop; i++ ) {
	hsl2rgb(h,s,l, r,g,b);
	int c = ((r & 0xff) << 16) | ((g & 0xff) << 8) | (b & 0xff);
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
	palRedBlackGreen[i] = 0xff00ff;
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
    
    
    class MyBlock {
    public:
      std::string name;
      int32_t color;
      bool lookupColorFlag;
      bool colorSetFlag;
      bool solidFlag;
      int colorSetNeedCount;
      MyBlock() {
	name = "(unknown)";
	setColor(0xff00ff); // purple
	lookupColorFlag = false;
	solidFlag = true;
	colorSetFlag = false;
	colorSetNeedCount = 0;
      }
      MyBlock& setName(const std::string s) {
	name = std::string(s);
	return *this;
      }
      MyBlock& setColor(int32_t rgb) {
	// note: we convert color storage to big endian so that we can memcpy when creating images
	color = htobe32(rgb);
	colorSetFlag = true;
	return *this;
      }
      MyBlock& setLookupColorFlag(bool f) {
	lookupColorFlag = f;
	return *this;
      }
      MyBlock& setSolidFlag(bool f) {
	solidFlag = f;
	return *this;
      }
      bool isSolid() { return solidFlag; }
    };

    MyBlock blockInfo[256];
    

    // todo - make these all subclasses of a common class
    class MyItem {
    public:
      std::string name;
      MyItem(const char* n) {
	setName(n);
      }
      MyItem& setName (const std::string s) {
	name = std::string(s);
	return *this;
      }
    };

    std::map<int, std::unique_ptr<MyItem> > itemInfo;


    class MyEntity {
    public:
      std::string name;
      MyEntity(const char* n) {
	setName(n);
      }
      MyEntity& setName (const std::string s) {
	name = std::string(s);
	return *this;
      }
    };

    std::map<int, std::unique_ptr<MyEntity> > entityInfo;


    class MyBiome {
    public:
      std::string name;
      int32_t color;
      bool colorSetFlag;
      MyBiome(const char* n) {
	setName(n);
	setColor(0xff00ff);
	colorSetFlag = false;
      }
      MyBiome(const char* n, int32_t rgb) {
	setName(n);
	setColor(rgb);
      }
      MyBiome& setName (const std::string s) {
	name = std::string(s);
	return *this;
      }
      MyBiome& setColor(int32_t rgb) {
	// note: we convert color storage to big endian so that we can memcpy when creating images
	color = htobe32(rgb);
	colorSetFlag=true;
	return *this;
      }
    };

    std::map<int, std::unique_ptr<MyBiome> > biomeInfo;


    class MyChunk {
    public:
      int chunkX, chunkZ;
      uint8_t blocks[16][16];
      uint8_t data[16][16];
      uint32_t grassAndBiome[16][16];
      uint8_t topBlockY[16][16];
      uint8_t heightCol[16][16];
      uint8_t topLight[16][16];
      MyChunk(int cx, int cz,
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


    class MyChunkList {
    public:
      std::string name;
      int worldId;
      std::vector< std::unique_ptr<MyChunk> > list;
      int minChunkX = 0, maxChunkX = 0;
      int minChunkZ = 0, maxChunkZ = 0;
      bool chunkBoundsValid;
      int chunkCount = 0;

      int histoChunkType[256];
      int histoGlobalBiome[256];

      std::vector<int> blockForceTopList;
      std::vector<int> blockHideList;
      
      MyChunkList() {
	name = "(UNKNOWN)";
	worldId = -1;
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
      
      void addToChunkBounds(int chunkX, int chunkZ) {
	minChunkX = std::min(minChunkX, chunkX);
	maxChunkX = std::max(maxChunkX, chunkX);
	minChunkZ = std::min(minChunkZ, chunkZ);
	maxChunkZ = std::max(maxChunkZ, chunkZ);
      }
      
      void putChunk ( int chunkX, int chunkZ,
		      const uint8_t* topBlock, const uint8_t* blockData,
		      const uint32_t* grassAndBiome, const uint8_t* topBlockY, const uint8_t* height, const uint8_t* topLight) {
	chunkCount++;
	
	minChunkX = std::min(minChunkX, chunkX);
	minChunkZ = std::min(minChunkZ, chunkZ);
	
	maxChunkX = std::max(maxChunkX, chunkX);
	maxChunkZ = std::max(maxChunkZ, chunkZ);

	std::unique_ptr<MyChunk> tmp(new MyChunk(chunkX, chunkZ, topBlock, blockData, grassAndBiome, topBlockY, height, topLight));
	list.push_back( std::move(tmp) );
      }

      
      bool checkDoForWorld(int v) {
	if ( v == kDoOutputAll ) {
	  return true;
	}
	if ( v == worldId ) {
	  return true;
	}
	return false;
      }
      
    
      void doOutputStats() {
	fprintf(control.fpLog,"\n%s Statistics:\n", name.c_str());
	fprintf(control.fpLog,"chunk-count: %d\n", chunkCount);
	fprintf(control.fpLog,"Min-dim:  %d %d\n", minChunkX, minChunkZ);
	fprintf(control.fpLog,"Max-dim:  %d %d\n", maxChunkX, maxChunkZ);
	int dx = (maxChunkX-minChunkX+1);
	int dz = (maxChunkZ-minChunkZ+1);
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
	if(!fp) abort();
      
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


      void generateImage(const std::string fname, int imageMode = kImageModeTerrain) {
	const int chunkOffsetX = -minChunkX;
	const int chunkOffsetZ = -minChunkZ;
	
	const int chunkW = (maxChunkX-minChunkX+1);
	const int chunkH = (maxChunkZ-minChunkZ+1);
	const int imageW = chunkW * 16;
	const int imageH = chunkH * 16;
	
	// note RGB pixels
	uint8_t *buf = new uint8_t[ imageW * imageH * 3 ];
	memset(buf, 0, imageW*imageH*3);

	int32_t color;
	const char *pcolor = (const char*)&color;
	for ( auto it = list.begin() ; it != list.end(); ++it) {
	  int imageX = ((*it)->chunkX + chunkOffsetX) * 16;
	  int imageZ = ((*it)->chunkZ + chunkOffsetZ) * 16;

	  int worldX = (*it)->chunkX * 16;
	  int worldZ = (*it)->chunkZ * 16;
	  
	  for (int cz=0; cz < 16; cz++) {
	    for (int cx=0; cx < 16; cx++) {

	      // todo - this conditional inside an inner loop, not so good
	      
	      if ( imageMode == kImageModeBiome ) {
		// get biome color
		int biomeId = (*it)->grassAndBiome[cx][cz] & 0xff;
		try { 
		  if ( biomeInfo.at(biomeId) ) {
		    color = biomeInfo[biomeId]->color;
		  }
		} catch (std::exception& e) {
		  // set an error color
		  fprintf(stderr,"ERROR: Unkown biome %d 0x%x\n", biomeId, biomeId);
		  color = htobe32(0xff2020);
		}
	      }
	      else if ( imageMode == kImageModeGrass ) {
		// get grass color
		int32_t grassColor = (*it)->grassAndBiome[cx][cz] >> 8;
		color = htobe32(grassColor);
	      }
	      else if ( imageMode == kImageModeHeightCol ) {
		// get height value and use red-black-green palette
		uint8_t c = (*it)->heightCol[cx][cz];
		color = htobe32(palRedBlackGreen[c]);
	      }
	      else if ( imageMode == kImageModeHeightColGrayscale ) {
		// get height value and make it grayscale
		uint8_t c = (*it)->heightCol[cx][cz];
		color = htobe32( (c << 16) | (c << 8) | c );
	      }
	      else if ( imageMode == kImageModeBlockLight ) {
		// get block light value and expand it (is only 4-bits)
		// todobig - why z/x here and x/z everywhere else... hmmm
		uint8_t c = ((*it)->topLight[cz][cx] & 0x0f) << 4;
		color = htobe32( (c << 16) | (c << 8) | c );
	      }
	      else if ( imageMode == kImageModeSkyLight ) {
		// get sky light value and expand it (is only 4-bits)
		// todobig - why z/x here and x/z everywhere else... hmmm
		uint8_t c = ((*it)->topLight[cz][cx] & 0xf0);
		color = htobe32( (c << 16) | (c << 8) | c );
	      }
	      else {
		// regular image
		int blockid = (*it)->blocks[cz][cx];
		
		if ( blockInfo[blockid].lookupColorFlag ) {
		  int blockdata = (*it)->data[cz][cx];
		  color = standardColorInfo[blockdata];
		} else {
		  color = blockInfo[blockid].color;
		  if ( ! blockInfo[blockid].colorSetFlag ) {
		    blockInfo[blockid].colorSetNeedCount++;
		  }
		}
	      }

	      // do grid lines
	      if ( checkDoForWorld(control.doGrid) && (cx==0 || cz==0) ) {
		if ( ((*it)->chunkX == 0) && ((*it)->chunkZ == 0) && (cx == 0) && (cz == 0) ) {
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

	      if ( worldId == 0 && imageMode == kImageModeTerrain ) {
		int ix = (imageX + cx);
		int iz = (imageZ + cz);
		int wx = (worldX + cx);
		int wz = (worldZ + cz);
		if ( (wx == 0) && (wz == 0) ) {
		  fprintf(stderr,"Info: World (0, 0) is at image (%d,%d)\n", ix,iz);
		}
		if ( (wx == worldSpawnX) && (wz == worldSpawnZ) ) {
		  fprintf(stderr,"Info: World Spawn (%d, %d) is at image (%d, %d)\n", worldSpawnX, worldSpawnZ, ix, iz);
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
	    if ( blockInfo[i].colorSetNeedCount ) {
	      fprintf(stderr,"Need pixel color for: 0x%x '%s' (%d)\n", i, blockInfo[i].name.c_str(), blockInfo[i].colorSetNeedCount);
	    }
	  }
	}
      }

      void generateMovie(leveldb::DB* db, const std::string fname) {
	const int chunkOffsetX = -minChunkX;
	const int chunkOffsetZ = -minChunkZ;
	
	//const int chunkW = (maxChunkX-minChunkX+1);
	//const int chunkH = (maxChunkZ-minChunkZ+1);
	//const int imageW = chunkW * 16;
	//const int imageH = chunkH * 16;

	int divisor = 1;
	if ( worldId == kWorldIdNether ) { 
	  // if nether, we divide coordinates by 8
	  divisor = 8; 
	}
	int cropX = control.movieX / divisor;
	int cropZ = control.movieY / divisor;
	int cropW = control.movieW / divisor;
	int cropH = control.movieH / divisor;
	  
	// note RGB pixels
	uint8_t* buf = new uint8_t[ cropW * cropH * 3 ];
	memset(buf, 0, cropW*cropH*3);

	std::vector<std::string> fileList;

	// todobig - we *could* write image data to flat files during parseDb and then convert these flat files into png here

	leveldb::ReadOptions readOptions;
	readOptions.fill_cache=false; // may improve performance?

	std::string svalue;
	const char* pchunk = nullptr;
	int pchunkX = 0;
	int pchunkZ = 0;
	
	int32_t color;
	const char *pcolor = (const char*)&color;
	for (int cy=0; cy < 128; cy++) {
	  // todo - make this part a func so that user can ask for specific slices from the cmdline?
	  fprintf(stderr,"  Layer %d\n", cy);
	  for ( auto it = list.begin() ; it != list.end(); ++it) {
	    int imageX = ((*it)->chunkX + chunkOffsetX) * 16;
	    int imageZ = ((*it)->chunkZ + chunkOffsetZ) * 16;

	    for (int cz=0; cz < 16; cz++) {
	      for (int cx=0; cx < 16; cx++) {

		int ix = (imageX + cx);
		int iz = (imageZ + cz);

		if ( (ix >= cropX) && (ix < (cropX + cropW)) &&
		     (iz >= cropZ) && (iz < (cropZ + cropH)) ) {

		  if ( pchunk==nullptr || (pchunkX != (*it)->chunkX) || (pchunkZ != (*it)->chunkZ) ) {
		    // get the chunk
		    // construct key
		    char keybuf[20];
		    int keybuflen;
		    int32_t kx = (*it)->chunkX, kz=(*it)->chunkZ, kw=worldId;
		    uint8_t kt=0x30;
		    switch (worldId) {
		    case 0:
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
		    pchunkX = (*it)->chunkX;
		    pchunkZ = (*it)->chunkZ;
		  }
		 
		  uint8_t blockid = getBlockId(pchunk, cx,cz,cy);

		  if ( blockid == 0 && ( cy > (*it)->topBlockY[cz][cx] ) ) {
		    // special handling for air -- keep existing value if we are above top block
		    // the idea is to show air underground, but hide it above so that the map is not all black pixels @ y=127
		  } else {
		    
		    if ( blockInfo[blockid].lookupColorFlag ) {
		      uint8_t blockdata = getBlockData(pchunk, cx,cz,cy);
		      color = standardColorInfo[blockdata];
		    } else {
		      color = blockInfo[blockid].color;
		    }
		  
		    // do grid lines
		    if ( checkDoForWorld(control.doGrid) && (cx==0 || cz==0) ) {
		      if ( ((*it)->chunkX == 0) && ((*it)->chunkZ == 0) && (cx == 0) && (cz == 0) ) {
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
	  fnameTmp += name;
	  fnameTmp += ".";
	  char xtmp[100];
	  sprintf(xtmp,"%03d",cy);
	  fnameTmp += xtmp;
	  fnameTmp += ".png";
	  outputPNG(fnameTmp, buf, cropW, cropH);
	  fileList.push_back(fnameTmp);
	}

	delete [] buf;

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

      int doOutput(leveldb::DB* db) {
	fprintf(stderr,"Do Output: %s\n",name.c_str());

	doOutputStats();
	
	fprintf(stderr,"Generate Image\n");
	generateImage(std::string(control.fnOutputBase + "." + name + ".map.png"));
	
	if ( checkDoForWorld(control.doImageBiome) ) {
	  fprintf(stderr,"Generate Biome Image\n");
	  generateImage(std::string(control.fnOutputBase + "." + name + ".biome.png"), kImageModeBiome);
	}
	if ( checkDoForWorld(control.doImageGrass) ) {
	  fprintf(stderr,"Generate Grass Image\n");
	  generateImage(std::string(control.fnOutputBase + "." + name + ".grass.png"), kImageModeGrass);
	}
	if ( checkDoForWorld(control.doImageHeightCol) ) {
	  fprintf(stderr,"Generate Height Col Image\n");
	  generateImage(std::string(control.fnOutputBase + "." + name + ".height_col.png"), kImageModeHeightCol);
	}
	if ( checkDoForWorld(control.doImageHeightColGrayscale) ) {
	  fprintf(stderr,"Generate Height Col (grayscale) Image\n");
	  generateImage(std::string(control.fnOutputBase + "." + name + ".height_col_grayscale.png"), kImageModeHeightColGrayscale);
	}
	if ( checkDoForWorld(control.doImageBlockLight) ) {
	  fprintf(stderr,"Generate Block Light Image\n");
	  generateImage(std::string(control.fnOutputBase + "." + name + ".light_block.png"), kImageModeBlockLight);
	}
	if ( checkDoForWorld(control.doImageSkyLight) ) {
	  fprintf(stderr,"Generate Sky Light Image\n");
	  generateImage(std::string(control.fnOutputBase + "." + name + ".light_sky.png"), kImageModeSkyLight);
	}

	if ( checkDoForWorld(control.doMovie) ) {
	  fprintf(stderr,"Generate movie\n");
	  generateMovie(db, std::string(control.fnOutputBase + "." + name + ".mp4"));
	}
	
	// reset
	for (int i=0; i < 256; i++) {
	  blockInfo[i].colorSetNeedCount = 0;
	}

	return 0;
      }
    };

    
    bool vectorContains( std::vector<int> &v, int i ) {
      for ( auto iter = std::begin(v); iter != std::end(v); iter++) {
	if ( *iter == i ) {
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
      char s[256];
      try { 
	if ( biomeInfo.at(idv) ) {
	  return biomeInfo[idv]->name;
	}
      } catch (std::exception& e) {
	sprintf(s,"ERROR: Failed to find biome id (%d)",idv);
	return std::string(s);
      }
      sprintf(s,"ERROR: Unknown biome id (%d)",idv);
      return std::string(s);
    }

    
    // todo - keep this to use with new nbt lib?
    int printIdTag(FILE *fout, int nbtMode, bool idTagFlag, int idv) {
      if ( idTagFlag ) {
	if ( idv >= 0 ) {
	  switch ( nbtMode ) {
	  case kNbtModeEntity:
	    try { 
	      if ( entityInfo.at(idv) ) {
		fprintf(fout, " [Entity: %s]", entityInfo[idv]->name.c_str());
	      }
	    } catch (std::exception& e) {
	      fprintf(fout, " [Entity: UNKNOWN (%d 0x%x) (%s)]",idv,idv,e.what());
	    }
	    break;
	  case kNbtModeItem:
	    try {
	      if ( itemInfo.at(idv) ) {
		fprintf(fout, " [Item: %s]", itemInfo[idv]->name.c_str());
	      }
	    } catch (std::exception& e) {
	      fprintf(fout, " [Item: UNKNOWN (%d 0x%x) (%s)]",idv,idv,e.what());
	    }
	    break;
	  }
	}
      }
      return 0;
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
	  for (auto itt = v.begin(); itt != v.end(); ++itt) {
	    if ( i++ > 0 ) { fprintf(control.fpLog," "); }
	    fprintf(control.fpLog,"%02x", (int)(*itt));
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
	  fprintf(control.fpLog,"LIST {\n");
	  indent++;
	  for ( auto it=v.begin(); it != v.end(); ++it ) {
	    std::unique_ptr<nbt::tag> t = it->get().clone();
	    parseNbtTag( hdr, indent, std::make_pair(std::string(""), std::move(t) ) );
	  }
	  if ( --indent < 0 ) { indent=0; }
	  fprintf(control.fpLog,"%s} LIST\n", makeIndent(indent,hdr).c_str());
	}
	break;
      case nbt::tag_type::Compound:
	{
	  nbt::tag_compound v = t.second->as<nbt::tag_compound>();
	  fprintf(control.fpLog,"COMPOUND {\n");
	  indent++;
	  for ( auto it=v.begin(); it != v.end(); ++it ) {
	    std::unique_ptr<nbt::tag> t = it->second.get().clone();
	    parseNbtTag( hdr, indent, std::make_pair( it->first, std::move(t) ) );
	  }
	  if ( --indent < 0 ) { indent=0; }
	  fprintf(control.fpLog,"%s} COMPOUND\n", makeIndent(indent,hdr).c_str());
	}
	break;
      case nbt::tag_type::Int_Array:
	{
	  nbt::tag_int_array v = t.second->as<nbt::tag_int_array>();
	  fprintf(control.fpLog,"[");
	  int i=0;
	  for (auto itt = v.begin(); itt != v.end(); ++itt) {
	    if ( i++ > 0 ) { fprintf(control.fpLog," "); }
	    fprintf(control.fpLog,"%x", (*itt));
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
      for ( auto itt = tagList.begin(); itt != tagList.end(); ++itt ) {
	parseNbtTag( hdr, indent, *itt );
      }
      
      fprintf(control.fpLog,"%sNBT Decode End (%d tags)\n",makeIndent(indent,hdr).c_str(), (int)tagList.size());

      return 0;
    }
    
    int printKeyValue(const char* key, int key_size, const char* value, int value_size, bool printKeyAsStringFlag) {
      fprintf(control.fpLog,"WARNING: Unknown key size (%d) k=[%s][", key_size, 
	      (printKeyAsStringFlag ? key : "(SKIPPED)"));
      for (int i=0; i < key_size; i++) {
	if ( i > 0 ) { fprintf(control.fpLog," "); }
	fprintf(control.fpLog,"%02x",((int)key[i] & 0xff));
      }
      fprintf(control.fpLog,"] v=[");
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
      
      MyChunkList chunkList[kWorldIdCount];
      
      MyWorld() {
	db = nullptr;
	dbOptions.compressors[0] = new leveldb::ZlibCompressor();
	dbOptions.create_if_missing = false;
	dbReadOptions.fill_cache = false;
	for (int i=0; i < kWorldIdCount; i++) {
	  chunkList[i].worldId = i;
	  chunkList[i].clearChunkBounds();
	}
	chunkList[kWorldIdOverworld].setName("overworld");
	chunkList[kWorldIdNether].setName("nether");
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
	for (int i=0; i < kWorldIdCount; i++) {
	  if ( ! chunkList[i].chunkBoundsValid ) {
	    passFlag = false;
	  }
	}
	if ( passFlag ) {
	  return 0;
	}

	// clear bounds
	for (int i=0; i < kWorldIdCount; i++) {
	  chunkList[i].clearChunkBounds();
	}

	int chunkX=-1, chunkZ=-1, chunkWorldId=-1, chunkType=-1;
	
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
	    chunkWorldId = myParseInt32(key, 8);
	    chunkType = myParseInt8(key, 12);
	    
	    // sanity checks
	    if ( chunkType == 0x30 ) {
	      chunkList[chunkWorldId].addToChunkBounds(chunkX, chunkZ);
	    }
	  }
	}

	assert(iter->status().ok());  // Check for any errors found during the scan
	delete iter;

	// mark bounds valid
	for (int i=0; i < kWorldIdCount; i++) {
	  chunkList[i].setChunkBoundsValid();
	}

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

	int chunkX=-1, chunkZ=-1, chunkWorldId=-1, chunkType=-1;
      
	calcChunkBounds();
	
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
	    parseNbt("Local Player: ", value, value_size, tagList);
	    // todo - parse tagList?
	    // todo - interesting tags: BedPositionX/Y/Z; Pos; Rotation
	    // todo - parse entity function?
	  }
	  else if ( (key_size>=7) && (strncmp(key,"player_",7) == 0) ) {
	    // note: key contains player id (e.g. "player_-1234")
	    std::string playerRemoteId = &key[strlen("player_")];
	    fprintf(control.fpLog,"Remote Player (id=%s) value:\n",playerRemoteId.c_str());
	    parseNbt("Remote Player: ", value, value_size, tagList);
	    // todo - parse tagList? similar to local player
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
	    parseNbt("portals: ", value, value_size, tagList);
	    // todo - parse tagList?
	  }
			 
	  else if ( key_size == 9 || key_size == 13 ) {

	    // this is probably a record we want to parse

	    std::string worldName; // todo - could use name from chunkList
	    if ( key_size == 9 ) {
	      chunkX = myParseInt32(key, 0);
	      chunkZ = myParseInt32(key, 4);
	      chunkWorldId = 0; // forced for overworld
	      chunkType = myParseInt8(key, 8);
	      worldName = "overworld";
	    }
	    else if ( key_size == 13 ) {
	      chunkX = myParseInt32(key, 0);
	      chunkZ = myParseInt32(key, 4);
	      chunkWorldId = myParseInt32(key, 8);
	      chunkType = myParseInt8(key, 12);
	      worldName = "nether";

	      // check for new world id's
	      if ( chunkWorldId != 1 ) {
		fprintf(stderr, "HEY! Found new chunkWorldId=0x%x\n", chunkWorldId);
	      }
	    }
	  
	    chunkList[chunkWorldId].histoChunkType[chunkType]++;

	    r = worldName + "-chunk: ";
	    sprintf(tmpstring,"%d %d (type=0x%02x)", chunkX, chunkZ, chunkType);
	    r += tmpstring;
	    if ( true ) {
	      // show approximate image coordinates for chunk
	      int chunkOffsetX = -chunkList[chunkWorldId].minChunkX;
	      int chunkOffsetZ = -chunkList[chunkWorldId].maxChunkZ;
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
		    if ( topBlock[cz][cx] == 0 ||
			 vectorContains(chunkList[chunkWorldId].blockHideList, topBlock[cz][cx]) ||
			 vectorContains(chunkList[chunkWorldId].blockForceTopList, blockId) ) {
		      topBlock[cz][cx] = blockId;
		      topData[cz][cx] = getBlockData(value, cx,cz,cy);
		      topSkyLight[cz][cx] = getBlockSkyLight(value, cx,cz,cy);
		      topBlockLight[cz][cx] = getBlockBlockLight(value, cx,cz,cy);
		      topBlockY[cz][cx] = cy;
		    }

		    // todo - testing
#if 1
		    // todo - this does not quite work yet; need to get block light on block above first n SOLID block
		    if ( topBlock[cz][cx] == 0 ) {
		      // note: we store blocklight from air block ABOVE top block; use this for block light image
		      uint8_t sl = getBlockSkyLight(value, cx,cz,cy);
		      uint8_t bl = getBlockBlockLight(value, cx,cz,cy);	
		      // we combine the light nibbles into a byte
		      topLight[cz][cx] = (sl << 4) | bl;
		    }
#endif
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
		  int ty = colData1[cz][cx];
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
		  chunkList[chunkWorldId].histoGlobalBiome[biomeId]++;
		  grassAndBiome[cz][cx] = rawData;
		  fprintf(control.fpLog,"%02x:%x:%02x ", (int)topBlock[cz][cx], (int)topData[cz][cx], (int)biomeId);
		}
		fprintf(control.fpLog,"\n");
	      }
	      fprintf(control.fpLog,"Block Histogram:\n");
	      for (int i=0; i < 256; i++) {
		if ( histo[i] > 0 ) {
		  fprintf(control.fpLog,"%s-hg: %02x: %6d (%s)\n", worldName.c_str(), i, histo[i], blockInfo[i].name.c_str());
		}
	      }
	      fprintf(control.fpLog,"Biome Histogram:\n");
	      for (int i=0; i < 256; i++) {
		if ( histoBiome[i] > 0 ) {
		  std::string biomeName( getBiomeName(i) );
		  fprintf(control.fpLog,"%s-hg-biome: %02x: %6d (%s)\n", worldName.c_str(), i, histoBiome[i], biomeName.c_str());
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
	      chunkList[chunkWorldId].putChunk(chunkX, chunkZ,
					       &topBlock[0][0], &topData[0][0],
					       &grassAndBiome[0][0], &topBlockY[0][0],
					       &colData1[0][0], &topLight[0][0]);

	      break;

	    case 0x31:
	      fprintf(control.fpLog,"%s 0x31 chunk (tile entity data):\n", worldName.c_str());
	      parseNbt("0x31-te: ", value, value_size, tagList);
	      // todo - parse tagList?
	      break;

	    case 0x32:
	      fprintf(control.fpLog,"%s 0x32 chunk (entity data):\n", worldName.c_str());
	      parseNbt("0x32-e: ", value, value_size, tagList);
	      // todo - parse tagList?
	      break;

	    case 0x33:
	      // todo - this appears to be info on blocks that can move: water + lava + fire + sand + gravel
	      fprintf(control.fpLog,"%s 0x33 chunk (tick-list):\n", worldName.c_str());
	      parseNbt("0x33-tick: ", value, value_size, tagList);
	      // todo - parse tagList?
	      break;

	    case 0x34:
	      fprintf(control.fpLog,"%s 0x34 chunk (TODO - UNKNOWN RECORD)\n", worldName.c_str());
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
	      fprintf(control.fpLog,"%s 0x35 chunk (TODO - UNKNOWN RECORD)\n", worldName.c_str());
	      printKeyValue(key,key_size,value,value_size,false);
	      /*
		0x35 ?? -- both worlds -- length 3,5,7,9,11 -- appears to be: b0 (count of items) b1..bn (2-byte ints) 
		-- there are 2907 in "another1"
		-- to examine data:
		cat xee | grep "WARNING: Unknown key size" | grep " 35\]" | cut -b75- | sort | nl
	      */
	      break;

	    case 0x76:
	      {
		// this record is not very interesting we usually hide it
		// todo - it would be interesting if this is not == 2 (as of MCPE 0.12.x it is always 2)
		if ( control.verboseFlag || (value[0] != 2) ) { 
		  fprintf(control.fpLog,"%s 0x76 chunk (world format version): v=%d\n", worldName.c_str(), (int)(value[0]));
		}
	      }
	      break;

	    default:
	      fprintf(control.fpLog,"WARNING: %s unknown chunk - size=%d type=0x%x length=%d\n", worldName.c_str(),
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
	    fprintf(control.fpLog,"WARNING: Unknown key size (%d)\n", key_size);
	    printKeyValue(key,key_size,value,value_size,true);
	    // try to nbt decode
	    fprintf(control.fpLog,"WARNING: Attempting NBT Decode:\n");
	    parseNbt("WARNING: ", value, value_size, tagList);
	    // todo - parse tagList?
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
	for (int i=0; i < kWorldIdCount; i++) {
	  chunkList[i].doOutput(db);
	}
	return 0;
      }
    };

    MyWorld myWorld;

    
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
	  fprintf(stderr, "Found World Spawn: x=%d y=%d z=%d\n", worldSpawnX, worldSpawnY, worldSpawnZ);
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

      fprintf(stderr,"Level name is [%s]\n", (strlen(buf) > 0 ) ? buf : "(UNKNOWN)");
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
	  int worldId = -1;
	  int blockId = -1;
	  int pass = false;
	  if ( sscanf(&p[9],"%d 0x%x", &worldId, &blockId) == 2 ) {
	    pass = true;
	  }
	  else if ( sscanf(&p[9],"%d %d", &worldId, &blockId) == 2 ) {
	    pass = true;
	  }
	  // check worldId
	  if ( worldId < kWorldIdOverworld || worldId >= kWorldIdCount ) {
	    pass = false;
	  }
	  if ( pass ) {
	    // add to hide list
	    fprintf(stderr,"INFO: Adding 'hide-top' block: worldId=%d blockId=%3d (0x%02x) (%s)\n", worldId, blockId, blockId, blockInfo[blockId].name.c_str());
	    myWorld.chunkList[worldId].blockHideList.push_back(blockId);
	  } else {
	    fprintf(stderr,"ERROR: Failed to parse cfg item 'hide-top': [%s]\n", buf);
	  }
	}

	else if ( (p=strstr(buf,"force-top:")) ) {
	  int worldId = -1;
	  int blockId = -1;
	  int pass = false;
	  if ( sscanf(&p[10],"%d 0x%x", &worldId, &blockId) == 2 ) {
	    pass = true;
	  }
	  else if ( sscanf(&p[10],"%d %d", &worldId, &blockId) == 2 ) {
	    pass = true;
	  }
	  // check worldId
	  if ( worldId < kWorldIdOverworld || worldId >= kWorldIdCount ) {
	    pass = false;
	  }
	  if ( pass ) {
	    // add to hide list
	    fprintf(stderr,"INFO: Adding 'force-top' block: worldId=%d blockId=%3d (0x%02x) (%s)\n", worldId, blockId, blockId, blockInfo[blockId].name.c_str());
	    myWorld.chunkList[worldId].blockForceTopList.push_back(blockId);
	  } else {
	    fprintf(stderr,"ERROR: Failed to parse cfg item 'hide': [%s]\n", buf);
	  }
	}

	else {
	  if ( strlen(buf) > 0 ) {
	    fprintf(stderr,"WARNING: Unparsed config line: [%s]\n",buf);
	  }
	}
	
      }

      fclose(fp);
      return 0;
    }

    
    int parseConfigFile( char** argv ) {
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
      fn = dirname(argv[0]);
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

	  bool idValid, nameValid, colorValid, lookupColorFlagValid, solidFlagValid;
	  
	  int id = xmlGetInt(cur, (const xmlChar*)"id", idValid);
	  std::string name = xmlGetString(cur, (const xmlChar*)"name", nameValid);
	  int color = xmlGetInt(cur, (const xmlChar*)"color", colorValid);
	  bool lookupColorFlag = xmlGetBool(cur, (const xmlChar*)"lookupColor", false, lookupColorFlagValid);
	  bool solidFlag = xmlGetBool(cur, (const xmlChar*)"solid", true, solidFlagValid);

	  // create data
	  if ( idValid && nameValid ) {
	    MyBlock& b = blockInfo[id].setName(name);
	    if ( colorValid ) {
	      b.setColor(color);
	    }
	    b.setLookupColorFlag(lookupColorFlag);
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

	  bool idValid, colorValid;
	  
	  int id = xmlGetInt(cur, (const xmlChar*)"id", idValid);
	  int color = xmlGetInt(cur, (const xmlChar*)"color", colorValid);

	  // create data
	  if ( idValid && colorValid ) {
	    standardColorInfo[id] = htobe32(color);
	  } else {
	    // todo error
	    fprintf(stderr,"WARNING: Did not find valid id and color for standardcolor: (0x%x) (0x%x)\n"
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
	    itemInfo.insert( std::make_pair(id, std::unique_ptr<MyItem>(new MyItem(name.c_str()))) );
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
	    entityInfo.insert( std::make_pair(id, std::unique_ptr<MyEntity>(new MyEntity(name.c_str()))) );
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
	    std::unique_ptr<MyBiome> b(new MyBiome(name.c_str()));
	    if ( colorValid ) {
	      b->setColor(color);
	    }
	    biomeInfo.insert( std::make_pair(id, std::move(b)) );
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

    int parseXml ( char** argv ) {
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
      fn = dirname(argv[0]);
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
	      "  --detail                 Log extensive details about the world to the log file\n"
	      "\n"
	      "  (note: [wid] is optional world-id - if not specified, do all worlds; 0=Overworld; 1=Nether)\n"
	      "  --grid [wid]             Display chunk grid on top of world\n"
	      "\n"
	      "  --all-image [wid]        Create all image types\n"
	      "  --biome [wid]            Create a biome map image\n"
	      "  --grass [wid]            Create a grass color map image\n"
	      "  --height-col [wid]       Create a height column map image (red is below sea; gray is sea; green is above sea)\n"
	      "  --height-col-gs [wid]    Create a height column map image (grayscale)\n"
	      "  --blocklight [wid]       Create a block light map image\n"
	      "  --skylight [wid]         Create a sky light map image\n"
	      "\n"
	      "  --movie [wid]            Create movie of layers of overworld\n"
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
    int parseWorldIdOptArg(const char* arg) {
      if ( arg ) {
	// sanity check
	int wid = atoi(arg);
	if ( wid >= kWorldIdOverworld && wid < kWorldIdCount ) {
	  return wid;
	} else {
	  fprintf(stderr,"ERROR: Invalid world-id supplied (%d), defaulting to Overworld only\n", wid);
	  wid=kWorldIdOverworld;
	}
	return wid;
      } else {
	// if no arg, we want output for all worlds
	return kDoOutputAll;
      }
    }
    
    int parse_args ( int argc, char **argv, Control& control ) {

      static struct option longoptlist[] = {
	{"db", required_argument, NULL, 'D'},
	{"out", required_argument, NULL, 'O'},

	{"xml", required_argument, NULL, 'X'},
	{"log", required_argument, NULL, 'L'},

	{"detail", no_argument, NULL, '@'},
	
	{"all-image", optional_argument, NULL, 'A'},
	{"biome", optional_argument, NULL, 'B'},
	{"grass", optional_argument, NULL, 'g'},
	{"height-col", optional_argument, NULL, 'd'},
	{"height-col-gs", optional_argument, NULL, '#'},
	{"blocklight", optional_argument, NULL, 'b'},
	{"skylight", optional_argument, NULL, 's'},
    
	{"movie", optional_argument, NULL, 'M'},
	{"movie-dim", required_argument, NULL, '*'},
	
	{"grid", optional_argument, NULL, 'G'},

	{"shortrun", no_argument, NULL, '$'}, // this is just for testing
    
	{"verbose", no_argument, NULL, 'v'},
	{"quiet", no_argument, NULL, 'q'},
	{"help", no_argument, NULL, 'H'},
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

	case 'G':
	  control.doGrid = parseWorldIdOptArg(optarg);
	  break;

	case 'B':
	  control.doImageBiome = parseWorldIdOptArg(optarg);
	  break;
	case 'g':
	  control.doImageGrass = parseWorldIdOptArg(optarg);
	  break;
	case 'd':
	  control.doImageHeightCol = parseWorldIdOptArg(optarg);
	  break;
	case '#':
	  control.doImageHeightColGrayscale = parseWorldIdOptArg(optarg);
	  break;
	case 'b':
	  control.doImageBlockLight = parseWorldIdOptArg(optarg);
	  break;
	case 's':
	  control.doImageSkyLight = parseWorldIdOptArg(optarg);
	  break;

	case 'A':
	  control.doImageBiome = parseWorldIdOptArg(optarg);
	  control.doImageGrass = parseWorldIdOptArg(optarg);
	  control.doImageHeightCol = parseWorldIdOptArg(optarg);
	  control.doImageHeightColGrayscale = parseWorldIdOptArg(optarg);
	  control.doImageBlockLight = parseWorldIdOptArg(optarg);
	  control.doImageSkyLight = parseWorldIdOptArg(optarg);
	  break;
      
	case 'M':
	  control.doMovie = parseWorldIdOptArg(optarg);
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
	case 'H':
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
  
  int ret = mcpe_viz::parse_args(argc, argv, mcpe_viz::control);
  if (ret != 0) {
    mcpe_viz::print_usage(basename(argv[0]));
    return ret;
  }

  ret = mcpe_viz::parseXml(argv);
  if ( ret != 0 ) {
    fprintf(stderr,"ERROR: Failed to parse an XML file.  Exiting...\n");
    fprintf(stderr,"** Hint: Make sure that mcpe_viz.xml is in any of: current dir, exec dir, ~/.mcpe_viz/\n");
    return -1;
  }
  
  mcpe_viz::parseConfigFile(argv);
  
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
