/*
  Minecraft Pocket Edition (MCPE) World File Visualization & Reporting Tool
  (c) Plethora777, 2015.9.26

  GPL'ed code - see LICENSE

  NBT support for mcpe_viz
*/

#ifndef __MCPE_VIZ_NBT_H__
#define __MCPE_VIZ_NBT_H__

// nbt lib stuff
#include "io/stream_reader.h"
// hide innocuous warnings here
#pragma GCC diagnostic ignored "-Wshadow"
#include "nbt_tags.h"
#pragma GCC diagnostic pop
#include <iostream>
#include <fstream>
#include <sstream>

namespace mcpe_viz {

  // helper types for NBT
  typedef std::pair<std::string, std::unique_ptr<nbt::tag> > MyNbtTag;
  typedef std::vector< MyNbtTag > MyNbtTagList;


  std::string makeGeojsonHeader(int32_t ix, int32_t iy);
  
  int parseNbt( const char* hdr, const char* buf, int bufLen, MyNbtTagList& tagList );
    
  int parseNbt_entity(int32_t dimensionId, std::string dimName, MyNbtTagList &tagList,
		      bool playerLocalFlag, bool playerRemoteFlag);
    
  int parseNbt_tileEntity(int32_t dimensionId, std::string dimName, MyNbtTagList &tagList);
    
  int parseNbt_portals(MyNbtTagList &tagList);
    
} // namespace mcpe_viz

#endif // __MCPE_VIZ_NBT_H__
