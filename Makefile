########################################################################
# Copyright (c) 2003-2007 Oliver Kreylos, Ed Puckett
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or (at
# your option) any later version.
# 
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
# USA
########################################################################

# Set the Vrui installation directory:
VRUIDIR = $(HOME)/Vrui-1.0

# Set up additional flags for the C++ compiler:
CFLAGS = 

# Include the Vrui application makefile fragment:
ifdef DEBUG
  # Build debug version of the applications, using the debug version of Vrui:
  include $(VRUIDIR)/etc/Vrui.debug.makeinclude
  CFLAGS += -g2 -O0
else
  # Build release version of the applications, using the release version of Vrui:
  include $(VRUIDIR)/etc/Vrui.makeinclude
  CFLAGS += -g0 -O3
endif


# List all project targets:
ALL = vruivnc TestVncWidget libVncTool.$(VRUI_PLUGINFILEEXT) libVncVislet.$(VRUI_PLUGINFILEEXT)

.PHONY: all
all: $(ALL)

install: all
	cp libVncTool.$(VRUI_PLUGINFILEEXT) $(VRUI_LIBDIR)/VRTools/
	cp libVncVislet.$(VRUI_PLUGINFILEEXT) $(VRUI_LIBDIR)/VRVislets/

# Rule to remove all targets:
.PHONY: clean
clean:
	-rm -rf o plugin-o
	-rm -f $(ALL) libVncVislet.* libVncTool.* 


# Pattern rules to compile C sources:
o/%.o: %.c
	@mkdir -p o/$(*D)
	@echo Compiling $<...
	@gcc -c -o $@ $(VRUI_CFLAGS) $(CFLAGS) $<

plugin-o/%.o: %.c
	@mkdir -p plugin-o/$(*D)
	@echo Compiling $<...
	@gcc -c -o $@ $(VRUI_CFLAGS) $(CFLAGS) $(VRUI_PLUGINCFLAGS) $<

o/%.o: librfb/%.c
	@mkdir -p o/$(*D)
	@echo Compiling $<...
	@gcc -c -o $@ $(VRUI_CFLAGS) $(CFLAGS) $<

plugin-o/%.o: librfb/%.c
	@mkdir -p plugin-o/$(*D)
	@echo Compiling $<...
	@gcc -c -o $@ $(VRUI_CFLAGS) $(CFLAGS) $(VRUI_PLUGINCFLAGS) $<

# Pattern rules to compile C++ sources:
o/%.o: %.cpp
	@mkdir -p o/$(*D)
	@echo Compiling $<...
	@g++ -c -o $@ $(VRUI_CFLAGS) $(CFLAGS) $<

plugin-o/%.o: %.cpp
	@mkdir -p plugin-o/$(*D)
	@echo Compiling $<...
	@g++ -c -o $@ $(VRUI_CFLAGS) $(CFLAGS) $(VRUI_PLUGINCFLAGS) $<

o/%.o: librfb/%.cpp
	@mkdir -p o/$(*D)
	@echo Compiling $<...
	@g++ -c -o $@ $(VRUI_CFLAGS) $(CFLAGS) $<

plugin-o/%.o: librfb/%.cpp
	@mkdir -p plugin-o/$(*D)
	@echo Compiling $<...
	@g++ -c -o $@ $(VRUI_CFLAGS) $(CFLAGS) $(VRUI_PLUGINCFLAGS) $<

# Pattern rule to link executables:
%: o/%.o
	@echo Linking $@...
	@g++ -o $@ $^ $(VRUI_LINKFLAGS) -lz

# Pattern rule to link plugins:
lib%.$(VRUI_PLUGINFILEEXT): plugin-o/%.o
	@echo Linking $@...
	@g++ -o $@ $^ $(VRUI_LINKFLAGS) $(VRUI_PLUGINLINKFLAGS) -lz


# List all executable dependencies:
o/d3des.o: librfb/d3des.c librfb/d3des.h

o/rfbproto.o: librfb/rfbproto.cpp librfb/corre.cppinc librfb/hextile.cppinc librfb/rre.cppinc librfb/zrle.cppinc librfb/rfbproto.h librfb/d3des.h

o/VncManager.o: VncManager.cpp VncManager.h librfb/rfbproto.h

o/VncWidget.o: VncWidget.cpp VncWidget.h VncManager.h librfb/rfbproto.h

o/vruivnc.o: vruivnc.cpp vruivnc.h VncManager.h librfb/rfbproto.h

o/TestVncWidget.o: TestVncWidget.cpp TestVncWidget.h VncWidget.h VncManager.h librfb/rfbproto.h

vruivnc: o/vruivnc.o o/VncManager.o o/rfbproto.o o/d3des.o

TestVncWidget: o/TestVncWidget.o o/VncWidget.o o/VncManager.o o/rfbproto.o o/d3des.o


# List all plugin dependencies:
plugin-o/d3des.o: librfb/d3des.c librfb/d3des.h

plugin-o/rfbproto.o: librfb/rfbproto.cpp librfb/corre.cppinc librfb/hextile.cppinc librfb/rre.cppinc librfb/zrle.cppinc librfb/rfbproto.h librfb/d3des.h

plugin-o/VncManager.o: VncManager.cpp VncManager.h librfb/rfbproto.h

plugin-o/VncWidget.o: VncWidget.cpp VncWidget.h VncManager.h librfb/rfbproto.h

plugin-o/VncDialog.o: VncDialog.cpp VncDialog.h VncWidget.h VncManager.h librfb/rfbproto.h

plugin-o/KeyboardDialog.o: KeyboardDialog.cpp KeyboardDialog.h

plugin-o/VncVislet.o: VncVislet.cpp VncVislet.h VncWidget.h VncManager.h librfb/rfbproto.h KeyboardDialog.h

plugin-o/VncTool.o: VncTool.cpp VncTool.h VncDialog.h VncWidget.h VncManager.h librfb/rfbproto.h KeyboardDialog.h

libVncVislet.$(VRUI_PLUGINFILEEXT): plugin-o/VncVislet.o plugin-o/KeyboardDialog.o plugin-o/VncDialog.o plugin-o/VncWidget.o plugin-o/VncManager.o plugin-o/rfbproto.o plugin-o/d3des.o

libVncTool.$(VRUI_PLUGINFILEEXT): plugin-o/VncTool.o plugin-o/KeyboardDialog.o plugin-o/VncDialog.o plugin-o/VncWidget.o plugin-o/VncManager.o plugin-o/rfbproto.o plugin-o/d3des.o
