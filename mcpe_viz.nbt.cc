/*
  Minecraft Pocket Edition (MCPE) World File Visualization & Reporting Tool
  (c) Plethora777, 2015.9.26

  GPL'ed code - see LICENSE

  NBT support
*/

#include <stdio.h>
#include <algorithm>
#include "mcpe_viz.util.h"
#include "mcpe_viz.h"
#include "mcpe_viz.nbt.h"

namespace mcpe_viz {

  // nbt parsing helpers
  int globalNbtListNumber=0;
  int globalNbtCompoundNumber=0;

  int parseNbtTag( const char* hdr, int& indent, const MyNbtTag& t ) {

    logger.msg(kLogInfo1,"%s[%s] ", makeIndent(indent,hdr).c_str(), t.first.c_str());

    nbt::tag_type tagType = t.second->get_type();
      
    switch ( tagType ) {
    case nbt::tag_type::End:
      logger.msg(kLogInfo1,"TAG_END\n");
      break;
    case nbt::tag_type::Byte:
      {
	nbt::tag_byte v = t.second->as<nbt::tag_byte>();
	logger.msg(kLogInfo1,"%d 0x%x (byte)\n", v.get(), v.get());
      }
      break;
    case nbt::tag_type::Short:
      {
	nbt::tag_short v = t.second->as<nbt::tag_short>();
	logger.msg(kLogInfo1,"%d 0x%x (short)\n", v.get(), v.get());
      }
      break;
    case nbt::tag_type::Int:
      {
	nbt::tag_int v = t.second->as<nbt::tag_int>();
	logger.msg(kLogInfo1,"%d 0x%x (int)\n", v.get(), v.get());
      }
      break;
    case nbt::tag_type::Long:
      {
	nbt::tag_long v = t.second->as<nbt::tag_long>();
	// note: silly work around for linux vs win32 weirdness
	logger.msg(kLogInfo1,"%lld 0x%llx (long)\n", (long long int)v.get(), (long long int)v.get());
      }
      break;
    case nbt::tag_type::Float:
      {
	nbt::tag_float v = t.second->as<nbt::tag_float>();
	logger.msg(kLogInfo1,"%f (float)\n", v.get());
      }
      break;
    case nbt::tag_type::Double:
      {
	nbt::tag_double v = t.second->as<nbt::tag_double>();
	logger.msg(kLogInfo1,"%lf (double)\n", v.get());
      }
      break;
    case nbt::tag_type::Byte_Array:
      {
	nbt::tag_byte_array v = t.second->as<nbt::tag_byte_array>();
	logger.msg(kLogInfo1,"[");
	int i=0;
	for (const auto& itt: v ) {
	  if ( i++ > 0 ) { logger.msg(kLogInfo1," "); }
	  logger.msg(kLogInfo1,"%02x", (int)itt);
	}
	logger.msg(kLogInfo1,"] (hex byte array)\n");
      }
      break;
    case nbt::tag_type::String:
      {
	nbt::tag_string v = t.second->as<nbt::tag_string>();
	logger.msg(kLogInfo1,"'%s' (string)\n", v.get().c_str());
      }
      break;
    case nbt::tag_type::List:
      {
	nbt::tag_list v = t.second->as<nbt::tag_list>();
	int lnum = ++globalNbtListNumber;
	logger.msg(kLogInfo1,"LIST-%d {\n",lnum);
	indent++;
	for ( const auto& it: v ) {
	  parseNbtTag( hdr, indent, std::make_pair(std::string(""), it.get().clone() ) );
	}
	if ( --indent < 0 ) { indent=0; }
	logger.msg(kLogInfo1,"%s} LIST-%d\n", makeIndent(indent,hdr).c_str(), lnum);
      }
      break;
    case nbt::tag_type::Compound:
      {
	nbt::tag_compound v = t.second->as<nbt::tag_compound>();
	int cnum = ++globalNbtCompoundNumber;
	logger.msg(kLogInfo1,"COMPOUND-%d {\n",cnum);
	indent++;
	for ( const auto& it: v ) {
	  parseNbtTag( hdr, indent, std::make_pair( it.first, it.second.get().clone() ) );
	}
	if ( --indent < 0 ) { indent=0; }
	logger.msg(kLogInfo1,"%s} COMPOUND-%d\n", makeIndent(indent,hdr).c_str(),cnum);
      }
      break;
    case nbt::tag_type::Int_Array:
      {
	nbt::tag_int_array v = t.second->as<nbt::tag_int_array>();
	logger.msg(kLogInfo1,"[");
	int i=0;
	for ( const auto& itt: v ) {
	  if ( i++ > 0 ) { logger.msg(kLogInfo1," "); }
	  logger.msg(kLogInfo1,"%x", itt);
	}
	logger.msg(kLogInfo1,"] (hex int array)\n");
      }
      break;
    default:
      logger.msg(kLogInfo1,"[ERROR: Unknown tag type = %d]\n", (int)tagType);
      break;
    }

    return 0;
  }

    
  int parseNbt( const char* hdr, const char* buf, int bufLen, MyNbtTagList& tagList ) {
    int indent=0;
    logger.msg(kLogInfo1,"%sNBT Decode Start\n",makeIndent(indent,hdr).c_str());

    // these help us look at dumped nbt data and match up LIST's and COMPOUND's
    globalNbtListNumber=0;
    globalNbtCompoundNumber=0;
      
    std::istringstream is(std::string(buf,bufLen));
    nbt::io::stream_reader reader(is, endian::little);

    // remove all elements from taglist
    tagList.clear();
      
    // read all tags
    MyNbtTag t;
    bool done = false;
    std::istream& pis = reader.get_istr();
    while ( !done && (pis) && (!pis.eof()) ) {
      try {
	// todobig emplace_back?
	tagList.push_back(reader.read_tag());
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
      
    logger.msg(kLogInfo1,"%sNBT Decode End (%d tags)\n",makeIndent(indent,hdr).c_str(), (int)tagList.size());

    return 0;
  }


  
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
	sprintf(tmpstring," (%d)\"", level.value);
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
	s = "\"Enchantments\": [ ";
	int i = enchantmentList.size();
	for ( const auto& it: enchantmentList ) {
	  s+="{ " + it->toGeoJSON() + " }";
	  if ( --i > 0 ) {
	    s += ",";
	  }
	}
	s += "]";
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
    // todobig - this is very handy + powerful - use it for other Parsed classes?
    std::vector< std::pair<std::string,std::string> > otherProps;
    bool otherPropsSortedFlag;
      
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
      otherProps.clear();
      otherPropsSortedFlag = false;
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

    int addOtherProp(std::string key, std::string value) {
      otherProps.push_back( std::make_pair(key,value) );
      return 0;
    }
      
    int checkOtherProp(nbt::tag_compound& tc, std::string key) {
      char tmpstring[1025];
      if ( tc.has_key(key)) {
	// seems silly that we have to do this:
	const nbt::tag_type tagType = tc[key].get_type();
	switch ( tagType ) {
	case nbt::tag_type::Byte:
	  {
	    int8_t v = tc[key].as<nbt::tag_byte>().get();
	    sprintf(tmpstring,"%d (0x%x)", v, v);
	    addOtherProp(key, std::string(tmpstring));
	  }
	  break;
	case nbt::tag_type::Short:
	  {
	    int16_t v = tc[key].as<nbt::tag_short>().get();
	    sprintf(tmpstring,"%d (0x%x)", v, v);
	    addOtherProp(key, std::string(tmpstring));
	  }
	  break;
	case nbt::tag_type::Int:
	  {
	    int32_t v = tc[key].as<nbt::tag_int>().get();
	    sprintf(tmpstring,"%d (0x%x)", v, v);
	    addOtherProp(key, std::string(tmpstring));
	  }
	  break;
	case nbt::tag_type::Long:
	  {
	    int64_t v = tc[key].as<nbt::tag_long>().get();
	    sprintf(tmpstring,"%lld (0x%llx)", (long long int)v, (long long int)v);
	    addOtherProp(key, std::string(tmpstring));
	  }
	  break;
	case nbt::tag_type::Float:
	  {
	    sprintf(tmpstring,"%f",tc[key].as<nbt::tag_float>().get());
	    addOtherProp(key, std::string(tmpstring));
	  }
	  break;
	case nbt::tag_type::Double:
	  {
	    sprintf(tmpstring,"%lf",tc[key].as<nbt::tag_double>().get());
	    addOtherProp(key, std::string(tmpstring));
	  }
	  break;
	case nbt::tag_type::String:
	  {
	    addOtherProp(key, std::string(tc[key].as<nbt::tag_string>().get()));
	  }
	  break;
	default:
	  // todo - err?
	  // items we don't parse (yet)
	  //Byte_Array = 7,
	  //List = 9,
	  //Compound = 10,
	  //Int_Array = 11,
	  fprintf(stderr,"WARNING: Unable to capture entity other prop key=%s\n", key.c_str());
	  break;
	}
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
	"{ "
	"\"type\": \"Feature\", "
	"\"geometry\": { \"type\": \"Point\", \"coordinates\": ["
	;
      s += tmpstring;
      s +=
	"] }, "
	"\"properties\": { "
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
	  std::string ts = "\"Armor\": [ ";
	  int i = tlist.size();
	  for (const auto& iter : tlist ) {
	    ts += iter;
	    if ( --i > 0 ) {
	      ts += ",";
	    }
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
	  std::string ts = "\"Inventory\": [ ";
	  int i = tlist.size();
	  for (const auto& iter : tlist ) {
	    ts += iter;
	    if ( --i > 0 ) {
	      ts += ",";
	    }
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

      if ( ! otherPropsSortedFlag ) {
	std::sort( otherProps.begin(), otherProps.end() );
	otherPropsSortedFlag = true;
      }
      for ( const auto& it : otherProps ) {
	list.push_back(std::string("\"" + it.first + "\": \"" + it.second + "\""));
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
	}
      }
      s += "} }";
	
      return s;
    }
      
    // todo - this should probably be multi-line so it's not so insane looking :)
    std::string toString(int32_t forceDimensionId) {
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

      if ( ! otherPropsSortedFlag ) {
	std::sort( otherProps.begin(), otherProps.end() );
	otherPropsSortedFlag = true;
      }
      for ( const auto& it : otherProps ) {
	s += " " + it.first + "=" + it.second;
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
	  // todobig - should we keep lists and combine chests so that we can show full content of double chests?
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
	  std::string ts = "\"Items\": [ ";
	  int i = tlist.size();
	  for (const auto& iter : tlist ) {
	    ts += "{ " + iter + " }";
	    if ( --i > 0 ) {
	      ts += ",";
	    }
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
	  sprintf(tmpstring,"\"Text%d\": \"%s\"", t++, escapeString(it,"\"").c_str());
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
	ts += "}";
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
	  "{ "
	  "\"type\": \"Feature\", "
	  "\"geometry\": { \"type\": \"Point\", \"coordinates\": ["
	  ;
	s += tmpstring;
	s +=
	  "] }, "
	  "\"properties\": { "
	  ;
	  
	int i = list.size();
	for (const auto& iter : list ) {
	  s += iter;
	  if ( --i > 0 ) {
	    s += ",";
	  }
	}
	s += "} }";
	return s;
      }

      return std::string("");
    }
      
    // todo - this should probably be multi-line so it's not so insane looking :)
    std::string toString(int32_t dimensionId) {
      char tmpstring[1025];
	
      std::string s = "[";
	
      s += "Pos=" + pos.toStringWithImageCoords(dimensionId);

      if ( items.size() > 0 ) {
	if ( pairChest.valid ) {
	  // todobig - should we keep lists and combine chests so that we can show full content of double chests?
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

    
  int parseNbt_entity(int32_t dimensionId, std::string dimName, MyNbtTagList &tagList, bool playerLocalFlag, bool playerRemoteFlag) {
    ParsedEntityList entityList;
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

      // todo - diff entities have other fields:
      // see: http://minecraft.gamepedia.com/Chunk_format#Mobs

      // chicken: IsChickenJockey
      entity->checkOtherProp(tc, "IsChickenJockey");
	
      // ocelot: CatType
      entity->checkOtherProp(tc, "CatType");
	
      // sheep: Sheared; Color
      entity->checkOtherProp(tc, "Sheared");
      entity->checkOtherProp(tc, "Color");
	
      // skeleton: SkeletonType (wither skeleton!)
      entity->checkOtherProp(tc, "SkeletonType");

      // slime: Size
      entity->checkOtherProp(tc, "Size");
      entity->checkOtherProp(tc, "OnGround");
	
      // wolf: Angry; CollarColor
      entity->checkOtherProp(tc, "Angry");
      entity->checkOtherProp(tc, "CollarColor");
      entity->checkOtherProp(tc, "Owner"); 
      entity->checkOtherProp(tc, "OwnerNew"); // mcpe only?

      // villager: Profession; Riches; Career; CareerLevel; Willing; (Inventory); [Offers]
      entity->checkOtherProp(tc, "Profession");
      entity->checkOtherProp(tc, "Riches");
      entity->checkOtherProp(tc, "Career");
      entity->checkOtherProp(tc, "CareerLevel");
      entity->checkOtherProp(tc, "Willing");
      entity->checkOtherProp(tc, "Sitting");

      // breedable mobs
      entity->checkOtherProp(tc, "InLove");
      entity->checkOtherProp(tc, "Age");
      entity->checkOtherProp(tc, "ForcedAge");
	
      // zombie: (IsVillager?); IsBaby;
      entity->checkOtherProp(tc, "IsBaby");

      // zombie pigman
      entity->checkOtherProp(tc, "Anger");
      entity->checkOtherProp(tc, "HurtBy");

      // enderman
      entity->checkOtherProp(tc, "carried");
      entity->checkOtherProp(tc, "carriedData");

      // creeper
      entity->checkOtherProp(tc, "IsPowered");
	
      // common
      entity->checkOtherProp(tc, "Health");
      entity->checkOtherProp(tc, "OwnerID"); 
      entity->checkOtherProp(tc, "Persistent");

      entity->checkOtherProp(tc, "PlayerCreated"); // golems?
	
      // IsGlobal - but appears to be always 0

      // todo - LinksTag - might be spider jockeys?
	
      // stuff I found:
      entity->checkOtherProp(tc, "SpawnedByNight");
	
      logger.msg(kLogInfo1, "%sParsedEntity: %s\n", dimName.c_str(), entity->toString(actualDimensionId).c_str());

      listGeoJSON.push_back( entity->toGeoJSON(actualDimensionId) );

      entityList.push_back( std::move(entity) );
    }

    return 0;
  }

    
  int parseNbt_tileEntity(int32_t dimensionId, std::string dimName, MyNbtTagList &tagList) {
    ParsedTileEntityList tileEntityList;
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
	  logger.msg(kLogInfo1,"ERROR: Unknown tileEntity id=(%s)\n", id.c_str());
	}
      }

      if ( parseFlag ) {
	logger.msg(kLogInfo1, "%sParsedTileEntity: %s\n", dimName.c_str(), tileEntity->toString(dimensionId).c_str());

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
	  "{ "
	  "\"type\": \"Feature\", "
	  "\"geometry\": { \"type\": \"Point\", \"coordinates\": ["
	  ;
	s += tmpstring;
	s +=
	  "] }, "
	  "\"properties\": { "
	  ;
	  
	int i = list.size();
	for (const auto& iter : list ) {
	  s += iter;
	  if ( --i > 0 ) {
	    s += ",";
	  }
	}
	s += "} }";
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

    
  int parseNbt_portals(MyNbtTagList &tagList) {
    ParsedPortalList portalList;
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
		
	      logger.msg(kLogInfo1, "ParsedPortal: %s\n", portal->toString().c_str());
		
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
  
} // namespace mcpe_viz
