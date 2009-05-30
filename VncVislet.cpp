/***********************************************************************
  VncVislet - Class for vislets that can interact with remote VNC servers.
  Copyright (c) 2009 blackguard@voltaic.com

  This file is part of the Virtual Reality User Interface Library (Vrui).

  The Virtual Reality User Interface Library is free software; you can
  redistribute it and/or modify it under the terms of the GNU General
  Public License as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.

  The Virtual Reality User Interface Library is distributed in the hope
  that it will be useful, but WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
  PURPOSE.  See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along
  with the Virtual Reality User Interface Library; if not, write to the
  Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
  02111-1307 USA
***********************************************************************/

#include "VncVislet.h"

#include <GLMotif/WidgetManager.h>
#include <GLMotif/StyleSheet.h>
#include <GLMotif/RowColumn.h>
#include <GLMotif/Blind.h>
#include <Vrui/ToolManager.h>
#include <Vrui/Vrui.h>
#include <ctype.h>



namespace Voltaic {

//----------------------------------------------------------------------

VncVisletFactory::VncVisletFactory(Vrui::VisletManager& visletManager) :
    Vrui::VisletFactory("VncVislet", visletManager)
{
#if 0
    // Insert class into class hierarchy:
    Vrui::VisletFactory* visletFactory = visletManager.loadClass("Vislet");
    visletFactory->addChildClass(this);
    addParentClass(visletFactory);
#endif

    Vrui::ToolManager* const toolManager = Vrui::getToolManager();
    if (toolManager)
        toolManager->loadClass("VncTool");

    // Load class settings:
    Misc::ConfigurationFileSection cfs = visletManager.getVisletClassSection(getClassName());

    // Set vislet class' factory pointer:
    VncVislet::factory = this;
}



VncVisletFactory::~VncVisletFactory()
{
    // Reset vislet class' factory pointer:
    VncVislet::factory = 0;
}



Vrui::Vislet* VncVisletFactory::createVislet(int numVisletArguments, const char* const visletArguments[]) const
{
    return new VncVislet(numVisletArguments, visletArguments);
}



void VncVisletFactory::destroyVislet(Vrui::Vislet* vislet) const
{
    delete vislet;
}



extern "C" void resolveVncVisletDependencies(Plugins::FactoryManager<Vrui::VisletFactory>& manager)
{
#if 0
    // Load base classes:
    manager.loadClass("Vislet");
#endif
}



extern "C" Vrui::VisletFactory* createVncVisletFactory(Plugins::FactoryManager<Vrui::VisletFactory>& manager)
{
    Vrui::VisletManager* const visletManager = static_cast<Vrui::VisletManager*>(&manager);
    return new VncVisletFactory(*visletManager);
}



extern "C" void destroyVncVisletFactory(Vrui::VisletFactory* factory)
{
    delete factory;
}



//----------------------------------------------------------------------

VncVisletFactory* VncVislet::factory = 0;  // static member



VncVislet::VncVislet(int numArguments, const char* const arguments[]) :
    Vrui::Vislet(),
    GLObject(),
    initViaConnect(true),
    hostname(),
    rfbPort(0),
    requestedEncodings(),
    sharedDesktopFlag(false),
    enableClickThrough(true),
    initializedWithPassword(false),
    password(),
    vncDialog(0)
{
    parseArguments(numArguments, arguments);
    enable();
}



VncVislet::~VncVislet()
{
    closePopupWindow(vncDialog);
}



//----------------------------------------------------------------------
// Vrui::Vislet methods:
Vrui::VisletFactory* VncVislet::getFactory() const
{
    return factory;
}



void VncVislet::disable()
{
    closePopupWindow(vncDialog);
    this->Vrui::Vislet::disable();
}



void VncVislet::enable()
{
    if (!vncDialog)
        vncDialog = new VncDialog( "VncDialog", Vrui::getWidgetManager(),
                                   hostname.c_str(), (initializedWithPassword ? password.c_str() : 0),
                                   rfbPort, initViaConnect, requestedEncodings.c_str(), sharedDesktopFlag,
                                   enableClickThrough );

    this->Vrui::Vislet::enable();
}



void VncVislet::initContext(GLContextData& contextData) const
{
    // nothing...
}



void VncVislet::frame()
{
    if (vncDialog)
        vncDialog->checkForUpdates();

    Vrui::requestUpdate();
}



void VncVislet::display(GLContextData& contextData) const
{
    // nothing...
}



//----------------------------------------------------------------------

static const char* check_arg(const char* test, const char* arg)
{
    // Comparisons are case-insensitive.
    // Permit the following forms:
    //
    //    test
    //    test=val
    //    test:val
    //    -test
    //    -test=val
    //    -test:val
    //    --test
    //    --test=val
    //    --test:val

    if (*arg == '-') arg++;
    if (*arg == '-') arg++;

    const size_t testlen = strlen(test);
    if (strncasecmp(arg, test, testlen) != 0)
        return 0;
    else
    {
        const char* const val     = arg+testlen;
        const char* const result  = (*val == ':' || *val == '=')
                                        ? (*(val+1) ? (val+1) : 0)
                                        : (!*val    ? ""      : 0);
        return result;
    }
}

static bool parse_bool(const char* val)
{
    return !( strcasecmp("no",    val) == 0 ||
              strcasecmp("false", val) == 0 ||
              strcasecmp("0",     val) == 0    );
}

static unsigned parse_unsigned(const char* val)
{
    int n = atoi(val);
    return (unsigned)((n < 0) ? 0 : n);
}

void VncVislet::parseArguments(int numArguments, const char* const arguments[])
{
    for (int argpos = 0; argpos < numArguments; )
    {
        const char* arg = arguments[argpos++];

        const char* val = 0;
        if ((val = check_arg("hostname", arg)) != 0)
            hostname = (*val || argpos >= numArguments) ? val : arguments[argpos++];
        else if ((val = check_arg("initViaConnect", arg)) != 0)
            initViaConnect = parse_bool((*val || argpos >= numArguments) ? val : arguments[argpos++]);
        else if ((val = check_arg("rfbPort", arg)) != 0)
            rfbPort = parse_unsigned((*val || argpos >= numArguments) ? val : arguments[argpos++]);
        else if ((val = check_arg("requestedEncodings", arg)) != 0)
            requestedEncodings = (*val || argpos >= numArguments) ? val : arguments[argpos++];
        else if ((val = check_arg("sharedDesktopFlag", arg)) != 0)
            sharedDesktopFlag = parse_bool((*val || argpos >= numArguments) ? val : arguments[argpos++]);
        else if ((val = check_arg("enableClickThrough", arg)) != 0)
            enableClickThrough = parse_bool((*val || argpos >= numArguments) ? val : arguments[argpos++]);
        else if ((val = check_arg("password", arg)) != 0)
        {
            initializedWithPassword = true;
            password = (*val || argpos >= numArguments) ? val : arguments[argpos++];
        }
        else
            break;
    }
}



template<class PopupWindowClass>
    void VncVislet::closePopupWindow(PopupWindowClass*& var)
{
    if (var)
    {
        var->getManager()->popdownWidget(var);
        var->getManager()->deleteWidget(var);
        var = 0;
    }
}

}  // end of namespace Voltaic
