PLUGIN_NAME = mea

HEADERS = mea.h\
          /usr/local/lib/rtxi_includes/scrollbar.h\
          /usr/local/lib/rtxi_includes/scrollzoomer.h\
          /usr/local/lib/rtxi_includes/basicplot.h\

SOURCES = mea.cpp \
          moc_mea.cpp\
          /usr/local/lib/rtxi_includes/basicplot.cpp\
          /usr/local/lib/rtxi_includes/scrollbar.cpp\
          /usr/local/lib/rtxi_includes/scrollzoomer.cpp\
          /usr/local/lib/rtxi_includes/moc_scrollbar.cpp\
          /usr/local/lib/rtxi_includes/moc_scrollzoomer.cpp\
          /usr/local/lib/rtxi_includes/moc_basicplot.cpp\

LIBS = -lqwt

### Do not edit below this line ###

include $(shell rtxi_plugin_config --pkgdata-dir)/Makefile.plugin_compile
