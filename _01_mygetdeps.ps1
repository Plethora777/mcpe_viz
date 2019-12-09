git clone https://github.com/marzipan42/leveldb-mcpe.git
git clone https://github.com/marzipan42/libnbtplusplus.git

curl "http://xmlsoft.org/sources/libxml2-2.9.10.tar.gz" -OutFile libxml2-2.9.10.tar.gz
curl "https://managedway.dl.sourceforge.net/project/libpng/libpng16/1.6.37/lpng1637.zip" -OutFile lpng1637.zip
curl "https://www.zlib.net/zlib1211.zip" -OutFile zlib1211.zip

# native tar added to powershell in windows 10 1803
tar zxvf libxml2-2.9.10.tar.gz
expand-archive lpng1637.zip .
expand-archive zlib1211.zip .
