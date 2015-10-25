/*
  Minecraft Pocket Edition (MCPE) World File Visualization & Reporting Tool
  (c) Plethora777, 2015.9.26

  GPL'ed code - see LICENSE

  XML support
*/

#include <stdio.h>
#include <libxml/xmlreader.h>
#include "mcpe_viz.util.h"
#include "mcpe_viz.h"
#include "mcpe_viz.xml.h"

namespace mcpe_viz {
    
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

  int doParseXml_Unknown(xmlNodePtr cur) {
    // some unknowns are fine (e.g. text and comment)
    if ( cur->type == XML_TEXT_NODE ) {
      // fine
    }
    else if ( cur->type == XML_COMMENT_NODE ) {
      // fine
    }
    else {
      fprintf(stderr, "WARNING: Unrecognized XML element: (parent=%s) name=(%s) type=(%d) content=(%s)\n"
	      , cur->parent ? (char*)cur->parent->name : "(NONE)"
	      , (char*)cur->name
	      , (int)cur->type
	      , cur->content ? (char*)cur->content : "(NULL)"
	      );
    }
    return 0;
  }
    
  int doParseXML_blocklist_blockvariant(xmlNodePtr cur, BlockInfo& block) {
    cur = cur->xmlChildrenNode;
    while (cur != NULL) {
      if ( xmlStrcmp(cur->name, (const xmlChar *)"blockvariant") == 0 ) {

	// example:
	//   <blockvariant blockdata="0x0" name="Oak Leaves" />

	bool blockDataValid, nameValid, colorValid, dcolorValid;
	  
	int blockdata = xmlGetInt(cur, (const xmlChar*)"blockdata", blockDataValid);
	std::string name = xmlGetString(cur, (const xmlChar*)"name", nameValid);
	int color = xmlGetInt(cur, (const xmlChar*)"color", colorValid);
	int dcolor = xmlGetInt(cur, (const xmlChar*)"dcolor", dcolorValid);

	// create data
	if ( blockDataValid && nameValid ) {
	  BlockInfo& bv = block.addVariant(blockdata,name);
	  if ( colorValid ) {
	    if ( dcolorValid ) {
	      color += dcolor;
	    }
	    bv.setColor(color);
	  } else {
	    // no color specified, we increment the parent block's color w/ blockdata (to keep it unique)
	    color = be32toh(block.color);
	    color += blockdata;
	    bv.setColor(color);
	  }
	} else {
	  // todo error
	  fprintf(stderr,"WARNING: Did not find valid blockdata and name for blockvariant of block: (%s)\n"
		  , block.name.c_str()
		  );
	}
      }
      else {
	doParseXml_Unknown(cur);
      }
	
      cur = cur->next;
    }
    return 0;
  }
    
  int doParseXML_blocklist(xmlNodePtr cur) {
    cur = cur->xmlChildrenNode;
    while (cur != NULL) {
      if ( xmlStrcmp(cur->name, (const xmlChar *)"block") == 0 ) {
	  
	bool idValid, nameValid, colorValid, solidFlagValid;
	  
	int id = xmlGetInt(cur, (const xmlChar*)"id", idValid);
	std::string name = xmlGetString(cur, (const xmlChar*)"name", nameValid);
	int color = xmlGetInt(cur, (const xmlChar*)"color", colorValid);
	bool solidFlag = xmlGetBool(cur, (const xmlChar*)"solid", true, solidFlagValid);

	// create data
	if ( idValid && nameValid ) {
	  BlockInfo& b = blockInfoList[id].setName(name);
	  if ( colorValid ) {
	    b.setColor(color);
	  }

	  b.setSolidFlag(solidFlag);

	  doParseXML_blocklist_blockvariant(cur, b);
	} else {
	  // todo error
	  fprintf(stderr,"WARNING: Did not find valid id and name for block: (0x%x) (%s) (0x%x)\n"
		  , id
		  , name.c_str()
		  , color
		  );
	}
      }
      else {
	doParseXml_Unknown(cur);
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
      else {
	doParseXml_Unknown(cur);
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
      else {
	doParseXml_Unknown(cur);
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
      else {
	doParseXml_Unknown(cur);
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
      else {
	doParseXml_Unknown(cur);
      }
      cur = cur->next;
    }
    return 0;
  }
    
  int doParseXML_xml(xmlNodePtr cur) {
    cur = cur->xmlChildrenNode;
    while (cur != NULL) {

      // todo - should count warning/errors and return this info

      if ( false ) {
      }
      else if ( xmlStrcmp(cur->name, (const xmlChar *)"blocklist") == 0 ) {
	doParseXML_blocklist(cur);
      }
      else if ( xmlStrcmp(cur->name, (const xmlChar *)"itemlist") == 0 ) {
	doParseXML_itemlist(cur);
      }
      else if ( xmlStrcmp(cur->name, (const xmlChar *)"entitylist") == 0 ) {
	doParseXML_entitylist(cur);
      }
      else if ( xmlStrcmp(cur->name, (const xmlChar *)"biomelist") == 0 ) {
	doParseXML_biomelist(cur);
      }
      else if ( xmlStrcmp(cur->name, (const xmlChar *)"enchantmentlist") == 0 ) {
	doParseXML_enchantmentlist(cur);
      }
      else {
	doParseXml_Unknown(cur);
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

    // todo - use verboseflag to show all items as they are processed
    fprintf(stderr,"Reading XML from %s\n", fn.c_str());
      
    int ret = 2;
    cur = xmlDocGetRootElement(doc);
    while (cur != NULL) {
      if ( xmlStrcmp(cur->name, (const xmlChar *)"xml") == 0 ) {
	ret = doParseXML_xml(cur);
      }
      else {
	doParseXml_Unknown(cur);
      }
      cur = cur->next;
    }

    xmlFreeDoc(doc);
    xmlCleanupParser();
      
    return ret;
  }

} // namespace mcpe_viz
