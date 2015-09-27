/*
  MCPE World File Visualizer
  (c) Plethora777, 2015.9.26

  GPL'ed code - see LICENSE

  This requires Mojang's modified LevelDB library! (see README.md for details)

  To build it, use cmake or do something like this:
  
  g++ --std=c++11 -DDLLX= -I. -Ileveldb-mcpe/include -I.. -std=c++0x -fno-builtin-memcmp -pthread -DOS_LINUX -DLEVELDB_PLATFORM_POSIX -DLEVELDB_ATOMIC_PRESENT -O2 -DNDEBUG -c mcpe_viz.cc -o mcpe_viz.o ; g++ -pthread mcpe_viz.o leveldb-mcpe/libleveldb.a -lz -o mcpe_viz 


  todo

  ** use container classes to populate an object with NBT content so that we can parse this info

  ** cmdline options:
  filtering stuff w/ decimal or hex (e.g. torches)
  highlighting per world
  presets for filtering?
  save set of slices
  save a particular slice
  draw text on slice files (e.g. Y)
  biome image - image using only biomeid
  logfiles for overworld + nether + unknown

  ** put block, item, entity etc info in cfg files

  ** add cfg files for filtering and highlighting etc

  ** maps/instructions to get from point A (e.g. spawn) to biome type X (in blocks but also in landmark form: over 2 seas left at the birch forest etc)

  */

#include <stdio.h>
#include <getopt.h>
#include <map>
#include <vector>
#include <png.h>
#include "leveldb/db.h"
#include "leveldb/zlib_compressor.h"


// all user options are stored here
class Control {
public:
  std::string dirLeveldb;
  std::string fnOutputBase;
  bool doMovieFlag;
  bool doMovieNetherFlag;
  bool doGridFlag;
  bool doBiomeImageFlag;
  bool verboseFlag;
  bool quietFlag;
  int movieX, movieY, movieW, movieH;
  
