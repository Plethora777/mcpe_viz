/*
  MCPE World File Visualizer
  (c) Plethora777, 2015.9.26

  GPL'ed code - see LICENSE

  This requires Mojang's modified LevelDB library! (see README.md for details)

  To build it, use cmake or do something like this:
  
  g++ --std=c++11 -DDLLX= -I. -Ileveldb-mcpe/include -I.. -std=c++0x -fno-builtin-memcmp -pthread -DOS_LINUX -DLEVELDB_PLATFORM_POSIX -DLEVELDB_ATOMIC_PRESENT -O2 -DNDEBUG -c mcpe_viz.cc -o mcpe_viz.o ; g++ -pthread mcpe_viz.o leveldb-mcpe/libleveldb.a -lz -o mcpe_viz 

  todo

  ** cmdline options:
  filtering stuff w/ decimal or hex (e.g. torches)
  highlighting per world
  presets for filtering?
  save set of slices
  save a particular slice
  draw text on slice files (e.g. Y)
  logfiles for overworld + nether + unknown

  ** maps/instructions to get from point A (e.g. spawn) to biome type X (in blocks but also in landmark form: over 2 seas left at the birch forest etc)
  
  */

#include <stdio.h>
#include <libgen.h>
#include <map>
#include <vector>
#include <libxml/xmlreader.h>
#include <png.h>
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



// all user options are stored here
class Control {
public:
  std::string dirLeveldb;
  std::string fnOutputBase;
  std::string fnCfg;
  std::string fnXml;
  std::string fnLog;
  bool doMovieFlag;
  bool doMovieNetherFlag;
  bool doGridFlag;
  bool doBiomeImageFlag;
  bool shortRunFlag;
  bool verboseFlag;
  bool quietFlag;
  int movieX, movieY, movieW, movieH;

  bool fpLogNeedCloseFlag;
  FILE *fpLog;
  
