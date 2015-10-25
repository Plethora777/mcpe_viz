/*
  Minecraft Pocket Edition (MCPE) World File Visualization & Reporting Tool
  (c) Plethora777, 2015.9.26

  GPL'ed code - see LICENSE

  Utility functions
*/

#include <stdio.h>
#include <libxml/xmlreader.h>
#include "mcpe_viz.util.h"
#include "mcpe_viz.h"
#include "mcpe_viz.xml.h"

namespace mcpe_viz {

  // note: this is only used on win32/win64 builds
  // todo - doesn't check host endianness
  int32_t local_htobe32(const int32_t src) {
    int32_t dst;
    const char* ps = (char*)&src;
    char* pd = (char*)&dst;
    pd[0]=ps[3];
    pd[1]=ps[2];
    pd[2]=ps[1];
    pd[3]=ps[0];
    return dst;
  }

  // note: this is only used on win32/win64 builds
  // todo - doesn't check host endianness
  int32_t local_be32toh(const int32_t src) {
    int32_t dst;
    const char* ps = (char*)&src;
    char* pd = (char*)&dst;
    pd[0]=ps[3];
    pd[1]=ps[2];
    pd[2]=ps[1];
    pd[3]=ps[0];
    return dst;
  }

  
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

  
  int file_exists(const char* fn) {
    struct stat buf;
    int ret = stat(fn, &buf);
    return (ret == 0);
  }
    

  std::string escapeString(const std::string& s, const std::string& escapeChars) {
    if ( escapeChars.size() == 0 ) {
      return s;
    }
    std::string ret="";
    for ( const auto& ch : s ) {
      bool replaced = false;
      for ( const auto& escape : escapeChars ) {
	if ( ch == escape ) {
	  ret += "\\";
	  ret += escape;
	  replaced = true;
	  break;
	}
      }
      if (!replaced) {
	ret += ch;
      }
    }
    return ret;
  }

 
  // hacky file copying funcs
  typedef std::vector< std::pair<std::string, std::string> > StringReplacementList;
  int copyFileWithStringReplacement ( const std::string fnSrc, const std::string fnDest,
				      const StringReplacementList& replaceStrings ) {
    char buf[1025];

    //fprintf(stderr,"  copyFileWithStringReplacement src=%s dest=%s\n", fnSrc.c_str(), fnDest.c_str());

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

  
  // hacky but expedient text file copy
  int copyFile ( const std::string fnSrc, const std::string fnDest ) {
    char buf[1025];
    memset(buf,0,1025);

    //fprintf(stderr,"  copyFile src=%s dest=%s\n", fnSrc.c_str(), fnDest.c_str());
  
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



  // from: http://kickjava.com/src/org/eclipse/swt/graphics/RGB.java.htm
  int rgb2hsb(int32_t red, int32_t green, int32_t blue, double& hue, double& saturation, double &brightness) {
    double r = (double)red / 255.0;
    double g = (double)green / 255.0;
    double b = (double)blue / 255.0;
    double vmax = std::max(std::max(r, g), b);
    double vmin = std::min(std::min(r, g), b);
    double delta = vmax - vmin;
    hue = 0.0;
    brightness = vmax;
    saturation = (vmax == 0.0) ? 0.0 : ((vmax - vmin) / vmax);
      
    if (delta != 0.0) {
      if (r == vmax) {
	hue = (g - b) / delta;
      } else {
	if (g == vmax) {
	  hue = 2 + (b - r) / delta;
	} else {
	  hue = 4 + (r - g) / delta;
	}
      }
      hue *= 60.0;
      if (hue < 0.0) hue += 360.0;
    }
    return 0;
  }

#if 0    
  // from: http://stackoverflow.com/questions/3018313/algorithm-to-convert-rgb-to-hsv-and-hsv-to-rgb-in-range-0-255-for-both
  int rgb2hsv(int32_t in_r, int32_t in_g, int32_t in_b, double& out_h, double& out_s, double& out_v) {
    double min, max, delta;

    min = in_r < in_g ? in_r : in_g;
    min = min  < in_b ? min  : in_b;
      
    max = in_r > in_g ? in_r : in_g;
    max = max  > in_b ? max  : in_b;
      
    out_v = max;                                // v
    delta = max - min;
    if (delta < 0.00001)
      {
	out_s = 0;
	out_h = 0; // undefined, maybe nan?
	return 0;
      }
    if( max > 0.0 ) { // NOTE: if Max is == 0, this divide would cause a crash
      out_s = (delta / max);                  // s
    } else {
      // if max is 0, then r = g = b = 0
      // s = 0, v is undefined
      out_s = 0.0;
      out_h = NAN;                            // its now undefined
      return 0;
    }
    if( in_r >= max ) {                           // > is bogus, just keeps compilor happy
      out_h = ( in_g - in_b ) / delta;        // between yellow & magenta
    }
    else {
      if( in_g >= max ) {
	out_h = 2.0 + ( in_b - in_r ) / delta;  // between cyan & yellow
      }
      else {
	out_h = 4.0 + ( in_r - in_g ) / delta;  // between magenta & cyan
      }
    }
      
    out_h *= 60.0;                              // degrees
      
    if( out_h < 0.0 ) {
      out_h += 360.0;
    }
      
    return 0;
  }
#endif
    
    
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


  bool compareColorInfo(std::unique_ptr<ColorInfo> const& a, std::unique_ptr<ColorInfo> const& b) {
    double dh=a->h - b->h;
    if ( dh < 0.0 ) { return true; }
    if ( dh > 0.0 ) { return false; }
    double ds=a->s - b->s;
    if ( ds < 0.0 ) { return true; }
    if ( ds > 0.0 ) { return false; }
    double dl=a->l - b->l;
    if ( dl < 0.0 ) { return true; }
    if ( dl > 0.0 ) { return false; }
    return false;
  }


  bool vectorContains( const std::vector<int> &v, int i ) {
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

} // namespace mcpe_viz