  Control() {
    init();
  }
  void init() {
    dirLeveldb = "";
    fnOutputBase = "";
    doMovieFlag = false;
    doMovieNetherFlag = false;
    doGridFlag = false;
    doBiomeImageFlag = false;
    verboseFlag = false;
    quietFlag = false;
    movieX = movieY = movieW = movieH = 0;
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

    // todobig - magic numbers - get these from .dat file
    int spawnX = 64;
    int spawnZ = 0;

    std::vector<int> blockHighlightList;
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
      //blockHighlightList.push_back(0x34);
      // todo - cmdline option


      // hide certain blocks from topBlock
      // todo - cmdline option

      // topBlock in the nether is nuts.  So that we can see some interesting info, let's hide: bedrock netherrack lava
      blockHideList[kWorldIdNether].push_back(0x07);
      blockHideList[kWorldIdNether].push_back(0x57);
      blockHideList[kWorldIdNether].push_back(0x0a);
      blockHideList[kWorldIdNether].push_back(0x0b);

      // we init lots of info gleaned from the wiki....

      // todo - move all of this to cfg files
      // todo - would be cool to find a definitive list of colors for blocks
      // todo - could rip images from apk and get an avg color?
      // todo - option file for specifying colors?
      // todo - setColor for all blocks
      
      blockInfo[0x00].setName("Air").setColor(0x0);
      blockInfo[0x01].setName("Stone S").setColor(0xa0a0a0);
      blockInfo[0x02].setName("Grass Block").setColor(0x1f953e);
      blockInfo[0x03].setName("Dirt").setColor(0x96721a); // 665428);
      blockInfo[0x04].setName("Cobblestone").setColor(0xc0c0c0);
      blockInfo[0x05].setName("Wooden Planks S").setColor(0x696a2d);
      blockInfo[0x06].setName("Sapling S").setColor(0x8afb97);
      blockInfo[0x07].setName("Bedrock").setColor(0x101010);
      blockInfo[0x08].setName("Water S").setColor(0x616699);
      blockInfo[0x09].setName("Stationary Water S").setColor(0x8b92dd);
      blockInfo[0x0A].setName("Lava S").setColor(0xfc2a2a);
      blockInfo[0x0B].setName("Stationary Lava S").setColor(0xfc2a2a);
      blockInfo[0x0C].setName("Sand").setColor(0xede586);
      blockInfo[0x0D].setName("Gravel").setColor(0x808080);
      blockInfo[0x0E].setName("Gold Ore").setColor(0xfcd0fa);
      blockInfo[0x0F].setName("Iron Ore").setColor(0xfcd0fa);
      blockInfo[0x10].setName("Coal Ore").setColor(0xfcd0fa);
      blockInfo[0x11].setName("Wood S B").setColor(0x916609);
      blockInfo[0x12].setName("Leaves S B").setColor(0x184d1e);
      blockInfo[0x13].setName("Sponge");
      blockInfo[0x14].setName("Glass").setColor(0xe0e0e0);
      blockInfo[0x15].setName("Lapis Lazuli Ore");
      blockInfo[0x16].setName("Lapis Lazuli Block");
      blockInfo[0x18].setName("Sandstone S B").setColor(0xfbf8b4);
      blockInfo[0x1A].setName("Bed S").setColor(0xff0000);
      blockInfo[0x1B].setName("Powered Rail").setColor(0xfe9c02);
      blockInfo[0x1E].setName("Cobweb");
      blockInfo[0x1F].setName("Tall Grass").setColor(0x1f953e);
      blockInfo[0x20].setName("Dead Bush").setColor(0x47461e);
      blockInfo[0x23].setName("Wool S B").setLookupColor();
      blockInfo[0x25].setName("Dandelion").setColor(0xe8e98b);
      blockInfo[0x26].setName("Flower S B").setColor(0xe98b9a);
      blockInfo[0x27].setName("Brown Mushroom").setColor(0xcaa762);
      blockInfo[0x28].setName("Red Mushroom").setColor(0xfe9c9c);
      blockInfo[0x29].setName("Block of Gold");
      blockInfo[0x2A].setName("Block of Iron");
      blockInfo[0x2B].setName("Double Stone Slab S").setColor(0x909090);
      blockInfo[0x2C].setName("Stone Slab S B").setColor(0x909090);
      blockInfo[0x2D].setName("Bricks").setColor(0x9a0b18);
      blockInfo[0x2E].setName("TNT");
      blockInfo[0x2F].setName("Bookshelf");
      blockInfo[0x30].setName("Moss Stone").setColor(0x8a9e90);
      blockInfo[0x31].setName("Obsidian").setColor(0xc2a6c1);
      blockInfo[0x32].setName("Torch").setColor(0xffff00);
      blockInfo[0x33].setName("Fire").setColor(0xff1d32);
      blockInfo[0x34].setName("Monster Spawner E").setColor(0x930a91);
      blockInfo[0x35].setName("Oak Wood Stairs S").setColor(0x696a2d);
      blockInfo[0x36].setName("Chest E");
      blockInfo[0x38].setName("Diamond Ore");
      blockInfo[0x39].setName("Block of Diamond");
      blockInfo[0x3A].setName("Crafting Table").setColor(0x202020);
      blockInfo[0x3B].setName("Seeds D").setColor(0x3ebf59);
      blockInfo[0x3C].setName("Farmland D").setColor(0x646325);
      blockInfo[0x3D].setName("Furnace S E").setColor(0x303030);
      blockInfo[0x3E].setName("Burning Furnace S E");
      blockInfo[0x3F].setName("Sign I S E");
      blockInfo[0x40].setName("Wooden Door I S");
      blockInfo[0x41].setName("Ladder");
      blockInfo[0x42].setName("Rail").setColor(0x444421);
      blockInfo[0x43].setName("Cobblestone Stairs D").setColor(0xd0d0d0);
      blockInfo[0x44].setName("Wall Sign I S E");
      blockInfo[0x47].setName("Iron Door S");
      blockInfo[0x49].setName("Redstone Ore");
      blockInfo[0x4A].setName("Glowing Redstone Ore");
      blockInfo[0x4E].setName("Snow (layer)").setColor(0xc0c0c0);
      blockInfo[0x4F].setName("Ice").setColor(0x0cf5ed);
      blockInfo[0x50].setName("Snow").setColor(0xf0f0f0);
      blockInfo[0x51].setName("Cactus").setColor(0x116713);
      blockInfo[0x52].setName("Clay (block)");
      blockInfo[0x53].setName("Sugar Cane").setColor(0x4ee547);
      blockInfo[0x55].setName("Fence").setColor(0x606060);
      blockInfo[0x56].setName("Pumpkin S").setColor(0xfbad26);
      blockInfo[0x57].setName("Netherrack").setColor(0xc6475f);
      blockInfo[0x58].setName("Soul Sand").setColor(0x9f6969);
      blockInfo[0x59].setName("Glowstone").setColor(0xfbfe03);
      blockInfo[0x5A].setName("Portal").setColor(0xffa0ff);
      blockInfo[0x5B].setName("Jack o'Lantern S").setColor(0xfd9501);
      blockInfo[0x5C].setName("Cake S");
      blockInfo[0x5F].setName("Invisible Bedrock");
      blockInfo[0x60].setName("Trapdoor S");
      blockInfo[0x61].setName("Monster Egg");
      blockInfo[0x62].setName("Stone Brick D B").setColor(0xb6b6b0);
      blockInfo[0x63].setName("Brown Mushroom (block)").setColor(0xcaa762);
      blockInfo[0x64].setName("Red Mushroom (block)").setColor(0xfe9c9c);
      blockInfo[0x65].setName("Iron Bars");
      blockInfo[0x66].setName("Glass Pane").setColor(0xd0d0d0);
      blockInfo[0x67].setName("Melon").setColor(0x73d86c);
      blockInfo[0x68].setName("Pumpkin Stem S").setColor(0xa6a96f);
      blockInfo[0x69].setName("Melon Stem S").setColor(0xa6a96f);
      blockInfo[0x6A].setName("Vines D").setColor(0x265e2c);
      blockInfo[0x6B].setName("Fence Gate S").setColor(0x57553b);
      blockInfo[0x6C].setName("Brick Stairs S");
      blockInfo[0x6D].setName("Stone Brick Stairs S");
      blockInfo[0x6E].setName("Mycelium");
      blockInfo[0x6F].setName("Lily Pad");
      blockInfo[0x70].setName("Nether Brick").setColor(0x712e2e);
      blockInfo[0x71].setName("Nether Brick Fence").setColor(0x8c3939);
      blockInfo[0x72].setName("Nether Brick Stairs S").setColor(0xa74949);
      blockInfo[0x73].setName("Nether Wart").setColor(0xa81031);
      blockInfo[0x74].setName("Enchantment Table E");
      blockInfo[0x75].setName("Brewing Stand I S E");
      blockInfo[0x78].setName("End Portal Frame");
      blockInfo[0x79].setName("End Stone");
      blockInfo[0x7E].setName("Cake (Mystery) S");
      blockInfo[0x7F].setName("Cocoa I S");
      blockInfo[0x80].setName("Sandstone Stairs S").setColor(0xcecb89);
      blockInfo[0x81].setName("Emerald Ore");
      blockInfo[0x85].setName("Block of Emerald");
      blockInfo[0x86].setName("Spruce Wood Stairs S").setColor(0x696a2d);
      blockInfo[0x87].setName("Birch Wood Stairs S").setColor(0x696a2d);
      blockInfo[0x88].setName("Jungle Wood Stairs S").setColor(0x696a2d);
      blockInfo[0x8B].setName("Cobblestone Wall S").setColor(0xb0b0b0);
      blockInfo[0x8C].setName("Flower Pot I S E");
      blockInfo[0x8D].setName("Carrots I S");
      blockInfo[0x8E].setName("Potato I S");
      blockInfo[0x8F].setName("Mob head I S E");
      blockInfo[0x90].setName("Anvil S B");
      blockInfo[0x98].setName("Block of Redstone");
      blockInfo[0x99].setName("Nether Quartz Ore").setColor(0xd6cdcd);
      blockInfo[0x9B].setName("Block of Quartz S B");
      blockInfo[0x9C].setName("Quartz Stairs S");
      blockInfo[0x9D].setName("Wooden Double Slab S").setColor(0x696a2d);
      blockInfo[0x9E].setName("Wooden Slab S B").setColor(0x696a2d);
      blockInfo[0x9F].setName("Stained Clay S B").setLookupColor();
      blockInfo[0xA1].setName("Acacia Leaves S B").setColor(0x88b486);
      blockInfo[0xA2].setName("Acacia Wood S B").setColor(0x696a2d);
      blockInfo[0xA3].setName("Acacia Wood Stairs S").setColor(0x696a2d);
      blockInfo[0xA4].setName("Dark Oak Wood Stairs S").setColor(0x696a2d);
      blockInfo[0xAA].setName("Hay Bale");
      blockInfo[0xAB].setName("Carpet S B").setLookupColor();
      blockInfo[0xAC].setName("Hardened Clay").setColor(0xb75252); // b18245);
      blockInfo[0xAD].setName("Block of Coal");
      blockInfo[0xAE].setName("Packed Ice").setColor(0x02cffd);
      blockInfo[0xAF].setName("Sunflower");
      blockInfo[0xB7].setName("Spruce Fence Gate");
      blockInfo[0xB8].setName("Birch Fence Gate");
      blockInfo[0xB9].setName("Jungle Fence Gate");
      blockInfo[0xBA].setName("Dark Oak Fence Gate");
      blockInfo[0xBB].setName("Acacia Fence Gate");
      blockInfo[0xC6].setName("Grass Path").setColor(0xb86335);
      blockInfo[0xF3].setName("Podzol").setColor(0x45390a);
      blockInfo[0xF4].setName("Beetroot I S");
      blockInfo[0xF5].setName("Stone Cutter");
      blockInfo[0xF6].setName("Glowing Obsidian");
      blockInfo[0xF7].setName("Nether Reactor Core");
      blockInfo[0xF8].setName("Update Game Block (update!) [note 1]");
      blockInfo[0xF9].setName("Update Game Block (ate!upd)");
      blockInfo[0xFF].setName("info_reserved6");

      // from wiki - for wool + clay + carpet + stained glass
      // note: we convert color storage to big endian so that we can memcpy when creating images
      colorInfo[0] = htobe32(0xDDDDDD);
      colorInfo[1] = htobe32(0xDB7D3E);
      colorInfo[2] = htobe32(0xB350BC);
      colorInfo[3] = htobe32(0x6B8AC9);
      colorInfo[4] = htobe32(0xB1A627);
      colorInfo[5] = htobe32(0x41AE38);
      colorInfo[6] = htobe32(0xD08499);
      colorInfo[7] = htobe32(0x404040);
      colorInfo[8] = htobe32(0x9AA1A1);
      colorInfo[9] = htobe32(0x2E6E89);
      colorInfo[10] = htobe32(0x7E3DB5);
      colorInfo[11] = htobe32(0x2E388D);
      colorInfo[12] = htobe32(0x4F321F);
      colorInfo[13] = htobe32(0x35461B);
      colorInfo[14] = htobe32(0x963430);
      colorInfo[15] = htobe32(0x191616);

      itemInfo.insert( std::make_pair(0x100, std::unique_ptr<MyItem>(new MyItem("Iron Shovel D"))) );
      itemInfo.insert( std::make_pair(0x101, std::unique_ptr<MyItem>(new MyItem("Iron Pickaxe D"))) );
      itemInfo.insert( std::make_pair(0x102, std::unique_ptr<MyItem>(new MyItem("Iron Axe D"))) );
      itemInfo.insert( std::make_pair(0x103, std::unique_ptr<MyItem>(new MyItem("Flint and Steel D"))) );
      itemInfo.insert( std::make_pair(0x104, std::unique_ptr<MyItem>(new MyItem("Apple"))) );
      itemInfo.insert( std::make_pair(0x105, std::unique_ptr<MyItem>(new MyItem("Bow D"))) );
      itemInfo.insert( std::make_pair(0x106, std::unique_ptr<MyItem>(new MyItem("Arrow"))) );
      itemInfo.insert( std::make_pair(0x107, std::unique_ptr<MyItem>(new MyItem("Coal B"))) );
      itemInfo.insert( std::make_pair(0x108, std::unique_ptr<MyItem>(new MyItem("Diamond"))) );
      itemInfo.insert( std::make_pair(0x109, std::unique_ptr<MyItem>(new MyItem("Iron Ingot"))) );
      itemInfo.insert( std::make_pair(0x10A, std::unique_ptr<MyItem>(new MyItem("Gold Ingot"))) );
      itemInfo.insert( std::make_pair(0x10B, std::unique_ptr<MyItem>(new MyItem("Iron Sword D"))) );
      itemInfo.insert( std::make_pair(0x10C, std::unique_ptr<MyItem>(new MyItem("Wooden Sword D"))) );
      itemInfo.insert( std::make_pair(0x10D, std::unique_ptr<MyItem>(new MyItem("Wooden Shovel D"))) );
      itemInfo.insert( std::make_pair(0x10E, std::unique_ptr<MyItem>(new MyItem("Wooden Pickaxe D"))) );
      itemInfo.insert( std::make_pair(0x10F, std::unique_ptr<MyItem>(new MyItem("Wooden Axe D"))) );
      itemInfo.insert( std::make_pair(0x110, std::unique_ptr<MyItem>(new MyItem("Stone Sword D"))) );
      itemInfo.insert( std::make_pair(0x111, std::unique_ptr<MyItem>(new MyItem("Stone Shovel D"))) );
      itemInfo.insert( std::make_pair(0x112, std::unique_ptr<MyItem>(new MyItem("Stone Pickaxe D"))) );
      itemInfo.insert( std::make_pair(0x113, std::unique_ptr<MyItem>(new MyItem("Stone Axe D"))) );
      itemInfo.insert( std::make_pair(0x114, std::unique_ptr<MyItem>(new MyItem("Diamond Sword D"))) );
      itemInfo.insert( std::make_pair(0x115, std::unique_ptr<MyItem>(new MyItem("Diamond Shovel D"))) );
      itemInfo.insert( std::make_pair(0x116, std::unique_ptr<MyItem>(new MyItem("Diamond Pickaxe D"))) );
      itemInfo.insert( std::make_pair(0x117, std::unique_ptr<MyItem>(new MyItem("Diamond Axe D"))) );
      itemInfo.insert( std::make_pair(0x118, std::unique_ptr<MyItem>(new MyItem("Stick"))) );
      itemInfo.insert( std::make_pair(0x119, std::unique_ptr<MyItem>(new MyItem("Bowl"))) );
      itemInfo.insert( std::make_pair(0x11A, std::unique_ptr<MyItem>(new MyItem("Mushroom Stew"))) );
      itemInfo.insert( std::make_pair(0x11B, std::unique_ptr<MyItem>(new MyItem("Gold Sword D"))) );
      itemInfo.insert( std::make_pair(0x11C, std::unique_ptr<MyItem>(new MyItem("Gold Shovel D"))) );
      itemInfo.insert( std::make_pair(0x11D, std::unique_ptr<MyItem>(new MyItem("Gold Pickaxe D"))) );
      itemInfo.insert( std::make_pair(0x11E, std::unique_ptr<MyItem>(new MyItem("Gold Axe D"))) );
      itemInfo.insert( std::make_pair(0x11F, std::unique_ptr<MyItem>(new MyItem("String"))) );
      itemInfo.insert( std::make_pair(0x120, std::unique_ptr<MyItem>(new MyItem("Feather"))) );
      itemInfo.insert( std::make_pair(0x121, std::unique_ptr<MyItem>(new MyItem("Gunpowder"))) );
      itemInfo.insert( std::make_pair(0x122, std::unique_ptr<MyItem>(new MyItem("Wooden Hoe D"))) );
      itemInfo.insert( std::make_pair(0x123, std::unique_ptr<MyItem>(new MyItem("Stone Hoe D"))) );
      itemInfo.insert( std::make_pair(0x124, std::unique_ptr<MyItem>(new MyItem("Iron Hoe D"))) );
      itemInfo.insert( std::make_pair(0x125, std::unique_ptr<MyItem>(new MyItem("Diamond Hoe D"))) );
      itemInfo.insert( std::make_pair(0x126, std::unique_ptr<MyItem>(new MyItem("Gold Hoe"))) );
      itemInfo.insert( std::make_pair(0x127, std::unique_ptr<MyItem>(new MyItem("Seeds"))) );
      itemInfo.insert( std::make_pair(0x128, std::unique_ptr<MyItem>(new MyItem("Wheat"))) );
      itemInfo.insert( std::make_pair(0x129, std::unique_ptr<MyItem>(new MyItem("Bread"))) );
      itemInfo.insert( std::make_pair(0x12A, std::unique_ptr<MyItem>(new MyItem("Leather Cap D"))) );
      itemInfo.insert( std::make_pair(0x12B, std::unique_ptr<MyItem>(new MyItem("Leather Tunic D"))) );
      itemInfo.insert( std::make_pair(0x12C, std::unique_ptr<MyItem>(new MyItem("Leather Pants D"))) );
      itemInfo.insert( std::make_pair(0x12D, std::unique_ptr<MyItem>(new MyItem("Leather Boots D"))) );
      itemInfo.insert( std::make_pair(0x12E, std::unique_ptr<MyItem>(new MyItem("Chain Helmet D"))) );
      itemInfo.insert( std::make_pair(0x12F, std::unique_ptr<MyItem>(new MyItem("Chain Chestplate D"))) );
      itemInfo.insert( std::make_pair(0x130, std::unique_ptr<MyItem>(new MyItem("Chain Leggings D"))) );
      itemInfo.insert( std::make_pair(0x131, std::unique_ptr<MyItem>(new MyItem("Chain Boots D"))) );
      itemInfo.insert( std::make_pair(0x132, std::unique_ptr<MyItem>(new MyItem("Iron Helmet D"))) );
      itemInfo.insert( std::make_pair(0x133, std::unique_ptr<MyItem>(new MyItem("Iron Chestplate D"))) );
      itemInfo.insert( std::make_pair(0x134, std::unique_ptr<MyItem>(new MyItem("Iron Leggings D"))) );
      itemInfo.insert( std::make_pair(0x135, std::unique_ptr<MyItem>(new MyItem("Iron Boots D"))) );
      itemInfo.insert( std::make_pair(0x136, std::unique_ptr<MyItem>(new MyItem("Diamond Helmet D"))) );
      itemInfo.insert( std::make_pair(0x137, std::unique_ptr<MyItem>(new MyItem("Diamond Chestplate D"))) );
      itemInfo.insert( std::make_pair(0x138, std::unique_ptr<MyItem>(new MyItem("Diamond Leggings D"))) );
      itemInfo.insert( std::make_pair(0x139, std::unique_ptr<MyItem>(new MyItem("Diamond Boots D"))) );
      itemInfo.insert( std::make_pair(0x13A, std::unique_ptr<MyItem>(new MyItem("Golden Helmet D"))) );
      itemInfo.insert( std::make_pair(0x13B, std::unique_ptr<MyItem>(new MyItem("Golden Chestplate D"))) );
      itemInfo.insert( std::make_pair(0x13C, std::unique_ptr<MyItem>(new MyItem("Golden Leggings D"))) );
      itemInfo.insert( std::make_pair(0x13D, std::unique_ptr<MyItem>(new MyItem("Golden Boots D"))) );
      itemInfo.insert( std::make_pair(0x13E, std::unique_ptr<MyItem>(new MyItem("Flint"))) );
      itemInfo.insert( std::make_pair(0x13F, std::unique_ptr<MyItem>(new MyItem("Raw Porkchop"))) );
      itemInfo.insert( std::make_pair(0x140, std::unique_ptr<MyItem>(new MyItem("Cooked Porkchop"))) );
      itemInfo.insert( std::make_pair(0x141, std::unique_ptr<MyItem>(new MyItem("Painting"))) );
      itemInfo.insert( std::make_pair(0x142, std::unique_ptr<MyItem>(new MyItem("Golden Apple B"))) );
      itemInfo.insert( std::make_pair(0x143, std::unique_ptr<MyItem>(new MyItem("Sign"))) );
      itemInfo.insert( std::make_pair(0x144, std::unique_ptr<MyItem>(new MyItem("Wooden Door"))) );
      itemInfo.insert( std::make_pair(0x145, std::unique_ptr<MyItem>(new MyItem("Bucket"))) );
      itemInfo.insert( std::make_pair(0x148, std::unique_ptr<MyItem>(new MyItem("Minecart"))) );
      itemInfo.insert( std::make_pair(0x149, std::unique_ptr<MyItem>(new MyItem("Saddle"))) );
      itemInfo.insert( std::make_pair(0x14A, std::unique_ptr<MyItem>(new MyItem("Iron Door"))) );
      itemInfo.insert( std::make_pair(0x14B, std::unique_ptr<MyItem>(new MyItem("Redstone"))) );
      itemInfo.insert( std::make_pair(0x14C, std::unique_ptr<MyItem>(new MyItem("Snowball"))) );
      itemInfo.insert( std::make_pair(0x14D, std::unique_ptr<MyItem>(new MyItem("Boat"))) );
      itemInfo.insert( std::make_pair(0x14E, std::unique_ptr<MyItem>(new MyItem("Leather"))) );
      itemInfo.insert( std::make_pair(0x150, std::unique_ptr<MyItem>(new MyItem("Brick"))) );
      itemInfo.insert( std::make_pair(0x151, std::unique_ptr<MyItem>(new MyItem("Clay"))) );
      itemInfo.insert( std::make_pair(0x152, std::unique_ptr<MyItem>(new MyItem("Sugar Cane"))) );
      itemInfo.insert( std::make_pair(0x153, std::unique_ptr<MyItem>(new MyItem("Paper"))) );
      itemInfo.insert( std::make_pair(0x154, std::unique_ptr<MyItem>(new MyItem("Book"))) );
      itemInfo.insert( std::make_pair(0x155, std::unique_ptr<MyItem>(new MyItem("Slimeball"))) );
      itemInfo.insert( std::make_pair(0x158, std::unique_ptr<MyItem>(new MyItem("Egg"))) );
      itemInfo.insert( std::make_pair(0x159, std::unique_ptr<MyItem>(new MyItem("Compass"))) );
      itemInfo.insert( std::make_pair(0x15A, std::unique_ptr<MyItem>(new MyItem("Fishing Rod"))) );
      itemInfo.insert( std::make_pair(0x15B, std::unique_ptr<MyItem>(new MyItem("Clock"))) );
      itemInfo.insert( std::make_pair(0x15C, std::unique_ptr<MyItem>(new MyItem("Glowstone Dust"))) );
      itemInfo.insert( std::make_pair(0x15D, std::unique_ptr<MyItem>(new MyItem("Raw Fish"))) );
      itemInfo.insert( std::make_pair(0x15E, std::unique_ptr<MyItem>(new MyItem("Cooked Fish"))) );
      itemInfo.insert( std::make_pair(0x15F, std::unique_ptr<MyItem>(new MyItem("Dye B"))) );
      itemInfo.insert( std::make_pair(0x160, std::unique_ptr<MyItem>(new MyItem("Bone"))) );
      itemInfo.insert( std::make_pair(0x161, std::unique_ptr<MyItem>(new MyItem("Sugar"))) );
      itemInfo.insert( std::make_pair(0x162, std::unique_ptr<MyItem>(new MyItem("Cake"))) );
      itemInfo.insert( std::make_pair(0x163, std::unique_ptr<MyItem>(new MyItem("Bed"))) );
      itemInfo.insert( std::make_pair(0x165, std::unique_ptr<MyItem>(new MyItem("Cookie"))) );
      itemInfo.insert( std::make_pair(0x167, std::unique_ptr<MyItem>(new MyItem("Shears"))) );
      itemInfo.insert( std::make_pair(0x168, std::unique_ptr<MyItem>(new MyItem("Melon"))) );
      itemInfo.insert( std::make_pair(0x169, std::unique_ptr<MyItem>(new MyItem("Pumpkin Seeds"))) );
      itemInfo.insert( std::make_pair(0x16A, std::unique_ptr<MyItem>(new MyItem("Melon Seeds"))) );
      itemInfo.insert( std::make_pair(0x16B, std::unique_ptr<MyItem>(new MyItem("Raw Beef"))) );
      itemInfo.insert( std::make_pair(0x16C, std::unique_ptr<MyItem>(new MyItem("Steak"))) );
      itemInfo.insert( std::make_pair(0x16D, std::unique_ptr<MyItem>(new MyItem("Raw Chicken"))) );
      itemInfo.insert( std::make_pair(0x16E, std::unique_ptr<MyItem>(new MyItem("Cooked Chicken"))) );
      itemInfo.insert( std::make_pair(0x16F, std::unique_ptr<MyItem>(new MyItem("Rotten Flesh"))) );
      itemInfo.insert( std::make_pair(0x170, std::unique_ptr<MyItem>(new MyItem("Blaze Rod"))) );
      itemInfo.insert( std::make_pair(0x171, std::unique_ptr<MyItem>(new MyItem("Ghast Tear"))) );
      itemInfo.insert( std::make_pair(0x172, std::unique_ptr<MyItem>(new MyItem("Gold Nugget"))) );
      itemInfo.insert( std::make_pair(0x173, std::unique_ptr<MyItem>(new MyItem("Nether Wart"))) );
      itemInfo.insert( std::make_pair(0x174, std::unique_ptr<MyItem>(new MyItem("Potion"))) );
      itemInfo.insert( std::make_pair(0x175, std::unique_ptr<MyItem>(new MyItem("Spider Eye"))) );
      itemInfo.insert( std::make_pair(0x176, std::unique_ptr<MyItem>(new MyItem("Fermented Spider Eye"))) );
      itemInfo.insert( std::make_pair(0x177, std::unique_ptr<MyItem>(new MyItem("Blaze Powder"))) );
      itemInfo.insert( std::make_pair(0x178, std::unique_ptr<MyItem>(new MyItem("Magma Cream"))) );
      itemInfo.insert( std::make_pair(0x179, std::unique_ptr<MyItem>(new MyItem("Brewing Stand"))) );
      itemInfo.insert( std::make_pair(0x17A, std::unique_ptr<MyItem>(new MyItem("Glistering Melon"))) );
      itemInfo.insert( std::make_pair(0x17F, std::unique_ptr<MyItem>(new MyItem("Spawn Egg B"))) );
      itemInfo.insert( std::make_pair(0x180, std::unique_ptr<MyItem>(new MyItem("Bottle o' Enchanting"))) );
      itemInfo.insert( std::make_pair(0x184, std::unique_ptr<MyItem>(new MyItem("Emerald"))) );
      itemInfo.insert( std::make_pair(0x186, std::unique_ptr<MyItem>(new MyItem("Flower Pot"))) );
      itemInfo.insert( std::make_pair(0x187, std::unique_ptr<MyItem>(new MyItem("Carrot"))) );
      itemInfo.insert( std::make_pair(0x188, std::unique_ptr<MyItem>(new MyItem("Potato"))) );
      itemInfo.insert( std::make_pair(0x189, std::unique_ptr<MyItem>(new MyItem("Baked Potato"))) );
      itemInfo.insert( std::make_pair(0x18A, std::unique_ptr<MyItem>(new MyItem("Poisonous Potato"))) );
      itemInfo.insert( std::make_pair(0x18C, std::unique_ptr<MyItem>(new MyItem("Golden Carrot"))) );
      itemInfo.insert( std::make_pair(0x18D, std::unique_ptr<MyItem>(new MyItem("Mob head"))) );
      itemInfo.insert( std::make_pair(0x190, std::unique_ptr<MyItem>(new MyItem("Pumpkin Pie"))) );
      itemInfo.insert( std::make_pair(0x193, std::unique_ptr<MyItem>(new MyItem("Enchanted Book"))) );
      itemInfo.insert( std::make_pair(0x195, std::unique_ptr<MyItem>(new MyItem("Nether Brick"))) );
      itemInfo.insert( std::make_pair(0x196, std::unique_ptr<MyItem>(new MyItem("Nether Quartz"))) );
      itemInfo.insert( std::make_pair(0x19E, std::unique_ptr<MyItem>(new MyItem("Rabbit's Foot"))) );
      itemInfo.insert( std::make_pair(0x1B6, std::unique_ptr<MyItem>(new MyItem("Splash Potion"))) );
      itemInfo.insert( std::make_pair(0x1C9, std::unique_ptr<MyItem>(new MyItem("Beetroot"))) );
      itemInfo.insert( std::make_pair(0x1CA, std::unique_ptr<MyItem>(new MyItem("Beetroot Seeds"))) );
      itemInfo.insert( std::make_pair(0x1CB, std::unique_ptr<MyItem>(new MyItem("Beetroot Soup"))) );

      entityInfo.insert( std::make_pair(0x0A, std::unique_ptr<MyEntity>(new MyEntity("Chicken"))) );
      entityInfo.insert( std::make_pair(0x0B, std::unique_ptr<MyEntity>(new MyEntity("Cow"))) );
      entityInfo.insert( std::make_pair(0x0C, std::unique_ptr<MyEntity>(new MyEntity("Pig"))) );
      entityInfo.insert( std::make_pair(0x0D, std::unique_ptr<MyEntity>(new MyEntity("Sheep"))) );
      entityInfo.insert( std::make_pair(0x0E, std::unique_ptr<MyEntity>(new MyEntity("Wolf"))) );
      entityInfo.insert( std::make_pair(0x0F, std::unique_ptr<MyEntity>(new MyEntity("Villager"))) );
      entityInfo.insert( std::make_pair(0x10, std::unique_ptr<MyEntity>(new MyEntity("Mooshroom"))) );
      entityInfo.insert( std::make_pair(0x11, std::unique_ptr<MyEntity>(new MyEntity("Squid"))) );
      entityInfo.insert( std::make_pair(0x13, std::unique_ptr<MyEntity>(new MyEntity("Bat"))) );
      entityInfo.insert( std::make_pair(0x14, std::unique_ptr<MyEntity>(new MyEntity("Iron Golem"))) );
      entityInfo.insert( std::make_pair(0x15, std::unique_ptr<MyEntity>(new MyEntity("Snow Golem"))) );
      entityInfo.insert( std::make_pair(0x16, std::unique_ptr<MyEntity>(new MyEntity("Ocelot"))) );
      entityInfo.insert( std::make_pair(0x20, std::unique_ptr<MyEntity>(new MyEntity("Zombie"))) );
      entityInfo.insert( std::make_pair(0x21, std::unique_ptr<MyEntity>(new MyEntity("Creeper"))) );
      entityInfo.insert( std::make_pair(0x22, std::unique_ptr<MyEntity>(new MyEntity("Skeleton"))) );
      entityInfo.insert( std::make_pair(0x23, std::unique_ptr<MyEntity>(new MyEntity("Spider"))) );
      entityInfo.insert( std::make_pair(0x24, std::unique_ptr<MyEntity>(new MyEntity("Zombie Pigman"))) );
      entityInfo.insert( std::make_pair(0x25, std::unique_ptr<MyEntity>(new MyEntity("Slime"))) );
      entityInfo.insert( std::make_pair(0x26, std::unique_ptr<MyEntity>(new MyEntity("Enderman"))) );
      entityInfo.insert( std::make_pair(0x27, std::unique_ptr<MyEntity>(new MyEntity("Silverfish"))) );
      entityInfo.insert( std::make_pair(0x28, std::unique_ptr<MyEntity>(new MyEntity("Cave Spider"))) );
      entityInfo.insert( std::make_pair(0x29, std::unique_ptr<MyEntity>(new MyEntity("Ghast"))) );
      entityInfo.insert( std::make_pair(0x2A, std::unique_ptr<MyEntity>(new MyEntity("Magma Cube"))) );
      entityInfo.insert( std::make_pair(0x2B, std::unique_ptr<MyEntity>(new MyEntity("Blaze"))) );
      entityInfo.insert( std::make_pair(0x2C, std::unique_ptr<MyEntity>(new MyEntity("Zombie Villager"))) );
      entityInfo.insert( std::make_pair(0x3F, std::unique_ptr<MyEntity>(new MyEntity("The Player"))) );
      entityInfo.insert( std::make_pair(0x40, std::unique_ptr<MyEntity>(new MyEntity("Dropped item"))) );
      entityInfo.insert( std::make_pair(0x41, std::unique_ptr<MyEntity>(new MyEntity("Primed TNT"))) );
      entityInfo.insert( std::make_pair(0x42, std::unique_ptr<MyEntity>(new MyEntity("Falling block"))) );
      entityInfo.insert( std::make_pair(0x4D, std::unique_ptr<MyEntity>(new MyEntity("Fishing Rod hook"))) );
      entityInfo.insert( std::make_pair(0x50, std::unique_ptr<MyEntity>(new MyEntity("Shot arrow"))) );
      entityInfo.insert( std::make_pair(0x51, std::unique_ptr<MyEntity>(new MyEntity("Thrown snowball"))) );
      entityInfo.insert( std::make_pair(0x52, std::unique_ptr<MyEntity>(new MyEntity("Thrown egg"))) );
      entityInfo.insert( std::make_pair(0x53, std::unique_ptr<MyEntity>(new MyEntity("Painting"))) );
      entityInfo.insert( std::make_pair(0x54, std::unique_ptr<MyEntity>(new MyEntity("Minecart"))) );
      entityInfo.insert( std::make_pair(0x55, std::unique_ptr<MyEntity>(new MyEntity("Ghast fireball"))) );
      entityInfo.insert( std::make_pair(0x5A, std::unique_ptr<MyEntity>(new MyEntity("Boat"))) );

      // todo - add color for all biomes for biome images
      biomeInfo.insert( std::make_pair(0x00, std::unique_ptr<MyBiome>(new MyBiome("Ocean",0x8b92dd))) );
      biomeInfo.insert( std::make_pair(0x01, std::unique_ptr<MyBiome>(new MyBiome("Plains",0x1f953e))) );
      biomeInfo.insert( std::make_pair(0x02, std::unique_ptr<MyBiome>(new MyBiome("Desert",0xede586))) );
      biomeInfo.insert( std::make_pair(0x03, std::unique_ptr<MyBiome>(new MyBiome("Extreme Hills",0xc0c0c0))) );
      biomeInfo.insert( std::make_pair(0x04, std::unique_ptr<MyBiome>(new MyBiome("Forest",0x184d1e))) );
      biomeInfo.insert( std::make_pair(0x05, std::unique_ptr<MyBiome>(new MyBiome("Taiga"))) );
      biomeInfo.insert( std::make_pair(0x06, std::unique_ptr<MyBiome>(new MyBiome("Swampland",0x694268))) );
      biomeInfo.insert( std::make_pair(0x07, std::unique_ptr<MyBiome>(new MyBiome("River",0x616699))) );
      biomeInfo.insert( std::make_pair(0x08, std::unique_ptr<MyBiome>(new MyBiome("Hell"))) );
      biomeInfo.insert( std::make_pair(0x09, std::unique_ptr<MyBiome>(new MyBiome("The End"))) );
      biomeInfo.insert( std::make_pair(0x0a, std::unique_ptr<MyBiome>(new MyBiome("FrozenOcean",0x0cf5ed))) );
      biomeInfo.insert( std::make_pair(0x0b, std::unique_ptr<MyBiome>(new MyBiome("FrozenRiver",0x0cf5ed))) );
      biomeInfo.insert( std::make_pair(0x0c, std::unique_ptr<MyBiome>(new MyBiome("Ice Plains",0xf0f0f0))) );
      biomeInfo.insert( std::make_pair(0x0d, std::unique_ptr<MyBiome>(new MyBiome("Ice Mountains",0xc0c0c0))) );
      biomeInfo.insert( std::make_pair(0x0e, std::unique_ptr<MyBiome>(new MyBiome("MushroomIsland"))) );
      biomeInfo.insert( std::make_pair(0x0f, std::unique_ptr<MyBiome>(new MyBiome("MushroomIslandShore"))) );
      biomeInfo.insert( std::make_pair(0x10, std::unique_ptr<MyBiome>(new MyBiome("Beach",0xffd37e))) );
      biomeInfo.insert( std::make_pair(0x11, std::unique_ptr<MyBiome>(new MyBiome("DesertHills"))) );
      biomeInfo.insert( std::make_pair(0x12, std::unique_ptr<MyBiome>(new MyBiome("ForestHills"))) );
      biomeInfo.insert( std::make_pair(0x13, std::unique_ptr<MyBiome>(new MyBiome("TaigaHills"))) );
      biomeInfo.insert( std::make_pair(0x14, std::unique_ptr<MyBiome>(new MyBiome("Extreme Hills Edge"))) );
      biomeInfo.insert( std::make_pair(0x15, std::unique_ptr<MyBiome>(new MyBiome("Jungle",0x04a51e))) );
      biomeInfo.insert( std::make_pair(0x16, std::unique_ptr<MyBiome>(new MyBiome("JungleHills",0x036c14))) );
      biomeInfo.insert( std::make_pair(0x17, std::unique_ptr<MyBiome>(new MyBiome("JungleEdge",0x21ab38))) );
      biomeInfo.insert( std::make_pair(0x18, std::unique_ptr<MyBiome>(new MyBiome("Deep Ocean",0x3644d8))) );
      biomeInfo.insert( std::make_pair(0x19, std::unique_ptr<MyBiome>(new MyBiome("Stone Beach"))) );
      biomeInfo.insert( std::make_pair(0x1a, std::unique_ptr<MyBiome>(new MyBiome("Cold Beach"))) );
      biomeInfo.insert( std::make_pair(0x1b, std::unique_ptr<MyBiome>(new MyBiome("Birch Forest"))) );
      biomeInfo.insert( std::make_pair(0x1c, std::unique_ptr<MyBiome>(new MyBiome("Birch Forest Hills"))) );
      biomeInfo.insert( std::make_pair(0x1d, std::unique_ptr<MyBiome>(new MyBiome("Roofed Forest"))) );
      biomeInfo.insert( std::make_pair(0x1e, std::unique_ptr<MyBiome>(new MyBiome("Cold Taiga"))) );
      biomeInfo.insert( std::make_pair(0x1f, std::unique_ptr<MyBiome>(new MyBiome("Cold Taiga Hills"))) );
      biomeInfo.insert( std::make_pair(0x20, std::unique_ptr<MyBiome>(new MyBiome("Mega Taiga"))) );
      biomeInfo.insert( std::make_pair(0x21, std::unique_ptr<MyBiome>(new MyBiome("Mega Taiga Hills"))) );
      biomeInfo.insert( std::make_pair(0x22, std::unique_ptr<MyBiome>(new MyBiome("Extreme Hills+"))) );
      biomeInfo.insert( std::make_pair(0x23, std::unique_ptr<MyBiome>(new MyBiome("Savanna"))) );
      biomeInfo.insert( std::make_pair(0x24, std::unique_ptr<MyBiome>(new MyBiome("Savanna Plateau"))) );
      biomeInfo.insert( std::make_pair(0x25, std::unique_ptr<MyBiome>(new MyBiome("Mesa",0xb75252))) );
      biomeInfo.insert( std::make_pair(0x26, std::unique_ptr<MyBiome>(new MyBiome("Mesa Plateau F", 0xb76666))) );
      biomeInfo.insert( std::make_pair(0x27, std::unique_ptr<MyBiome>(new MyBiome("Mesa Plateau", 0xb87474))) );
      biomeInfo.insert( std::make_pair(0x7f, std::unique_ptr<MyBiome>(new MyBiome("The Void[upcoming]"))) );
      biomeInfo.insert( std::make_pair(0x80, std::unique_ptr<MyBiome>(new MyBiome("Plains M"))) );
      biomeInfo.insert( std::make_pair(0x81, std::unique_ptr<MyBiome>(new MyBiome("Sunflower Plains"))) );
      biomeInfo.insert( std::make_pair(0x82, std::unique_ptr<MyBiome>(new MyBiome("Desert M"))) );
      biomeInfo.insert( std::make_pair(0x83, std::unique_ptr<MyBiome>(new MyBiome("Extreme Hills M"))) );
      biomeInfo.insert( std::make_pair(0x84, std::unique_ptr<MyBiome>(new MyBiome("Flower Forest"))) );
      biomeInfo.insert( std::make_pair(0x85, std::unique_ptr<MyBiome>(new MyBiome("Taiga M"))) );
      biomeInfo.insert( std::make_pair(0x86, std::unique_ptr<MyBiome>(new MyBiome("Swampland M"))) );
      biomeInfo.insert( std::make_pair(0x8c, std::unique_ptr<MyBiome>(new MyBiome("Ice Plains Spikes",0x02cffd))) );
      biomeInfo.insert( std::make_pair(0x95, std::unique_ptr<MyBiome>(new MyBiome("Jungle M"))) );
      biomeInfo.insert( std::make_pair(0x97, std::unique_ptr<MyBiome>(new MyBiome("JungleEdge M"))) );
      biomeInfo.insert( std::make_pair(0x9b, std::unique_ptr<MyBiome>(new MyBiome("Birch Forest M"))) );
      biomeInfo.insert( std::make_pair(0x9c, std::unique_ptr<MyBiome>(new MyBiome("Birch Forest Hills M"))) );
      biomeInfo.insert( std::make_pair(0x9d, std::unique_ptr<MyBiome>(new MyBiome("Roofed Forest M"))) );
      biomeInfo.insert( std::make_pair(0x9e, std::unique_ptr<MyBiome>(new MyBiome("Cold Taiga M"))) );
      biomeInfo.insert( std::make_pair(0xa0, std::unique_ptr<MyBiome>(new MyBiome("Mega Spruce Taiga"))) );
      biomeInfo.insert( std::make_pair(0xa1, std::unique_ptr<MyBiome>(new MyBiome("Redwood Taiga Hills M"))) );
      biomeInfo.insert( std::make_pair(0xa2, std::unique_ptr<MyBiome>(new MyBiome("Extreme Hills+ M"))) );
      biomeInfo.insert( std::make_pair(0xa3, std::unique_ptr<MyBiome>(new MyBiome("Savanna M"))) );
      biomeInfo.insert( std::make_pair(0xa4, std::unique_ptr<MyBiome>(new MyBiome("Savanna Plateau M"))) );
      biomeInfo.insert( std::make_pair(0xa5, std::unique_ptr<MyBiome>(new MyBiome("Mesa (Bryce)",0xd18383))) );
      biomeInfo.insert( std::make_pair(0xa6, std::unique_ptr<MyBiome>(new MyBiome("Mesa Plateau F M",0xd19292))) );
      biomeInfo.insert( std::make_pair(0xa7, std::unique_ptr<MyBiome>(new MyBiome("Mesa Plateau M",0xb87e7e))) );
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
	printf("\nStatistics:\n");
	printf("chunk-count: %d\n", chunkCount);
	printf("Min-dim:  %d %d\n", minChunkX, minChunkZ);
	printf("Max-dim:  %d %d\n", maxChunkX, maxChunkZ);
	int dx = (maxChunkX-minChunkX+1);
	int dz = (maxChunkZ-minChunkZ+1);
	printf("diff-dim: %d %d\n", dx, dz);
	printf("pixels:   %d %d\n", dx*16, dz*16);

	printf("\nGlobal Chunk Type Histogram:\n");
	for (int i=0; i < 256; i++) {
	  if ( histoChunkType[worldId][i] > 0 ) {
	    printf("hg-chunktype: %02x %6d\n", i, histoChunkType[worldId][i]);
	  }
	}

	printf("\nGLobal Biome Histogram:\n");
	for (int i=0; i < 256; i++) {
	  if ( histoGlobalBiome[worldId][i] > 0 ) {
	    printf("hg-globalbiome: %02x %6d\n", i, histoGlobalBiome[worldId][i]);
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
	  //printf("outchunk: %6d %6d\n", (*it)->chunkX, (*it)->chunkZ);
	  int imageX = ((*it)->chunkX + chunkOffsetX) * 16;
	  int imageZ = ((*it)->chunkZ + chunkOffsetZ) * 16;

	  int worldX = (*it)->chunkX * 16;
	  int worldZ = (*it)->chunkZ * 16;

	  
	  for (int cz=0; cz < 16; cz++) {
	    for (int cx=0; cx < 16; cx++) {

	      if ( biomeImageFlag ) {

		// get biome color
		int8_t biomeId = (*it)->biome[cx][cz];
		try { 
		  if ( biomeInfo.at(biomeId) ) {
		    color = biomeInfo[biomeId]->color;
		    // todo - track "need color" flag?
		  }
		} catch (std::exception& e) {
		  // set an error color
		  // todo - this is getting hit and it should not - check it out
		  color = htobe32(0xff0000);
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
	      if ( (wx == spawnX) && (wz == spawnZ) ) {
		fprintf(stderr,"Info: World Spawn (%d, %d) is at image (%d, %d)\n", spawnX, spawnZ, ix, iz);
	      }
	    }
	  }
	}
	
	// output the image
	outputPNG(fname, buf, imageW, imageH);

	delete [] buf;

	if ( ! biomeImageFlag ) {
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
	    //printf("outchunk: %6d %6d\n", (*it)->chunkX, (*it)->chunkZ);
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

    int parseNbtPart(FILE *fout, const char* hdr, const int nbtMode, const char* buf, int buflen, int &i,
		     uint8_t tagId, int &indent, bool readHeaderFlag) {
      int ret = 0;
      bool showTypeFlag = false;
      bool idTagFlag = false;
      
      if ( readHeaderFlag ) {

	// read tag id
	int16_t nameLen;
	if ( i >= buflen ) {
	  fprintf(fout,"ERROR: buffer exceeded i=%d buflen=%d (tagid)\n",i,buflen);
	  return -1;
	}
	tagId = buf[i++];
	if ( tagId == 0 ) {
	  //if ( --indent < 0 ) { indent=0; }
	  //fprintf(fout,"%sTAG_End\n", makeIndent(indent,hdr).c_str());
	  return 1;
	}

	// sanity check
	if ( (tagId >= 0) && (tagId <= 11) ) {
	} else {
	  fprintf(fout,"ERROR: Invalid NBT tag id (%d)\n",(int)tagId);
	  return -1;
	}

	// read name length
	if ( (i+(int)sizeof(nameLen)) > buflen ) {
	  fprintf(fout,"ERROR: buffer exceeded i=%d buflen=%d (namelen)\n",i,buflen);
	  return -1;
	}
	memcpy(&nameLen, &buf[i], sizeof(nameLen));
	i+=sizeof(nameLen);

	// read name
	std::string name;
	if ( (i+nameLen) <= buflen ) {
	  name.append(&buf[i],nameLen);
	  i+=nameLen;
	} else {
	  fprintf(fout,"ERROR: buffer exceeded i=%d buflen=%d readlen=%d (name)\n",i,buflen,nameLen);
	  return -1;
	}

	if ( name.compare("id") == 0 ) {
	  idTagFlag = true;
	}
	
	fprintf(fout,"%s[%s] ",makeIndent(indent,hdr).c_str(),name.c_str());
      } else {
	fprintf(fout,"%s",makeIndent(indent,hdr).c_str());
      }
      
      // spec from: http://minecraft.gamepedia.com/NBT_format
      switch ( tagId ) {
      case 0:
	// END
	ret = 1;
	break;
      case 1:
	// BYTE
	{
	  int8_t v;
	  if ( (i+(int)sizeof(v)) > buflen ) {
	    fprintf(fout,"ERROR: buffer exceeded i=%d buflen=%d (byte)\n",i,buflen);
	    return -1;
	  }
	  v = buf[i++];
	  fprintf(fout,"[%d]%s", v, (showTypeFlag ? " BYTE" : ""));
	  printIdTag(fout, nbtMode, idTagFlag, (int)v);
	  fprintf(fout,"\n");
	}
	break;
      case 2:
	// SHORT
	{
	  int16_t v;
	  if ( (i+(int)sizeof(v)) > buflen ) {
	    fprintf(fout,"ERROR: buffer exceeded i=%d buflen=%d (short)\n",i,buflen);
	    return -1;
	  }
	  memcpy(&v,&buf[i],sizeof(v));
	  i+=sizeof(v);
	  fprintf(fout,"[%d]%s", v, (showTypeFlag ? " SHORT" : ""));
	  printIdTag(fout, nbtMode, idTagFlag, (int)v);
	  fprintf(fout,"\n");
	}
	break;
      case 3:
	// INT
	{
	  int32_t v;
	  if ( (i+(int)sizeof(v)) > buflen ) {
	    fprintf(fout,"ERROR: buffer exceeded i=%d buflen=%d (int)\n",i,buflen);
	    return -1;
	  }
	  memcpy(&v,&buf[i],sizeof(v));
	  i+=sizeof(v);
	  fprintf(fout,"[%d]%s", v, (showTypeFlag ? " INT" : ""));
	  printIdTag(fout, nbtMode, idTagFlag, (int)v);
	  fprintf(fout,"\n");
	}
	break;
      case 4:
	// LONG
	{
	  int64_t v;
	  if ( (i+(int)sizeof(v)) > buflen ) {
	    fprintf(fout,"ERROR: buffer exceeded i=%d buflen=%d (long)\n",i,buflen);
	    return -1;
	  }
	  memcpy(&v,&buf[i],sizeof(v));
	  i+=sizeof(v);
	  fprintf(fout,"[%ld]%s\n", v, (showTypeFlag ? " LONG" : ""));
	}
	break;
      case 5:
	// FLOAT
	{
	  float v;
	  if ( (i+(int)sizeof(v)) > buflen ) {
	    fprintf(fout,"ERROR: buffer exceeded i=%d buflen=%d (float)\n",i,buflen);
	    return -1;
	  }
	  memcpy(&v,&buf[i],sizeof(v));
	  i+=sizeof(v);
	  fprintf(fout,"[%f]%s\n", v, (showTypeFlag ? " FLOAT" : ""));
	}
	break;
      case 6:
	// DOUBLE
	{
	  double v;
	  if ( (i+(int)sizeof(v)) > buflen ) {
	    fprintf(fout,"ERROR: buffer exceeded i=%d buflen=%d (double)\n",i,buflen);
	    return -1;
	  }
	  memcpy(&v,&buf[i],sizeof(v));
	  i+=sizeof(v);
	  fprintf(fout,"[%lf]%s\n", v, (showTypeFlag ? " DOUBLE" : ""));
	}
	break;
      case 7:
	// BYTE ARRAY
	{
	  int32_t len;
	  if ( (i+(int)sizeof(len)) > buflen ) {
	    fprintf(fout,"ERROR: buffer exceeded i=%d buflen=%d (byte array len)\n",i,buflen);
	    return -1;
	  }
	  memcpy(&len,&buf[i],sizeof(len));
	  i+=sizeof(len);

	  if ( ((i+len) > buflen) || (len < 0) ) {
	    fprintf(fout,"ERROR: buffer exceeded i=%d buflen=%d (byte array)\n",i,buflen);
	    return -1;
	  }
	  char* vbuf = new char[len+1];
	  memset(vbuf,0,len+1);
	  memcpy(vbuf,&buf[i],len);
	  i+=len;
	  fprintf(fout,"[");
	  for (int n=0; n < len; n++) {
	    if ( n > 0 ) { fprintf(fout," "); }
	    fprintf(fout,"%02x",(int)vbuf[n]);
	  }
	  fprintf(fout,"] BYTE ARRAY (len=%d)\n", len);
	  delete [] vbuf;
	}
	break;
      case 8:
	// STRING
	{
	  int16_t len;
	  if ( (i+(int)sizeof(len)) > buflen ) {
	    fprintf(fout,"ERROR: buffer exceeded i=%d buflen=%d (string len)\n",i,buflen);
	    return -1;
	  }
	  memcpy(&len,&buf[i],sizeof(len));
	  i+=sizeof(len);

	  if ( ((i+len) > buflen) || (len < 0) ) {
	    fprintf(fout,"ERROR: buffer exceeded i=%d buflen=%d (string) (len=%d)\n",i,buflen,len);
	    return -1;
	  }
	  char* vbuf = new char[len+1];
	  memcpy(vbuf,&buf[i],len);
	  i+=len;
	  vbuf[len]=0;
	  fprintf(fout,"[%s]%s\n", vbuf, (showTypeFlag ? " STRING" : ""));
	  delete [] vbuf;
	}
	break;

      case 9:
	// LIST
	{
	  if ( (i) >= buflen ) {
	    fprintf(fout,"ERROR: buffer exceeded i=%d buflen=%d (list tagid)\n",i,buflen);
	    return -1;
	  }
	  uint8_t listTagId = buf[i++];

	  int32_t listSize;
	  if ( (i+(int)sizeof(listSize)) > buflen ) {
	    fprintf(fout,"ERROR: buffer exceeded i=%d buflen=%d (list size)\n",i,buflen);
	    return -1;
	  }
	  memcpy(&listSize,&buf[i],sizeof(listSize));
	  i+=sizeof(listSize);
	  fprintf(fout,"LIST (t=%d n=%d) {\n", listTagId,listSize);
	  indent++;
	  for (int lct=0; lct < listSize; lct++) {
	    if ( parseNbtPart(fout,hdr,nbtMode,buf,buflen,i,listTagId,indent, false) < 0 ) {
	      ret = -1;
	      break;
	    }
	  }
	  if ( --indent < 0 ) { indent=0; }
	  fprintf(fout,"%s} LIST\n",makeIndent(indent,hdr).c_str());
	}
	break;
	
      case 10:
	// COMPOUND
	{
	  fprintf(fout,"COMPOUND {\n");
	  indent++;
	  bool done = false;
	  while ( ! done ) {
	    int xret = parseNbtPart(fout,hdr,nbtMode,buf,buflen,i,-1,indent, true);
	    if ( xret < 0 ) {
	      ret = -1;
	      done = true;
	    }
	    else if ( xret > 0 ) {
	      done = true;
	    }
	  }
	  if ( ret < 0 ) {
	    fprintf(fout,"ERROR: Processing compound and we got ret=%d\n",ret);
	  }
	  if ( --indent < 0 ) { indent=0; }
	  fprintf(fout,"%s} COMPOUND\n",makeIndent(indent,hdr).c_str());
	}
	break;
	
      default:
	fprintf(fout,"ERROR: UNKNOWN TAG ID (%d) BAILING OUT\n",tagId);
	ret = -1;
	break;
      }

      return ret;
    }
    
    int parseNbt( FILE *fout, const char* hdr, const int nbtMode, const char* buf, int buflen ) {
      int i=0;
      int indent = 0;
      bool stopProcessingFlag = false;
      while ( (i < buflen) && !stopProcessingFlag ) {
	if ( parseNbtPart(fout, hdr, nbtMode, buf, buflen, i, -1, indent, true) < 0 ) {
	  stopProcessingFlag = true;
	}
      }

      return 0;
    }
    
    
    int myParseInt32_T(const Slice& slice, int startByte) {
      int ret;
      const char* p = slice.data();
      memcpy(&ret, &p[startByte], 4);
      return ret;
    }

    int myParseInt8(const Slice& slice, int startByte) {
      return (slice[startByte] & 0xff);
    }

    inline int _calcOffset(int x, int z, int y) {
      // todo - correct calc here? shouldn't it be z*16?!
      return (((x*16) + z)*128) + y;
    }
    
    inline int _calcOffset(int x, int z) {
      // todo - correct calc here? shouldn't it be z*16?!
      return (x*16) + z;
    }

    int getBlockId(const Slice& slice, int x, int z, int y) {
      return (slice[_calcOffset(x,z,y)] & 0xff);
    }

    int getBlockData(const Slice& slice, int x, int z, int y) {
      int off =  _calcOffset(x,z,y);
      int off2 = off / 2;
      int mod2 = off % 2;
      int v = slice[32768 + off2];
      if ( mod2 == 0 ) {
	return v & 0x0f;
      } else {
	return (v & 0xf0) >> 4;
      }
    }

    int getBlockSkyLight(const Slice& slice, int x, int z, int y) {
      int off =  _calcOffset(x,z,y);
      int off2 = off / 2;
      int mod2 = off % 2;
      int v = slice[32768 + 16384 + off2];
      if ( mod2 == 0 ) {
	return v & 0x0f;
      } else {
	return (v & 0xf0) >> 4;
      }
    }

    int getBlockBlockLight(const Slice& slice, int x, int z, int y) {
      int off =  _calcOffset(x,z,y);
      int off2 = off / 2;
      int mod2 = off % 2;
      int v = slice[32768 + 16384 + 16384 + off2];
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


    int printKeyValue(const Slice &key, const Slice &xvalue, bool printKeyAsStringFlag) {
      printf("WARNING: Unknown key size (%d) k=[%s][", (int)key.size(), 
	     (printKeyAsStringFlag ? key.ToString().c_str() : "(SKIPPED)"));
      for (size_t i=0; i < key.size(); i++) {
	if ( i > 0 ) { printf(" "); }
	printf("%02x",((int)key[i] & 0xff));
      }
      printf("] v=[");
      for (size_t i=0; i < xvalue.size(); i++) {
	if ( i > 0 ) { printf(" "); }
	printf("%02x",((int)xvalue[i] & 0xff));
      }
      printf("]\n");
      return 0;
    }
    
    Status parseDb ( const std::string fname ) {
      // todobig - leveldb read-only? snapshot? copy to temp location before opening (for safety)?
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
      
      // pre-scan keys to get min/max chunks so that we can provide image coordinates for chunks
      fprintf(stderr,"Pre-scan keys to get world boundaries\n");
      for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
	Slice key = iter->key();

	if ( key.size() == 9 ) {
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
	else if ( key.size() == 13 ) {
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
      int itemCt = 0;
      for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {

	// todo - cheaper to somehow get a pointer? does this copy the values?
	Slice key = iter->key();
	Slice xvalue = iter->value();

	if ( (++itemCt % 10000) == 0 ) {
	  fprintf(stderr, "  Reading items: %d\n", itemCt);
	}

	std::string r;
	printf("\n");

	if ( key.compare("BiomeData") == 0 ) {
	  // 0x61 +"BiomeData" -- snow accum? -- overworld only?
	  printf("BiomeData value:\n");
	  parseNbt(stdout, "BiomeData: ", kNbtModePlain, xvalue.data(), xvalue.size() );
	}
	else if ( key.compare("Overworld") == 0 ) {
	  //  0x64 +"Overworld" -- "LimboEntities"? -- overworld only?
	  printf("Overworld value:\n");
	  parseNbt(stdout, "Overworld: ", kNbtModePlain, xvalue.data(), xvalue.size() );
	}
	else if ( key.compare("~local_player") == 0 ) {
	  // 0x72 +"~local_player" -- player info?? ?? -- nether only?
	  printf("Local Player value:\n");
	  parseNbt(stdout, "Local Player: ", kNbtModeItem, xvalue.data(), xvalue.size() );
	}
	else if ( key.compare("player_") == 0 ) {
	  printf("Remote Player value:\n");
	  parseNbt(stdout, "Remote Player: ", kNbtModeItem, xvalue.data(), xvalue.size() );
	}
	else if ( key.compare("villages") == 0 ) {
	  printf("Villages value:\n");
	  parseNbt(stdout, "villages: ", kNbtModePlain, xvalue.data(), xvalue.size() );
	}
	else if ( key.compare("Nether") == 0 ) {
	  printf("Nether value:\n");
	  parseNbt(stdout, "Nether: ", kNbtModePlain, xvalue.data(), xvalue.size() );
	}
	else if ( key.compare("portals") == 0 ) {
	  printf("portals value:\n");
	  parseNbt(stdout, "portals: ", kNbtModePlain, xvalue.data(), xvalue.size() );
	}
			 
	else if ( key.size() == 9 || key.size() == 13 ) {

	  // this is probably a record we want to parse

	  std::string worldName;
	  if ( key.size() == 9 ) {
	    chunkX = myParseInt32_T(key, 0);
	    chunkZ = myParseInt32_T(key, 4);
	    chunkWorldId = 0; // forced for overworld
	    chunkType = myParseInt8(key, 8);
	    worldName = "overworld";
	  }
	  else if ( key.size() == 13 ) {
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
	  fprintf(stdout,r.c_str());

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
		  int blockId = getBlockId(xvalue, cx,cz,cy);
		  histo[blockId]++;
		  if ( topBlock[cz][cx] == 0 ||
		       vectorContains(blockHideList[chunkWorldId], topBlock[cz][cx]) ||
		       vectorContains(blockHighlightList, blockId) ) {
		    topBlock[cz][cx] = blockId;
		    topData[cz][cx] = getBlockData(xvalue, cx,cz,cy);
		    topSkyLight[cz][cx] = getBlockSkyLight(xvalue, cx,cz,cy);
		    topBlockLight[cz][cx] = getBlockBlockLight(xvalue, cx,cz,cy);
		    topBlockY[cz][cx] = cy;
		  }
		}
	      }
	    }

	    memset(colData1, 0, 16*16*sizeof(uint8_t));
	    memset(colData2, 0, 16*16*sizeof(int32_t));
	    for (int cz=0; cz < 16; cz++) {
	      for (int cx=0; cx < 16; cx++) {
		colData1[cz][cx] = getColData1(xvalue.data(), cx,cz);
		colData2[cz][cx] = getColData2(xvalue.data(), cx,cz);
	      }
	    }

	    // print chunk info
	    printf("Top Blocks (block-id:block-data:biome-id):\n");
	    for (int cz=0; cz<16; cz++) {
	      for (int cx=0; cx<16; cx++) {
		int biomeId = (int)(colData2[cz][cx] & 0xFF);
		histoBiome[biomeId]++;
		histoGlobalBiome[chunkWorldId][biomeId]++;
		blockBiome[cz][cx] = biomeId;
		printf("%02x:%x:%02x ", (int)topBlock[cz][cx], (int)topData[cz][cx], (int)biomeId);
	      }
	      printf("\n");
	    }
	    printf("Block Histogram:\n");
	    for (int i=0; i < 256; i++) {
	      if ( histo[i] > 0 ) {
		printf("%s-hg: %02x: %6d (%s)\n", worldName.c_str(), i, histo[i], blockInfo[i].name.c_str());
	      }
	    }
	    printf("Biome Histogram:\n");
	    for (int i=0; i < 256; i++) {
	      if ( histoBiome[i] > 0 ) {
		std::string biomeName( getBiomeName(i) );
		printf("%s-hg-biome: %02x: %6d (%s)\n", worldName.c_str(), i, histoBiome[i], biomeName.c_str());
	      }
	    }
	    printf("Block Light (skylight:blocklight):\n");
	    for (int cz=0; cz<16; cz++) {
	      for (int cx=0; cx<16; cx++) {
		printf("%x:%x ", (int)topSkyLight[cz][cx], (int)topBlockLight[cz][cx]);
	      }
	      printf("\n");
	    }
	    // todo - grass-color is in high 3 bytes of coldata2
	    // todo - need to show this?
	    printf("Column Data (dirty?:biome):\n");
	    for (int cz=0; cz<16; cz++) {
	      for (int cx=0; cx<16; cx++) {
		int biomeId = (int)(colData2[cz][cx] & 0xFF);
		printf("%x:%02x ", (int)colData1[cz][cx], biomeId);
	      }
	      printf("\n");
	    }

	    // store chunk
	    chunkList[chunkWorldId].putChunk(chunkX, chunkZ,
					     &topBlock[0][0], &topData[0][0],
					     &blockBiome[0][0], (const uint8_t*)xvalue.data(), &topBlockY[0][0]);

	    break;

	  case 0x31:
	    printf("%s 0x31 chunk (tile entity data):\n", worldName.c_str());
	    parseNbt(stdout, "0x31-te: ", kNbtModeItem, xvalue.data(), xvalue.size() );
	    break;

	  case 0x32:
	    printf("%s 0x32 chunk (entity data):\n", worldName.c_str());
	    parseNbt(stdout, "0x32-e: ", kNbtModeEntity, xvalue.data(), xvalue.size() );
	    break;

	  case 0x33:
	    // todo - this appears to be info on blocks that can move: water + lava + fire + sand + gravel
	    printf("%s 0x33 chunk (tick-list):\n", worldName.c_str());
	    parseNbt(stdout, "0x33-tick: ", kNbtModePlain, xvalue.data(), xvalue.size() );
	    break;

	  case 0x34:
	    printf("%s 0x34 chunk (TODO - UNKNOWN RECORD)\n", worldName.c_str());
	    printKeyValue(key,xvalue,false);
	    /* 
	       0x34 ?? does not appear to be NBT data -- overworld only? -- perhaps: b0..3 (count); for each: (int32_t) (int16_t) 
	       -- there are 206 of these in "another1" world
	       -- something to do with snow?
	       -- to examine data:
	       cat xee | grep "WARNING: Unknown key size" | grep " 34\]" | cut -b75- | sort | nl
	    */
	    break;

	  case 0x35:
	    printf("%s 0x35 chunk (TODO - UNKNOWN RECORD)\n", worldName.c_str());
	    printKeyValue(key,xvalue,false);
	    /*
	      0x35 ?? -- both worlds -- length 3,5,7,9,11 -- appears to be: b0 (count of items) b1..bn (2-byte ints) 
	      -- there are 2907 in "another1"
	      -- to examine data:
	      cat xee | grep "WARNING: Unknown key size" | grep " 35\]" | cut -b75- | sort | nl
	    */
	    break;

	  case 0x76:
	    {
	      const char* d = xvalue.data();
	      printf("%s 0x76 chunk (world format version): v=%d\n", worldName.c_str(), (int)(d[0]));
	    }
	    break;

	  default:
	    printf("WARNING: %s unknown chunk - size=%d type=0x%x length=%d\n", worldName.c_str(),
		   (int)key.size(), chunkType, (int)xvalue.size());
	    printKeyValue(key,xvalue, true);
	    if ( false ) {
	      if ( xvalue.size() > 10 ) {
		parseNbt(stdout, "UNK: ", kNbtModePlain, xvalue.data(), xvalue.size() );
	      }
	    }
	    break;
	  }
	}
	else {
	  printf("WARNING: Unknown key size (%d)\n", (int)key.size());
	  printKeyValue(key,xvalue,true);
	  // try to nbt decode
	  printf("WARNING: Attempting NBT Decode:\n");
	  parseNbt(stdout, "WARNING: ", kNbtModePlain, xvalue.data(), xvalue.size());
	}
      }
      fprintf(stderr,"Read %d items\n", itemCt);
      fprintf(stderr,"Status: %s\n", iter->status().ToString().c_str());
      
      assert(iter->status().ok());  // Check for any errors found during the scan

      delete iter;

      delete db;

      return Status::OK();
    }

    int generateImages() {

      fprintf(stdout,"Report for Overworld:\n");
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


      fprintf(stdout,"Report for Nether:\n");
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

      // todo - nether movie is probably broken
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

      fprintf(stderr,"parseLevelFile: fname=%s fversion=%d flen=%d\n", fname.c_str(), fVersion, bufLen);

      int ret = -2;
      if ( bufLen > 0 ) { 
	// read content
	char* buf = new char[bufLen];
	fread(buf,1,bufLen,fp);
	fclose(fp);
	
	ret = parseNbt(stdout, "level.dat: ", kNbtModePlain, buf, bufLen);

	delete [] buf;
      }
      
      return ret;
    }
    
    
  }  // namespace
}  // namespace leveldb


void print_usage(const char* fn) {
  fprintf(stderr,"Usage:\n\n");
  fprintf(stderr,"  %s [required parameters] [options]\n\n",fn);
  fprintf(stderr,"Required Parameters:\n"
	  "  --db dir                 Directory for leveldb\n"
	  "  --out fn-part            Filename base for output file(s)\n"
	  "\n"
	  );
  fprintf(stderr,"Options:\n"
	  "  --grid                   Display chunk grid on top of world\n"
	  "  --movie                  Create movie of layers of overworld\n"
	  "  --movie-nether           Create movie of layers of nether\n"
	  "  --movie-dim x,y,w,h      Integers describing the bounds of the movie (UL X, UL Y, WIDTH, HEIGHT)\n"
	  "\n"
	  "  --verbose                verbose output\n"
	  "  --quiet                  supress normal output, continue to output warning and error messages\n"
	  "  --help                   this info\n"
	  );
}

int parse_args ( int argc, char **argv, Control& control ) {

  static struct option longoptlist[] = {
    {"db", required_argument, NULL, 'D'},
    {"out", required_argument, NULL, 'O'},

    {"biome", no_argument, NULL, 'B'},
    
    {"movie", no_argument, NULL, 'M'},
    {"movie-nether", no_argument, NULL, 'N'},
    {"movie-dim", required_argument, NULL, '*'},

    {"grid", no_argument, NULL, 'G'},

    
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

  return errct;
}


int main ( int argc, char **argv ) {

  fprintf(stderr,"%s\n",leveldb::version.c_str());
  
  int ret = parse_args(argc, argv, control);
  if (ret != 0) {
    print_usage(basename(argv[0]));
    return ret;
  }
  
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
