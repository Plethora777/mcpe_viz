/*
  Minecraft Pocket Edition (MCPE) World File Visualization & Reporting Tool
  (c) Plethora777, 2015.9.26

  GPL'ed code - see LICENSE

  Main classes for mcpe_viz
*/

#ifndef __MCPE_VIZ_H__
#define __MCPE_VIZ_H__

#include <map>

namespace mcpe_viz {
    
  const int32_t kColorDefault = 0xff00ff;  

  // todo ugly globals
  extern Logger logger;
  extern double playerPositionImageX, playerPositionImageY;
  extern int32_t playerPositionDimensionId;
  extern std::vector<std::string> listGeoJSON;

  // dimensions
  enum DimensionType : int32_t {
    kDimIdOverworld = 0,
      kDimIdNether = 1,
      kDimIdCount = 2
      };

  
  void worldPointToImagePoint(int32_t dimId, float wx, float wz, int32_t &ix, int32_t &iy, bool geoJsonFlag);
  void worldPointToGeoJSONPoint(int32_t dimId, float wx, float wz, double &ix, double &iy);
  
  class BlockInfo {
  public:
    std::string name;
    int32_t color;
    bool colorSetFlag;
    bool solidFlag;
    bool opaqueFlag;
    bool liquidFlag;
    bool spawnableFlag;
    int32_t colorSetNeedCount;
    int32_t blockdata;
    std::vector< std::unique_ptr<BlockInfo> > variantList;

    BlockInfo() {
      name = "(unknown)";
      setColor(kColorDefault); // purple
      solidFlag = true;
      opaqueFlag = true;
      liquidFlag = false;
      spawnableFlag = true;
      colorSetFlag = false;
      colorSetNeedCount = 0;
      variantList.clear();
    }

    BlockInfo& setName(const std::string& s) {
      name = std::string(s);
      return *this;
    }

    BlockInfo& setColor(int32_t rgb) {
      // note: we convert color storage to big endian so that we can memcpy when creating images
      color = htobe32(rgb);
      colorSetFlag = true;
      return *this;
    }

    BlockInfo& setSolidFlag(bool f) {
      solidFlag = f;
      return *this;
    }
    bool isSolid() { return solidFlag; }

    BlockInfo& setOpaqueFlag(bool f) {
      opaqueFlag = f;
      return *this;
    }
    bool isOpaque() { return opaqueFlag; }

    BlockInfo& setLiquidFlag(bool f) {
      liquidFlag = f;
      return *this;
    }
    bool isLiquid() { return liquidFlag; }

    BlockInfo& setSpawnableFlag(bool f) {
      spawnableFlag = f;
      return *this;
    }
    bool isSpawnable(int32_t bd) {
      if (hasVariants()) {
	for (const auto& itbv : variantList) {
	  if ( itbv->blockdata == bd ) {
	    return itbv->spawnableFlag;
	  }
	}
	fprintf(stderr, "WARNING: did not find bd=%d (0x%x) for block='%s'\n", bd, bd, name.c_str());
      }
      return spawnableFlag;
    }

    bool hasVariants() {
      return (variantList.size() > 0);
    }
    
    void setBlockData(int32_t bd) {
      blockdata = bd;
    }

    BlockInfo& addVariant(int32_t bd, const std::string& n) {
      std::unique_ptr<BlockInfo> bv(new BlockInfo());
      bv->setName(n);
      bv->setBlockData(bd);
      variantList.push_back( std::move(bv) );
      return *(variantList.back());
    }

    std::string toString() {
      char tmpstring[1024];
      sprintf(tmpstring,"Block: name=%s color=0x%06x solid=%d opaque=%d liquid=%d spawnable=%d"
	      , name.c_str()
	      , color
	      , (int)solidFlag
	      , (int)opaqueFlag
	      , (int)liquidFlag
	      , (int)spawnableFlag
	      );
      // todo variants?
      return std::string(tmpstring);
    }
  };
  
  extern BlockInfo blockInfoList[256];



  class ItemInfo {
  public:
    std::string name;

    ItemInfo(const char* n) {
      setName(n);
    }

    ItemInfo& setName (const std::string& s) {
      name = std::string(s);
      return *this;
    }
  };

  typedef std::map<int, std::unique_ptr<ItemInfo> > ItemInfoList;
  extern ItemInfoList itemInfoList;
  bool has_key(const ItemInfoList &m, int32_t k);



  class EntityInfo {
  public:
    std::string name;

    EntityInfo(const char* n) {
      setName(n);
    }

    EntityInfo& setName (const std::string& s) {
      name = std::string(s);
      return *this;
    }
  };

  typedef std::map<int, std::unique_ptr<EntityInfo> > EntityInfoList;
  extern EntityInfoList entityInfoList;
  bool has_key(const EntityInfoList &m, int32_t k);



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

    BiomeInfo& setName (const std::string& s) {
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

  typedef std::map<int, std::unique_ptr<BiomeInfo> > BiomeInfoList;
  extern BiomeInfoList biomeInfoList;
  bool has_key(const BiomeInfoList &m, int32_t k);



  class EnchantmentInfo {
  public:
    std::string name;
    std::string officialName;

    EnchantmentInfo(const char* n) {
      setName(n);
      officialName="";
    }

    EnchantmentInfo& setName (const std::string& s) {
      name = std::string(s);
      return *this;
    }

    EnchantmentInfo& setOfficialName (const std::string& s) {
      officialName = std::string(s);
      return *this;
    }
  };

  typedef std::map<int, std::unique_ptr<EnchantmentInfo> > EnchantmentInfoList;
  extern EnchantmentInfoList enchantmentInfoList;
  bool has_key(const EnchantmentInfoList &m, int32_t k);
    
} // namespace mcpe_viz

#endif // __MCPE_VIZ_H__