  Control() {
    fpLog = stdout;
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
    doMovieFlag = false;
    doMovieNetherFlag = false;
    doGridFlag = false;
    doBiomeImageFlag = false;
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


namespace leveldb {
  namespace {

    static const std::string version("mcpe_viz v0.0.1 by Plethora777");

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

    int histoChunkType[kWorldIdCount][256];
    int histoGlobalBiome[kWorldIdCount][256];
    
    int32_t colorInfo[16];

    // these are read from level.dat
    int worldSpawnX = 0;
    int worldSpawnY = 0;
    int worldSpawnZ = 0;

    std::vector<int> blockForceTopList[kWorldIdCount];
    std::vector<int> blockHideList[kWorldIdCount];

    
    // todo - rename classes more sanely
    class MyBlock {
    public:
      std::string name;
      int32_t color;
      bool lookupColorFlag;
      bool colorSetFlag;
      int colorSetNeedCount;
      MyBlock() {
	name = "(unknown)";
	setColor(0xff00ff); // purple
	lookupColorFlag = false;
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
      MyBlock& setLookupColor() {
	lookupColorFlag = true;
	return *this;
      }
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

    class MyChunk {
    public:
      int chunkX, chunkZ;
      uint8_t blocks[16][16];
      uint8_t data[16][16];
      uint8_t biome[16][16];
      uint8_t blocks_all[16][16][128];
      uint8_t topBlockY[16][16];
      MyChunk(int cx, int cz,
	      const uint8_t* b, const uint8_t* d, const uint8_t* xbiome, const uint8_t* b_all, const uint8_t* tby) {
	chunkX=cx;
	chunkZ=cz;
	memcpy(blocks, b, 16*16);
	memcpy(data, d, 16*16);
	memcpy(biome, xbiome, 16*16);
	// todo - we could skip blocks_all if we are not doing a movie
	memcpy(blocks_all, b_all, 16*16*128);
	memcpy(topBlockY, tby, 16*16);
      }
    };

    std::map<int, std::unique_ptr<MyItem> > itemInfo;
    std::map<int, std::unique_ptr<MyEntity> > entityInfo;
    std::map<int, std::unique_ptr<MyBiome> > biomeInfo;

    void initBlockInfo() {
      // clear histograms
      for (int i=0; i < kWorldIdCount; i++) {
	memset(histoChunkType[i],0,256*sizeof(int));
	memset(histoGlobalBiome[i],0,256*sizeof(int));
      }
      
      // highlight blocks - that is, force them to be in topblock
      // for example, mob spawners:
      //blockForceTopList[0].push_back(0x34);
      //blockForceTopList[1].push_back(0x34);

      // hide certain blocks from topBlock
      // topBlock in the nether is nuts.  So that we can see some interesting info, let's hide: bedrock netherrack lava
      //blockHideList[kWorldIdNether].push_back(0x07);
      //blockHideList[kWorldIdNether].push_back(0x57);
      //blockHideList[kWorldIdNether].push_back(0x0a);
      //blockHideList[kWorldIdNether].push_back(0x0b);
    }

    class MyChunkList {
    public:
      std::vector< std::unique_ptr<MyChunk> > list;
      int minChunkX = 0, maxChunkX = 0;
      int minChunkZ = 0, maxChunkZ = 0;
      int chunkCount = 0;

      void putChunk ( int chunkX, int chunkZ,
		      const uint8_t* topBlock, const uint8_t* blockData,
		      const uint8_t* blockBiome, const uint8_t* blocks_all,
		      const uint8_t* topBlockY) {
	chunkCount++;
	
	minChunkX = std::min(minChunkX, chunkX);
	minChunkZ = std::min(minChunkZ, chunkZ);
	
	maxChunkX = std::max(maxChunkX, chunkX);
	maxChunkZ = std::max(maxChunkZ, chunkZ);

	std::unique_ptr<MyChunk> tmp(new MyChunk(chunkX, chunkZ, topBlock, blockData, blockBiome, blocks_all, topBlockY));
	list.push_back( std::move(tmp) );
      }

    
      void myreport(int worldId) {
	fprintf(control.fpLog,"\nStatistics:\n");
	fprintf(control.fpLog,"chunk-count: %d\n", chunkCount);
	fprintf(control.fpLog,"Min-dim:  %d %d\n", minChunkX, minChunkZ);
	fprintf(control.fpLog,"Max-dim:  %d %d\n", maxChunkX, maxChunkZ);
	int dx = (maxChunkX-minChunkX+1);
	int dz = (maxChunkZ-minChunkZ+1);
	fprintf(control.fpLog,"diff-dim: %d %d\n", dx, dz);
	fprintf(control.fpLog,"pixels:   %d %d\n", dx*16, dz*16);

	fprintf(control.fpLog,"\nGlobal Chunk Type Histogram:\n");
	for (int i=0; i < 256; i++) {
	  if ( histoChunkType[worldId][i] > 0 ) {
	    fprintf(control.fpLog,"hg-chunktype: %02x %6d\n", i, histoChunkType[worldId][i]);
	  }
	}

	fprintf(control.fpLog,"\nGLobal Biome Histogram:\n");
	for (int i=0; i < 256; i++) {
	  if ( histoGlobalBiome[worldId][i] > 0 ) {
	    fprintf(control.fpLog,"hg-globalbiome: %02x %6d\n", i, histoGlobalBiome[worldId][i]);
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

	// todo - png_destroy_xxx calls here?
	png_destroy_write_struct(&png, &info);
	//png_destroy_info_struct(&png, &info);

	free(row_pointers);
      
	fclose(fp);

	return 0;
      }


      void generateImage(const std::string fname, bool biomeImageFlag = false) {
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

	      if ( biomeImageFlag ) {

		// get biome color
		int biomeId = (*it)->biome[cx][cz] & 0xff;
		try { 
		  if ( biomeInfo.at(biomeId) ) {
		    color = biomeInfo[biomeId]->color;
		  }
		} catch (std::exception& e) {
		  // set an error color
		  fprintf(stderr,"ERROR: Unkown biome %d 0x%x\n", biomeId, biomeId);
		  color = htobe32(0xff2020);
		}
		
	      } else {
		// regular image
		int blockid = (*it)->blocks[cz][cx];
		
		if ( blockInfo[blockid].lookupColorFlag ) {
		  int blockdata = (*it)->data[cz][cx];
		  color = colorInfo[blockdata];
		} else {
		  color = blockInfo[blockid].color;
		  if ( ! blockInfo[blockid].colorSetFlag ) {
		    blockInfo[blockid].colorSetNeedCount++;
		  }
		}
	      }

	      // do grid lines
	      if ( control.doGridFlag && (cx==0 || cz==0) ) {
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
	
	// output the image
	outputPNG(fname, buf, imageW, imageH);

	delete [] buf;

	if ( biomeImageFlag ) {
	} else {
	  for (int i=0; i < 256; i++) {
	    if ( blockInfo[i].colorSetNeedCount ) {
	      fprintf(stderr,"Need pixel color for: %x '%s' (%d)\n", i, blockInfo[i].name.c_str(), blockInfo[i].colorSetNeedCount);
	    }
	  }
	}
      }

      void generateImageSlices(const std::string fname, const std::string fnameTmpBase, bool netherFlag) {
	const int chunkOffsetX = -minChunkX;
	const int chunkOffsetZ = -minChunkZ;
	
	//const int chunkW = (maxChunkX-minChunkX+1);
	//const int chunkH = (maxChunkZ-minChunkZ+1);
	//const int imageW = chunkW * 16;
	//const int imageH = chunkH * 16;

	int divisor = 1;
	if ( netherFlag ) { 
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
	
	int32_t color;
	const char *pcolor = (const char*)&color;
	for (int cy=0; cy < 128; cy++) {
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
		     
		  int blockid = (*it)->blocks_all[cx][cz][cy];
		
		  if ( blockid == 0 && ( cy > (*it)->topBlockY[cz][cx] ) ) {
		    // special handling for air -- keep existing value if we are above top block
		    // the idea is to show air underground, but hide it above so that the map is not all black pixels @ y=127
		  } else {
		    
		    if ( blockInfo[blockid].lookupColorFlag ) {
		      int blockdata = (*it)->data[cz][cx];
		      color = colorInfo[blockdata];
		    } else {
		      color = blockInfo[blockid].color;
		    }
		    
		    // do grid lines
		    if ( control.doGridFlag && (cx==0 || cz==0) ) {
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
	  fnameTmp += fnameTmpBase;
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
	fnameTmp += fnameTmpBase;
	fnameTmp += ".%03d.png";
	
	std::string cmdline = std::string("/usr/bin/ffmpeg -y -framerate 1 -i " + fnameTmp + " -c:v libx264 -r 30 ");
	cmdline += fname;
	int ret = system(cmdline.c_str());
	if ( ret != 0 ) {
	  fprintf(stderr,"Failed to create movie ret=(%d) cmd=(%s)\n",ret,cmdline.c_str());
	}

	// todo - delete temp files? cmdline option to NOT delete
	
      }
      
    };
    
    
    MyChunkList chunkList[kWorldIdCount];

    
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
	  fprintf(control.fpLog,"%d (byte)\n", v.get());
	}
	break;
      case nbt::tag_type::Short:
	{
	  nbt::tag_short v = t.second->as<nbt::tag_short>();
	  fprintf(control.fpLog,"%d (short)\n", v.get());
	}
	break;
      case nbt::tag_type::Int:
	{
	  nbt::tag_int v = t.second->as<nbt::tag_int>();
	  fprintf(control.fpLog,"%d (int)\n", v.get());
	}
	break;
      case nbt::tag_type::Long:
	{
	  nbt::tag_long v = t.second->as<nbt::tag_long>();
	  fprintf(control.fpLog,"%ld (long)\n", v.get());
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
	  // todo nbt::tag_double v = t.second->as<nbt::tag_double>();
	  fprintf(control.fpLog,"[TODO] (byte array)\n");
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
	  // todo nbt::tag_double v = t.second->as<nbt::tag_double>();
	  fprintf(control.fpLog,"[TODO] (int array)\n");
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
    
    int myParseInt32_T(const char* p, int startByte) {
      int ret;
      memcpy(&ret, &p[startByte], 4);
      return ret;
    }

    int myParseInt8(const char* p, int startByte) {
      return (p[startByte] & 0xff);
    }

    inline int _calcOffset(int x, int z, int y) {
      // todo - correct calc here? shouldn't it be z*16?!
      return (((x*16) + z)*128) + y;
    }
    
    inline int _calcOffset(int x, int z) {
      // todo - correct calc here? shouldn't it be z*16?!
      return (x*16) + z;
    }

    int getBlockId(const char* p, int x, int z, int y) {
      return (p[_calcOffset(x,z,y)] & 0xff);
    }

    int getBlockData(const char* p, int x, int z, int y) {
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

    int getBlockSkyLight(const char* p, int x, int z, int y) {
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

    int getBlockBlockLight(const char* p, int x, int z, int y) {
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

    // todo - rename
    int8_t getColData1(const char *buf, int x, int z) {
      int off = _calcOffset(x,z);
      int8_t v = buf[32768 + 16384 + 16384 + 16384 + off];
      return v;
    }

    // todo - rename
    int32_t getColData2(const char *buf, int x, int z) {
      int off = _calcOffset(x,z) * 4;
      int32_t v;
      memcpy(&v,&buf[32768 + 16384 + 16384 + 16384 + 256 + off],4);
      return v;
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
    
    Status parseDb ( const std::string fname ) {
      // todobig - leveldb read-only? snapshot?
      leveldb::DB* db;
      leveldb::Options options;
      options.compressors[0] = new leveldb::ZlibCompressor();
      options.create_if_missing = false;
      leveldb::Status status = leveldb::DB::Open(options, fname, &db);
      assert(status.ok());

      int histo[256];
      int histoBiome[256];
      uint8_t topBlock[16][16];
      uint8_t topData[16][16];
      uint8_t blockBiome[16][16];
      uint8_t topSkyLight[16][16];
      uint8_t topBlockLight[16][16];
      uint8_t topBlockY[16][16];
      int8_t colData1[16][16];
      int32_t colData2[16][16];

      char tmpstring[256];

      int chunkX=-1, chunkZ=-1, chunkWorldId=-1, chunkType=-1;
      
      int minChunkX[kWorldIdCount], maxChunkX[kWorldIdCount];
      int minChunkZ[kWorldIdCount], maxChunkZ[kWorldIdCount];
      for (int i=0; i < kWorldIdCount; i++) {
	minChunkX[i]=maxChunkX[i]=minChunkZ[i]=maxChunkZ[i]=0;
      }

      leveldb::Iterator* iter = db->NewIterator(leveldb::ReadOptions());
      int recordCt = 0;
      
      // pre-scan keys to get min/max chunks so that we can provide image coordinates for chunks
      fprintf(stderr,"Pre-scan keys to get world boundaries\n");
      for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
	Slice skey = iter->key();
	int key_size = skey.size();
	const char* key = skey.data();

	++recordCt;
	if ( control.shortRunFlag && recordCt > 100 ) {
	  break;
	}
	
	if ( key_size == 9 ) {
	  chunkX = myParseInt32_T(key, 0);
	  chunkZ = myParseInt32_T(key, 4);
	  chunkType = myParseInt8(key, 8);
	  
	  // sanity checks
	  if ( chunkType == 0x30 ) {
	    minChunkX[0] = std::min(minChunkX[0], chunkX);
	    minChunkZ[0] = std::min(minChunkZ[0], chunkZ);
	    maxChunkX[0] = std::max(maxChunkX[0], chunkX);
	    maxChunkZ[0] = std::max(maxChunkZ[0], chunkZ);
	  }
	}
	else if ( key_size == 13 ) {
	  chunkX = myParseInt32_T(key, 0);
	  chunkZ = myParseInt32_T(key, 4);
	  chunkWorldId = myParseInt32_T(key, 8);
	  chunkType = myParseInt8(key, 12);
	  
	  // sanity checks
	  if ( chunkType == 0x30 ) {
	    minChunkX[chunkWorldId] = std::min(minChunkX[chunkWorldId], chunkX);
	    minChunkZ[chunkWorldId] = std::min(minChunkZ[chunkWorldId], chunkZ);
	    maxChunkX[chunkWorldId] = std::max(maxChunkX[chunkWorldId], chunkX);
	    maxChunkZ[chunkWorldId] = std::max(maxChunkZ[chunkWorldId], chunkZ);
	  }
	}
      }

      fprintf(stderr,"Parse all leveldb records\n");
      MyTagList tagList;
      recordCt = 0;
      for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {

	// note: we get the raw buffer early to avoid overhead (maybe?)
	Slice skey = iter->key();
	int key_size = (int)skey.size();
	const char* key = skey.data();

	Slice svalue = iter->value();
	int value_size = svalue.size();
	const char* value = svalue.data();

	++recordCt;
	if ( control.shortRunFlag && recordCt > 100 ) {
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
	  //  0x64 +"Overworld" -- "LimboEntities"? -- overworld only?
	  fprintf(control.fpLog,"Overworld value:\n");
	  parseNbt("Overworld: ", value, value_size, tagList);
	  // todo - parse tagList?
	}
	else if ( strncmp(key,"~local_player",key_size) == 0 ) {
	  // 0x72 +"~local_player" -- player info?? ?? -- nether only?
	  fprintf(control.fpLog,"Local Player value:\n");
	  parseNbt("Local Player: ", value, value_size, tagList);
	  // todo - parse tagList? 
	}
	else if ( strncmp(key,"player_",key_size) == 0 ) {
	  fprintf(control.fpLog,"Remote Player value:\n");
	  parseNbt("Remote Player: ", value, value_size, tagList);
	  // todo - parse tagList?
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
	}
			 
	else if ( key_size == 9 || key_size == 13 ) {

	  // this is probably a record we want to parse

	  std::string worldName;
	  if ( key_size == 9 ) {
	    chunkX = myParseInt32_T(key, 0);
	    chunkZ = myParseInt32_T(key, 4);
	    chunkWorldId = 0; // forced for overworld
	    chunkType = myParseInt8(key, 8);
	    worldName = "overworld";
	  }
	  else if ( key_size == 13 ) {
	    chunkX = myParseInt32_T(key, 0);
	    chunkZ = myParseInt32_T(key, 4);
	    chunkWorldId = myParseInt32_T(key, 8);
	    chunkType = myParseInt8(key, 12);
	    worldName = "nether";

	    // check for new world id's
	    if ( chunkWorldId != 1 ) {
	      fprintf(stderr, "HEY! Found new chunkWorldId=0x%x\n", chunkWorldId);
	    }
	  }
	  
	  histoChunkType[chunkWorldId][chunkType]++;

	  r = worldName + "-chunk: ";
	  sprintf(tmpstring,"%d %d (t=%x)", chunkX, chunkZ, chunkType);
	  r += tmpstring;
	  if ( true ) {
	    // show approximate image coordinates for chunk
	    int chunkOffsetX = -minChunkX[chunkWorldId];
	    int chunkOffsetZ = -maxChunkZ[chunkWorldId];
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
	    memset(blockBiome,0, 16*16*sizeof(uint8_t));
	    memset(topSkyLight,0, 16*16*sizeof(uint8_t));
	    memset(topBlockLight,0, 16*16*sizeof(uint8_t));

	    // iterate over chunk area, get data etc
	    for (int cy=127; cy >= 0; cy--) {
	      for ( int cz=0; cz < 16; cz++ ) {
		for ( int cx=0; cx < 16; cx++) {
		  int blockId = getBlockId(value, cx,cz,cy);
		  histo[blockId]++;
		  if ( topBlock[cz][cx] == 0 ||
		       vectorContains(blockHideList[chunkWorldId], topBlock[cz][cx]) ||
		       vectorContains(blockForceTopList[chunkWorldId], blockId) ) {
		    topBlock[cz][cx] = blockId;
		    topData[cz][cx] = getBlockData(value, cx,cz,cy);
		    topSkyLight[cz][cx] = getBlockSkyLight(value, cx,cz,cy);
		    topBlockLight[cz][cx] = getBlockBlockLight(value, cx,cz,cy);
		    topBlockY[cz][cx] = cy;
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
	      }
	    }

	    // print chunk info
	    fprintf(control.fpLog,"Top Blocks (block-id:block-data:biome-id):\n");
	    for (int cz=0; cz<16; cz++) {
	      for (int cx=0; cx<16; cx++) {
		int biomeId = (int)(colData2[cz][cx] & 0xFF);
		histoBiome[biomeId]++;
		histoGlobalBiome[chunkWorldId][biomeId]++;
		blockBiome[cz][cx] = biomeId;
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
	    fprintf(control.fpLog,"Column Data (dirty?:biome):\n");
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
					     &blockBiome[0][0], (const uint8_t*)value, &topBlockY[0][0]);

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
	      const char* d = value;
	      fprintf(control.fpLog,"%s 0x76 chunk (world format version): v=%d\n", worldName.c_str(), (int)(d[0]));
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

      delete db;

      return Status::OK();
    }

    int generateImages() {

      fprintf(control.fpLog,"Report for Overworld:\n");
      chunkList[kWorldIdOverworld].myreport(kWorldIdOverworld);
      
      fprintf(stderr,"Generate Image for Overworld\n");
      chunkList[kWorldIdOverworld].generateImage(std::string(control.fnOutputBase + ".overworld.png"));

      if ( control.doBiomeImageFlag ) {
	fprintf(stderr,"Generate Biome Image for Overworld\n");
	chunkList[kWorldIdOverworld].generateImage(std::string(control.fnOutputBase + ".overworld.biome.png"), true);
      }

      // reset
      for (int i=0; i < 256; i++) {
	blockInfo[i].colorSetNeedCount = 0;
      }


      fprintf(control.fpLog,"Report for Nether:\n");
      chunkList[kWorldIdNether].myreport(kWorldIdNether);

      fprintf(stderr,"Generate Image for Nether\n");
      chunkList[kWorldIdNether].generateImage(std::string(control.fnOutputBase + ".nether.png"));

      if ( control.doBiomeImageFlag ) {
	fprintf(stderr,"Generate Biome Image for Nether\n");
	chunkList[kWorldIdNether].generateImage(std::string(control.fnOutputBase + ".nether.biome.png"), true);
      }

      // todo - could do a special nether output with all pixels 8x8 (could help w/ portal stuff)
      
      
      if ( control.doMovieFlag ) {
	fprintf(stderr,"Generate movie for Overworld\n");
	chunkList[kWorldIdOverworld].generateImageSlices(std::string(control.fnOutputBase + ".overworld.mp4"),"overworld", false);
      }

      if ( control.doMovieNetherFlag ) {
	fprintf(stderr,"Generate movie for Nether\n");
	chunkList[kWorldIdNether].generateImageSlices(std::string(control.fnOutputBase + ".nether.mp4"),"nether", true);
      }
      
      return 0;
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
	  fprintf(stderr, "Found World Spawn: x=%d y=%d z=%d\n", worldSpawnX, worldSpawnY, worldSpawnZ);
	}

	delete [] buf;
      } else {
	fclose(fp);
      }
      
      return ret;
    }

    int file_exists(const char* fn) {
      FILE *fp;
      fp = fopen(fn,"rb");
      if ( fp ) {
	fclose(fp);
	return 1;
      }
      return 0;
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
	  if ( pass ) {
	    // add to hide list
	    fprintf(stderr,"INFO: Adding 'hide-top' block: worldId=%d blockId=%d (0x%x) (%s)\n", worldId, blockId, blockId, blockInfo[blockId].name.c_str());
	    blockHideList[worldId].push_back(blockId);
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
	  if ( pass ) {
	    // add to hide list
	    fprintf(stderr,"INFO: Adding 'force-top' block: worldId=%d blockId=%d (0x%x) (%s)\n", worldId, blockId, blockId, blockInfo[blockId].name.c_str());
	    blockForceTopList[worldId].push_back(blockId);
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
      std::string fnHome = getenv("HOME");
      fnHome += "/.mcpe_viz/mcpe_viz.cfg";
      if ( doParseConfigFile( fnHome ) == 0 ) {
	return 0;
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


    // xml helper funcs
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

    // xml helper funcs
    bool xmlGetBool(xmlNodePtr cur, const xmlChar* p, bool &valid) {
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
	xmlFree(prop);
      }
      return false;
    }
    
    // xml helper funcs
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
    
    int doParseXML_blocklist(xmlDocPtr doc, xmlNodePtr cur) {
      cur = cur->xmlChildrenNode;
      while (cur != NULL) {
	if ( xmlStrcmp(cur->name, (const xmlChar *)"block") == 0 ) {

	  bool idValid, nameValid, colorValid, lookupColorFlagValid;
	  
	  int id = xmlGetInt(cur, (const xmlChar*)"id", idValid);
	  std::string name = xmlGetString(cur, (const xmlChar*)"name", nameValid);
	  int color = xmlGetInt(cur, (const xmlChar*)"color", colorValid);
	  bool lookupColorFlag = xmlGetBool(cur, (const xmlChar*)"lookupColor", lookupColorFlagValid);

	  // create data
	  if ( idValid && nameValid ) {
	    MyBlock& b = blockInfo[id].setName(name);
	    if ( colorValid ) {
	      b.setColor(color);
	    }
	    if ( lookupColorFlagValid ) {
	      if ( lookupColorFlag ) {
		b.setLookupColor();
	      }
	    }
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

    int doParseXML_standardcolorlist(xmlDocPtr doc, xmlNodePtr cur) {
      cur = cur->xmlChildrenNode;
      while (cur != NULL) {
	if ( xmlStrcmp(cur->name, (const xmlChar *)"standardcolor") == 0 ) {

	  bool idValid, colorValid;
	  
	  int id = xmlGetInt(cur, (const xmlChar*)"id", idValid);
	  int color = xmlGetInt(cur, (const xmlChar*)"color", colorValid);

	  // create data
	  if ( idValid && colorValid ) {
	    colorInfo[id] = htobe32(color);
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

    int doParseXML_itemlist(xmlDocPtr doc, xmlNodePtr cur) {
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

    int doParseXML_entitylist(xmlDocPtr doc, xmlNodePtr cur) {
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

    int doParseXML_biomelist(xmlDocPtr doc, xmlNodePtr cur) {
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
    
    int doParseXML_xml(xmlDocPtr doc, xmlNodePtr cur) {
      cur = cur->xmlChildrenNode;
      while (cur != NULL) {

	// todo - should count warning/errors and return this info
	
	if ( xmlStrcmp(cur->name, (const xmlChar *)"blocklist") == 0 ) {
	  doParseXML_blocklist(doc,cur);
	}
	if ( xmlStrcmp(cur->name, (const xmlChar *)"standardcolorlist") == 0 ) {
	  doParseXML_standardcolorlist(doc,cur);
	}
	if ( xmlStrcmp(cur->name, (const xmlChar *)"itemlist") == 0 ) {
	  doParseXML_itemlist(doc,cur);
	}
	if ( xmlStrcmp(cur->name, (const xmlChar *)"entitylist") == 0 ) {
	  doParseXML_entitylist(doc,cur);
	}
	if ( xmlStrcmp(cur->name, (const xmlChar *)"biomelist") == 0 ) {
	  doParseXML_biomelist(doc,cur);
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
	  ret = doParseXML_xml(doc,cur);
	}
	cur = cur->next;
      }

      // todo - valgrind reports that this does NOT free everyhing it uses
      xmlFreeDoc(doc);
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
      std::string fnHome = getenv("HOME");
      fnHome += "/.mcpe_viz/mcpe_viz.xml";
      ret = doParseXml( fnHome );
      if ( ret >= 0 ) {
	return ret;
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
    
    
  }  // namespace
}  // namespace leveldb


void print_usage(const char* fn) {
  fprintf(stderr,"Usage:\n\n");
  fprintf(stderr,"  %s [required parameters] [options]\n\n",fn);
  fprintf(stderr,"Required Parameters:\n"
	  "  --db dir                 Directory which holds world files (level.dat is in this dir)\n"
	  "  --out fn-part            Filename base for output file(s)\n"
	  "\n"
	  );
  fprintf(stderr,"Options:\n"
	  "  --grid                   Display chunk grid on top of world\n"
	  "  --biome                  Createa a biome map image\n"
	  "\n"
	  "  --movie                  Create movie of layers of overworld\n"
	  "  --movie-nether           Create movie of layers of nether\n"
	  "  --movie-dim x,y,w,h      Integers describing the bounds of the movie (UL X, UL Y, WIDTH, HEIGHT)\n"
	  "\n"
	  "  --xml fn                 XML file containing data definitions\n"
	  "  --log fn                 Send log to a file\n"
	  "\n"
	  // todo - re-enable when we use these:
	  //"  --verbose                verbose output\n"
	  //"  --quiet                  supress normal output, continue to output warning and error messages\n"
	  "  --help                   this info\n"
	  );
}

int parse_args ( int argc, char **argv, Control& control ) {

  static struct option longoptlist[] = {
    {"db", required_argument, NULL, 'D'},
    {"out", required_argument, NULL, 'O'},

    {"xml", required_argument, NULL, 'X'},
    {"log", required_argument, NULL, 'L'},
    
    {"biome", no_argument, NULL, 'B'},
    
    {"movie", no_argument, NULL, 'M'},
    {"movie-nether", no_argument, NULL, 'N'},
    {"movie-dim", required_argument, NULL, '*'},

    {"grid", no_argument, NULL, 'G'},

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
    case 'M':
      control.doMovieFlag = true;
      break;
    case 'N':
      control.doMovieNetherFlag = true;
      break;
    case 'G':
      control.doGridFlag = true;
      break;

    case 'B':
      control.doBiomeImageFlag = true;
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


int main ( int argc, char **argv ) {

  fprintf(stderr,"%s\n",leveldb::version.c_str());
  
  int ret = parse_args(argc, argv, control);
  if (ret != 0) {
    print_usage(basename(argv[0]));
    return ret;
  }

  ret = leveldb::parseXml(argv);
  if ( ret != 0 ) {
    fprintf(stderr,"ERROR: Failed to parse an XML file.  Exiting...\n");
    fprintf(stderr,"** Hint: Make sure that mcpe_viz.xml is in any of: current dir, exec dir, ~/.mcpe_viz/\n");
    return -1;
  }
  
  leveldb::parseConfigFile(argv);
  
  ret = leveldb::parseLevelFile(std::string(control.dirLeveldb + "/level.dat"));
  if ( ret != 0 ) {
    fprintf(stderr,"ERROR: Failed to parse level.dat file.  Exiting...\n");
    return -1;
  }
  
  leveldb::initBlockInfo();
  leveldb::parseDb(std::string(control.dirLeveldb+"/db"));
  leveldb::generateImages();
  
  fprintf(stderr,"Done.\n");

  return 0;
}
