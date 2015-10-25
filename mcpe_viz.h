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
  extern int32_t playerPositionImageX, playerPositionImageY;
  extern std::vector<std::string> listGeoJSON;

  // dimensions
  enum DimensionType : int32_t {
    kDimIdOverworld = 0,
      kDimIdNether = 1,
      kDimIdCount = 2
      };

  
  void worldPointToImagePoint(int32_t dimId, float wx, float wz, int &ix, int &iy, bool geoJsonFlag);
  
  class BlockInfo {
  public:
    std::string name;
    int32_t color;
    bool colorSetFlag;
    bool solidFlag;
    int colorSetNeedCount;
    int32_t blockdata;
    std::vector< std::unique_ptr<BlockInfo> > variantList;
    
    BlockInfo() {
      name = "(unknown)";
      setColor(kColorDefault); // purple
      solidFlag = true;
      colorSetFlag = false;
      colorSetNeedCount = 0;
      variantList.clear();
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
    BlockInfo& setSolidFlag(bool f) {
      solidFlag = f;
      return *this;
    }
    bool isSolid() { return solidFlag; }
    bool hasVariants() {
      return (variantList.size() > 0);
    }
    
    void setBlockData(int32_t bd) {
      blockdata = bd;
    }
    BlockInfo& addVariant(int32_t bd, std::string n) {
      std::unique_ptr<BlockInfo> bv(new BlockInfo());
      bv->setName(n);
      bv->setBlockData(bd);
      variantList.push_back( std::move(bv) );
      return *(variantList.back());
    }
  };
  
  extern BlockInfo blockInfoList[256];



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

  typedef std::map<int, std::unique_ptr<ItemInfo> > ItemInfoList;
  extern ItemInfoList itemInfoList;
  bool has_key(const ItemInfoList &m, int k);



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

  typedef std::map<int, std::unique_ptr<EntityInfo> > EntityInfoList;
  extern EntityInfoList entityInfoList;
  bool has_key(const EntityInfoList &m, int k);



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

  typedef std::map<int, std::unique_ptr<BiomeInfo> > BiomeInfoList;
  extern BiomeInfoList biomeInfoList;
  bool has_key(const BiomeInfoList &m, int k);



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

  typedef std::map<int, std::unique_ptr<EnchantmentInfo> > EnchantmentInfoList;
  extern EnchantmentInfoList enchantmentInfoList;
  bool has_key(const EnchantmentInfoList &m, int k);
    
} // namespace mcpe_viz

#endif // __MCPE_VIZ_H__
