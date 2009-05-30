/***********************************************************************
  VncTool - Class for tools that can interact with remote VNC servers.
  Copyright (c) 2007,2008 Voltaic

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

#include <Misc/StandardValueCoders.h>
#include <Misc/CompoundValueCoders.h>
#include <Geometry/Point.h>
#include <Geometry/Vector.h>
#include <GL/gl.h>
#include <GL/GLGeometryWrappers.h>
#include <GL/GLFont.h>
#include <GL/GLMaterial.h>
#include <GL/GLPolylineTube.h>
#include <GLMotif/Widget.h>
#include <GLMotif/WidgetAlgorithms.h>
#include <GLMotif/RowColumn.h>
#include <GLMotif/Blind.h>
#include <GLMotif/ToggleButton.h>
#include <GLMotif/TextField.h>
#include <GLMotif/TitleBar.h>
#include <GLMotif/StyleSheet.h>
#include <Vrui/ToolManager.h>
#include <Vrui/Vrui.h>

#include "VncTool.h"



namespace Voltaic {

//----------------------------------------------------------------------

class UpperLeftCornerPreserver
{
public:
    UpperLeftCornerPreserver(GLMotif::PopupWindow*& popupWindow) :
        popupWindow(popupWindow)
    {
        if (popupWindow)
        {
            const GLMotif::Box priorExterior = popupWindow->getExterior();
            priorUpperLeft[0] = priorExterior.origin[0];
            priorUpperLeft[1] = (priorExterior.origin[1] + priorExterior.size[1]);
            priorUpperLeft[2] = 0.0;
            Vrui::getWidgetManager()->calcWidgetTransformation(popupWindow).transform(priorUpperLeft);  // convert to world coordinates
        }
    }

    ~UpperLeftCornerPreserver()
    {
        if (popupWindow)
        {
            const GLMotif::WidgetManager::Transformation xform = Vrui::getWidgetManager()->calcWidgetTransformation(popupWindow);

            GLMotif::Box         exterior = popupWindow->getExterior();
            const GLMotif::Point newUpperLeft(exterior.origin[0], (exterior.origin[1] + exterior.size[1]), 0.0);
            xform.transform(newUpperLeft);  // convert to world coordinates

            if (newUpperLeft[1] != priorUpperLeft[1])
            {
                xform.inverseTransform(priorUpperLeft);  // convert back to widget coordinates
                exterior.origin[1] = priorUpperLeft[1] - exterior.size[1];
                popupWindow->resize(exterior);
            }
        }
    }

private:
    GLMotif::PopupWindow*& popupWindow;
    GLMotif::Point         priorUpperLeft;
};



static void expandStringEscapes(std::string& s)
{
    static const char ESCAPE_INTRO = '\\';
    static const char ESCAPE_NL    = 'n';
    static const char ESCAPE_CR    = 'r';
    static const char ESCAPE_TAB   = 't';
    static const char ESCAPE_SPC   = ' ';

    if (s.find(ESCAPE_INTRO, 0) != std::string::npos)
    {
        for (std::string::size_type i = 0; i < s.size(); i++)
            if (s.at(i) == ESCAPE_INTRO)
            {
                if (i >= s.size()-1)
                    Misc::throwStdErr("Dangling escape character at end of string \"%s\"", s.c_str());
                else
                {
                    const char tag = s.at(i+1);
                    char rep;

                    switch (tag)
                    {
                        case ESCAPE_INTRO: rep = '\\'; break;
                        case ESCAPE_NL:    rep = '\n'; break;
                        case ESCAPE_CR:    rep = '\r'; break;
                        case ESCAPE_TAB:   rep = '\t'; break;
                        case ESCAPE_SPC:   rep = ' ';  break;
                        default: Misc::throwStdErr("Unknown escape sequence '%c%c' at position %d in string \"%s\"", ESCAPE_INTRO, tag, (int)i, s.c_str());
                    }

                    s.replace(i, 1, 1, rep);
                    s.erase(i+1, 1);
                }
            }
    }
}



class FindTitleFunctor
{
protected:
    const char*& titleDest;

public:
    FindTitleFunctor(const char*& titleDest) :
        titleDest(titleDest)
    {
        titleDest = 0;
    }

    virtual void operator()(GLMotif::Widget* widget) const;
};



void FindTitleFunctor::operator()(GLMotif::Widget* widget) const
{
    if (!titleDest)  // only return the first occurrence
    {
        GLMotif::TitleBar* const titleBar = dynamic_cast<GLMotif::TitleBar*>(widget);
        if (titleBar)
            titleDest = titleBar->getLabel();
    }
}



static const char* getTitleFromTitleBarOfRootContaining(GLMotif::Widget* widget)
{
    const char* title = 0;

    if (widget)
    {
        GLMotif::PopupWindow* const popupWindow = dynamic_cast<GLMotif::PopupWindow*>(widget->getRoot());
        if (popupWindow)
            title = popupWindow->getTitleString();
    }

    return title;
}



//----------------------------------------------------------------------

class WidgetDataRetrievalFunctor
{
public:
    WidgetDataRetrievalFunctor( const char* beginDataString,
                                const char* interDatumString,
                                const char* endDataString,
                                const char* timestampFormat,
                                const char* tagString,
                                const char* dialogTitleString ) :
        beginDataString(beginDataString),
        interDatumString(interDatumString),
        endDataString(endDataString),
        result(),
        anyDataSeen(false),
        timestampFormat(timestampFormat),
        tagString(tagString),
        dialogTitleString(dialogTitleString)
    {
    }

    virtual void reset();
    virtual void complete();

    virtual void operator()(const GLMotif::Widget* widget);

protected:
    const char* const beginDataString;
    const char* const interDatumString;
    const char* const endDataString;
    const char* const timestampFormat;
    const char* const tagString;
    const char* const dialogTitleString;

public:
    std::string result;
    bool        anyDataSeen;
};



void WidgetDataRetrievalFunctor::reset()
{
    result.erase();
    anyDataSeen = false;
}



void WidgetDataRetrievalFunctor::complete()
{
    if (anyDataSeen && endDataString)
        result += endDataString;
}



void WidgetDataRetrievalFunctor::operator()(const GLMotif::Widget* widget)
{
    const GLMotif::TextField* const textField = dynamic_cast<const GLMotif::TextField*>(widget);

    if (textField)
    {
        if (!anyDataSeen)
        {
            if (beginDataString)
                result += beginDataString;

            if (timestampFormat)
            {
                const time_t t = time(0);
                char timestamp[256];
                strftime(timestamp, sizeof(timestamp), timestampFormat, localtime(&t));
                result += timestamp;

                if (interDatumString)
                    result += interDatumString;
            }

            if (tagString)
            {
                result += tagString;

                if (interDatumString)
                    result += interDatumString;
            }

            if (dialogTitleString)
            {
                result += dialogTitleString;

                if (interDatumString)
                    result += interDatumString;
            }
        }
        else
        {
            if (interDatumString)
                result += interDatumString;
        }

        result += textField->getLabel();

        anyDataSeen = true;
    }
}



static GLMotif::Point GLMotifPointFromGLMotifVector(const GLMotif::Vector v)
{
    return GLMotif::Point(v[0], v[1], v[2]);
}



//----------------------------------------------------------------------

VncToolFactory::HostDescriptor::HostDescriptor() :
    VncManager::RFBProtocolStartupData(),
    desktopHostString(),
    requestedEncodingsString(),
    beginDataString(),
    interDatumString(),
    endDataString(),
    initialEnableClickThrough(false),
    initialTimestampBeamedData(false),
    initialBeamedDataTag(),
    initialAutoBeam(false)
{
}



VncToolFactory::HostDescriptor::HostDescriptor( Misc::ConfigurationFileSection& cfs,
                                                const char*                     configFileSection,
                                                const char*                     hostName ) :
    VncManager::RFBProtocolStartupData(),
    desktopHostString(hostName ? hostName : ""),
    requestedEncodingsString(),
    beginDataString(),
    interDatumString(),
    endDataString(),
    initialEnableClickThrough(false),
    initialTimestampBeamedData(false),
    initialBeamedDataTag(),
    initialAutoBeam(false)
{
    if (hostName && strchr(hostName, '/'))
        Misc::throwStdErr("Illegal hostname format \"%s\"", (hostName ? hostName : ""));

    beginDataString            = cfs.retrieveValue<std::string>( "beginDataString",            ""    );
    interDatumString           = cfs.retrieveValue<std::string>( "interDatumString",           "\t"  );
    endDataString              = cfs.retrieveValue<std::string>( "endDataString",              "\n"  );
    initialEnableClickThrough  = cfs.retrieveValue<bool>(        "initialEnableClickThrough",  true  );
    initialTimestampBeamedData = cfs.retrieveValue<bool>(        "initialTimestampBeamedData", true  );
    initialBeamedDataTag       = cfs.retrieveValue<std::string>( "initialBeamedDataTag",       ""    );
    initialAutoBeam            = cfs.retrieveValue<bool>(        "initialAutoBeam",            false );

    requestedEncodingsString   = cfs.retrieveValue<std::string>( "requestedEncodings",         ""    );

    this->RFBProtocolStartupData::initViaConnect    = cfs.retrieveValue<bool>(     "initViaConnect",    true  );
    this->RFBProtocolStartupData::rfbPort           = cfs.retrieveValue<unsigned>( "rfbPort",           0     );
    this->RFBProtocolStartupData::sharedDesktopFlag = cfs.retrieveValue<bool>(     "sharedDesktopFlag", true  );

    if (hostName)
    {
        std::string prefix = configFileSection ? configFileSection : ".";
        prefix += "/hostDescription_";
        prefix += hostName;
        prefix += '/';

        beginDataString            = cfs.retrieveValue<std::string>( ( prefix+"beginDataString"            ).c_str(), beginDataString );
        interDatumString           = cfs.retrieveValue<std::string>( ( prefix+"interDatumString"           ).c_str(), interDatumString );
        endDataString              = cfs.retrieveValue<std::string>( ( prefix+"endDataString"              ).c_str(), endDataString );
        initialEnableClickThrough  = cfs.retrieveValue<bool>(        ( prefix+"initialEnableClickThrough"  ).c_str(), initialEnableClickThrough );
        initialTimestampBeamedData = cfs.retrieveValue<bool>(        ( prefix+"initialTimestampBeamedData" ).c_str(), initialTimestampBeamedData );
        initialBeamedDataTag       = cfs.retrieveValue<std::string>( ( prefix+"initialBeamedDataTag"       ).c_str(), initialBeamedDataTag );
        initialAutoBeam            = cfs.retrieveValue<bool>(        ( prefix+"initialAutoBeam"            ).c_str(), initialAutoBeam );

        requestedEncodingsString   = cfs.retrieveValue<std::string>( ( prefix+"requestedEncodings"         ).c_str(), requestedEncodingsString );

        this->RFBProtocolStartupData::initViaConnect    = cfs.retrieveValue<bool>(     ( prefix+"initViaConnect"    ).c_str(), this->RFBProtocolStartupData::initViaConnect);
        this->RFBProtocolStartupData::rfbPort           = cfs.retrieveValue<unsigned>( ( prefix+"rfbPort"           ).c_str(), this->RFBProtocolStartupData::rfbPort);
        this->RFBProtocolStartupData::sharedDesktopFlag = cfs.retrieveValue<bool>(     ( prefix+"sharedDesktopFlag" ).c_str(), this->RFBProtocolStartupData::sharedDesktopFlag);
    }

    this->RFBProtocolStartupData::desktopHost        = this->desktopHostString.c_str();
    this->RFBProtocolStartupData::requestedEncodings = this->requestedEncodingsString.c_str();
    if (this->RFBProtocolStartupData::requestedEncodings && !*this->RFBProtocolStartupData::requestedEncodings)
        this->RFBProtocolStartupData::requestedEncodings = 0;

    expandStringEscapes(beginDataString);
    expandStringEscapes(interDatumString);
    expandStringEscapes(endDataString);

    expandStringEscapes(initialBeamedDataTag);
}



VncToolFactory::HostDescriptor::HostDescriptor(const HostDescriptor& other)
{
    this->beginDataString                            = other.beginDataString;
    this->interDatumString                           = other.interDatumString;
    this->endDataString                              = other.endDataString;

    this->initialEnableClickThrough                  = other.initialEnableClickThrough;
    this->initialTimestampBeamedData                 = other.initialTimestampBeamedData;
    this->initialBeamedDataTag                       = other.initialBeamedDataTag;
    this->initialAutoBeam                            = other.initialAutoBeam;

    this->desktopHostString                          = other.desktopHostString;
    this->requestedEncodingsString                   = other.requestedEncodingsString;

    this->RFBProtocolStartupData::initViaConnect     = other.RFBProtocolStartupData::initViaConnect;
    this->RFBProtocolStartupData::desktopHost        = this->desktopHostString.c_str();
    this->RFBProtocolStartupData::rfbPort            = other.RFBProtocolStartupData::rfbPort;
    this->RFBProtocolStartupData::requestedEncodings = this->requestedEncodingsString.c_str();
    this->RFBProtocolStartupData::sharedDesktopFlag  = other.RFBProtocolStartupData::sharedDesktopFlag;

    if (this->RFBProtocolStartupData::requestedEncodings && !*this->RFBProtocolStartupData::requestedEncodings)
        this->RFBProtocolStartupData::requestedEncodings = 0;
}



VncToolFactory::HostDescriptor::HostDescriptor& VncToolFactory::HostDescriptor::operator=(const HostDescriptor& other)
{
    this->beginDataString                            = other.beginDataString;
    this->interDatumString                           = other.interDatumString;
    this->endDataString                              = other.endDataString;

    this->initialEnableClickThrough                  = other.initialEnableClickThrough;
    this->initialTimestampBeamedData                 = other.initialTimestampBeamedData;
    this->initialBeamedDataTag                       = other.initialBeamedDataTag;
    this->initialAutoBeam                            = other.initialAutoBeam;

    this->desktopHostString                          = other.desktopHostString;
    this->requestedEncodingsString                   = other.requestedEncodingsString;

    this->RFBProtocolStartupData::initViaConnect     = other.RFBProtocolStartupData::initViaConnect;
    this->RFBProtocolStartupData::desktopHost        = this->desktopHostString.c_str();
    this->RFBProtocolStartupData::rfbPort            = other.RFBProtocolStartupData::rfbPort;
    this->RFBProtocolStartupData::requestedEncodings = this->requestedEncodingsString.c_str();
    this->RFBProtocolStartupData::sharedDesktopFlag  = other.RFBProtocolStartupData::sharedDesktopFlag;

    if (this->RFBProtocolStartupData::requestedEncodings && !*this->RFBProtocolStartupData::requestedEncodings)
        this->RFBProtocolStartupData::requestedEncodings = 0;

    return *this;
}



void VncToolFactory::HostDescriptor::setDesktopHost(const char* hostName)
{
    this->desktopHostString = hostName;
    this->RFBProtocolStartupData::desktopHost = this->desktopHostString.c_str();
}



//----------------------------------------------------------------------

VncToolFactory::VncToolFactory(Vrui::ToolManager& toolManager) :
    ToolFactory("VncTool", toolManager),
    blankHostDescriptor(),
    hostDescriptors()
{
    // Initialize tool layout:
    layout.setNumDevices(1);
    layout.setNumButtons(0, 1);

    // Insert class into class hierarchy:
    ToolFactory* toolFactory = toolManager.loadClass("UtilityTool");
    toolFactory->addChildClass(this);
    addParentClass(toolFactory);

    // Load class settings:
    Misc::ConfigurationFileSection cfs = toolManager.getToolClassSection(getClassName());

    typedef std::vector<std::string> StringList;

    blankHostDescriptor = HostDescriptor(cfs, 0);

    // Note: VncTool relies on the fact that the hostDescriptors list never changes
    // once initialized.  If this assumption should change in the future, then perhaps
    // store the hostName instead of a pointer to the particular hostDescriptors entry
    // in VncTool.  See VncTool::changeHostCallback().  One issue would be that you
    // would also need to store a flag whether or not there is a host selected at all
    // because a NULL or empty hostName is synonymous with localhost.
    StringList emptyStringList;
    StringList hostNames = cfs.retrieveValue<StringList>("./hostNames", emptyStringList);
    for (StringList::const_iterator it = hostNames.begin(); it != hostNames.end(); ++it)
        hostDescriptors.push_back(HostDescriptor(cfs, 0, it->c_str()));

    // Set tool class' factory pointer:
    VncTool::factory = this;
}



VncToolFactory::~VncToolFactory()
{
    // Reset tool class' factory pointer:
    VncTool::factory = 0;
}



const char* VncToolFactory::getName() const
{
    return "VncTool";
}



Vrui::Tool* VncToolFactory::createTool(const Vrui::ToolInputAssignment& inputAssignment) const
{
    return new VncTool(this, inputAssignment);
}



void VncToolFactory::destroyTool(Vrui::Tool* tool) const
{
    delete tool;
}



extern "C" void resolveVncToolDependencies(Plugins::FactoryManager<Vrui::ToolFactory>& manager)
{
    // Load base classes:
    manager.loadClass("UtilityTool");
}



extern "C" Vrui::ToolFactory* createVncToolFactory(Plugins::FactoryManager<Vrui::ToolFactory>& manager)
{
    // Get pointer to tool manager:
    Vrui::ToolManager* toolManager = static_cast<Vrui::ToolManager*>(&manager);

    // Create factory object and insert it into class hierarchy:
    VncToolFactory* widgetToolFactory = new VncToolFactory(*toolManager);

    // Return factory object:
    return widgetToolFactory;
}



extern "C" void destroyVncToolFactory(Vrui::ToolFactory* factory)
{
    delete factory;
}



//----------------------------------------------------------------------

VncTool::Animation::~Animation()
{
}



//----------------------------------------------------------------------

class DataRetrievalAnimationX : public VncTool::Animation
{
public:
    static const double   RunningTime;
    static const double   WaveDurationFraction;
    static const unsigned WaveCount;
    static const double   WaveThicknessIncrement;
    static const double   WaveStartThickness;
    static const double   WaveAmbientColorRed;
    static const double   WaveAmbientColorGreen;
    static const double   WaveAmbientColorBlue;
    static const double   WaveSpecularColorRed;
    static const double   WaveSpecularColorGreen;
    static const double   WaveSpecularColorBlue;
    static const double   WaveShininess;
    static const unsigned WaveTubeSegmentCount;

public:
    DataRetrievalAnimationX( const GLMotif::Point&  lastClickPoint,
                             const GLMotif::Widget* from,
                             const GLMotif::Widget* to,
                             const char*            data ) :
        Animation(RunningTime),
        propagationTime((1.0-WaveDurationFraction)*RunningTime),
        waveDelay(WaveDurationFraction*RunningTime/WaveCount),
        splashDuration(WaveDurationFraction*RunningTime),
        from00(),
        from10(),
        from11(),
        toCenter(),
        to00(),
        to10(),
        to11(),
        data(data),
        trace(new GLPolylineTube(WaveStartThickness/2.0, 5)),
        traceMaterial( GLMaterial::Color(WaveAmbientColorRed,  WaveAmbientColorGreen,  WaveAmbientColorBlue),
                       GLMaterial::Color(WaveSpecularColorRed, WaveSpecularColorGreen, WaveSpecularColorBlue),
                       WaveShininess )
    {
        const GLMotif::Box fromBox = from->getExterior();
        const GLMotif::Box toBox   = to->getExterior();

        const GLMotif::WidgetManager::Transformation fromTransform = Vrui::getWidgetManager()->calcWidgetTransformation(from);
        const GLMotif::WidgetManager::Transformation toTransform   = Vrui::getWidgetManager()->calcWidgetTransformation(to);

        from00 = fromTransform.transform(GLMotifPointFromGLMotifVector(fromBox.getCorner(0)));  // (z, y, x) = (0, 0, 0)
        from10 = fromTransform.transform(GLMotifPointFromGLMotifVector(fromBox.getCorner(1)));  // (z, y, x) = (0, 0, 1)
        from11 = fromTransform.transform(GLMotifPointFromGLMotifVector(fromBox.getCorner(3)));  // (z, y, x) = (0, 1, 1)

        toCenter = toTransform.transform(lastClickPoint);

        to00 = toTransform.transform(GLMotifPointFromGLMotifVector(toBox.getCorner(0)));  // (z, y, x) = (0, 0, 0)
        to10 = toTransform.transform(GLMotifPointFromGLMotifVector(toBox.getCorner(1)));  // (z, y, x) = (0, 0, 1)
        to11 = toTransform.transform(GLMotifPointFromGLMotifVector(toBox.getCorner(3)));  // (z, y, x) = (0, 1, 1)

        trace->setNumTubeSegments(WaveTubeSegmentCount);
        trace->addVertex(GLPolylineTube::Point(0, 0, 0));
        trace->addVertex(GLPolylineTube::Point(0, 0, 0));
        trace->addVertex(GLPolylineTube::Point(0, 0, 0));
        trace->addVertex(GLPolylineTube::Point(0, 0, 0));
        trace->addVertex(GLPolylineTube::Point(0, 0, 0));
    }

    virtual ~DataRetrievalAnimationX();

    void showAt(Misc::Time t, GLContextData& contextData) const;

protected:
    const double     propagationTime;
    const double     waveDelay;
    const double     splashDuration;
    GLMotif::Point   from00;
    GLMotif::Point   from10;
    GLMotif::Point   from11;
    GLMotif::Point   toCenter;
    GLMotif::Point   to00;
    GLMotif::Point   to10;
    GLMotif::Point   to11;
    std::string      data;
    GLPolylineTube*  trace;
    const GLMaterial traceMaterial;
};



const double   DataRetrievalAnimationX::RunningTime            =  0.7;    // static member
const double   DataRetrievalAnimationX::WaveDurationFraction   =  0.5;    // static member
const unsigned DataRetrievalAnimationX::WaveCount              = 11;      // static member
const double   DataRetrievalAnimationX::WaveThicknessIncrement =  0.005;  // static member
const double   DataRetrievalAnimationX::WaveStartThickness     =  0.01;   // static member
const double   DataRetrievalAnimationX::WaveAmbientColorRed    =  1.0;    // static member
const double   DataRetrievalAnimationX::WaveAmbientColorGreen  =  1.0;    // static member
const double   DataRetrievalAnimationX::WaveAmbientColorBlue   =  0.5;    // static member
const double   DataRetrievalAnimationX::WaveSpecularColorRed   =  1.0;    // static member
const double   DataRetrievalAnimationX::WaveSpecularColorGreen =  1.0;    // static member
const double   DataRetrievalAnimationX::WaveSpecularColorBlue  =  1.0;    // static member
const double   DataRetrievalAnimationX::WaveShininess          = 50.0;    // static member
const unsigned DataRetrievalAnimationX::WaveTubeSegmentCount   = 12;      // static member



DataRetrievalAnimationX::~DataRetrievalAnimationX()
{
    if (trace)
        delete trace;
}



void DataRetrievalAnimationX::showAt(Misc::Time t, GLContextData& contextData) const
{
    for (unsigned i = 0; i < WaveCount; i++)
    {
        double lineThickness = WaveStartThickness + i*WaveThicknessIncrement;

        const double progress = Animation::timeToSeconds(t - t0) - i*waveDelay;

        if (progress >= 0.0)
        {
            glPushAttrib(GL_ENABLE_BIT|GL_LINE_BIT|GL_POLYGON_BIT|GL_LIGHTING_BIT);

            if (progress <= propagationTime)
            {
                glEnable(GL_LIGHTING);

                const double fraction = (propagationTime == 0.0) ? 1.0 : (progress / propagationTime);

                const GLMotif::Point p00 = from00 + fraction*(toCenter - from00);
                const GLMotif::Point p10 = from10 + fraction*(toCenter - from10);
                const GLMotif::Point p11 = from11 + fraction*(toCenter - from11);

                const GLMotif::Point p01 = p00 + (p11 - p10);

                trace->setTubeRadius(lineThickness/2.0);
                trace->setVertex(0, p00);
                trace->setVertex(1, p10);
                trace->setVertex(2, p11);
                trace->setVertex(3, p01);
                trace->setVertex(4, p00);

                glMaterial(GLMaterialEnums::FRONT, traceMaterial);
                trace->glRenderAction(contextData);
            }
            else
            {
                glDisable(GL_LIGHTING);

                const double splashProgress = progress - propagationTime;

                if (splashProgress <= splashDuration)
                {
                    const double fraction = (splashDuration == 0.0) ? 0.0 : (splashProgress / splashDuration);

                    const GLMotif::Point p00 = toCenter + fraction*(to00 - toCenter);
                    const GLMotif::Point p10 = toCenter + fraction*(to10 - toCenter);
                    const GLMotif::Point p11 = toCenter + fraction*(to11 - toCenter);

                    const GLMotif::Point p01 = p00 + (p11 - p10);

                    glPolygonMode(GL_FRONT, GL_LINE);
                    glLineWidth(2.0);
                    glBegin(GL_QUADS);
                        glColor3f(WaveAmbientColorRed, WaveAmbientColorGreen, WaveAmbientColorBlue);
                        glVertex(p00);
                        glVertex(p10);
                        glVertex(p11);
                        glVertex(p01);
                    glEnd();
                }
            }

            glPopAttrib();
        }
    }
}



//----------------------------------------------------------------------

class DataRetrievalAnimation : public VncTool::Animation
{
public:
    static const double RunningTime;
    static const double BeamThickness0;
    static const double BeamThickness1;
    static const double BeamAmbientColorRed0;
    static const double BeamAmbientColorGreen0;
    static const double BeamAmbientColorBlue0;
    static const double BeamAmbientColorAlpha0;
    static const double BeamSpecularColorRed0;
    static const double BeamSpecularColorGreen0;
    static const double BeamSpecularColorBlue0;
    static const double BeamSpecularColorAlpha0;
    static const double BeamShininess0;
    static const double BeamAmbientColorRed1;
    static const double BeamAmbientColorGreen1;
    static const double BeamAmbientColorBlue1;
    static const double BeamAmbientColorAlpha1;
    static const double BeamSpecularColorRed1;
    static const double BeamSpecularColorGreen1;
    static const double BeamSpecularColorBlue1;
    static const double BeamSpecularColorAlpha1;
    static const double BeamShininess1;
    static const double SplashThickness0;
    static const double SplashThickness1;
    static const double SplashColorRed0;
    static const double SplashColorGreen0;
    static const double SplashColorBlue0;
    static const double SplashColorRed1;
    static const double SplashColorGreen1;
    static const double SplashColorBlue1;

public:
    DataRetrievalAnimation( const GLMotif::Point&  lastClickPoint,
                            const GLMotif::Widget* from,
                            const GLMotif::Widget* to,
                            const char*            data ) :
        Animation(RunningTime),
        from00(),
        from10(),
        from11(),
        from01(),
        toCenter(),
        to00(),
        to10(),
        to11(),
        data(data)
    {
        const GLMotif::Box fromBox = from->getExterior();
        const GLMotif::Box toBox   = to->getExterior();

        const GLMotif::WidgetManager::Transformation fromTransform = Vrui::getWidgetManager()->calcWidgetTransformation(from);
        const GLMotif::WidgetManager::Transformation toTransform   = Vrui::getWidgetManager()->calcWidgetTransformation(to);

        from00 = fromTransform.transform(GLMotifPointFromGLMotifVector(fromBox.getCorner(0)));  // (z, y, x) = (0, 0, 0)
        from10 = fromTransform.transform(GLMotifPointFromGLMotifVector(fromBox.getCorner(1)));  // (z, y, x) = (0, 0, 1)
        from11 = fromTransform.transform(GLMotifPointFromGLMotifVector(fromBox.getCorner(3)));  // (z, y, x) = (0, 1, 1)

        from01 = from00 + (from11 - from10);

        toCenter = toTransform.transform(lastClickPoint);

        to00 = toTransform.transform(GLMotifPointFromGLMotifVector(toBox.getCorner(0)));  // (z, y, x) = (0, 0, 0)
        to10 = toTransform.transform(GLMotifPointFromGLMotifVector(toBox.getCorner(1)));  // (z, y, x) = (0, 0, 1)
        to11 = toTransform.transform(GLMotifPointFromGLMotifVector(toBox.getCorner(3)));  // (z, y, x) = (0, 1, 1)
    }

    void showAt(Misc::Time t, GLContextData& contextData) const;

protected:
    GLMotif::Point from00;
    GLMotif::Point from10;
    GLMotif::Point from11;
    GLMotif::Point from01;
    GLMotif::Point toCenter;
    GLMotif::Point to00;
    GLMotif::Point to10;
    GLMotif::Point to11;
    std::string    data;
};



const double DataRetrievalAnimation::RunningTime             =  0.7;  // static member
const double DataRetrievalAnimation::BeamThickness0          =  5.0;  // static member
const double DataRetrievalAnimation::BeamThickness1          =  1.0;  // static member
const double DataRetrievalAnimation::BeamAmbientColorRed0    =  1.0;  // static member
const double DataRetrievalAnimation::BeamAmbientColorGreen0  =  1.0;  // static member
const double DataRetrievalAnimation::BeamAmbientColorBlue0   =  0.5;  // static member
const double DataRetrievalAnimation::BeamAmbientColorAlpha0  =  0.5;  // static member
const double DataRetrievalAnimation::BeamSpecularColorRed0   =  1.0;  // static member
const double DataRetrievalAnimation::BeamSpecularColorGreen0 =  1.0;  // static member
const double DataRetrievalAnimation::BeamSpecularColorBlue0  =  1.0;  // static member
const double DataRetrievalAnimation::BeamSpecularColorAlpha0 =  0.5;  // static member
const double DataRetrievalAnimation::BeamShininess0          = 50.0;  // static member
const double DataRetrievalAnimation::BeamAmbientColorRed1    =  1.0;  // static member
const double DataRetrievalAnimation::BeamAmbientColorGreen1  =  1.0;  // static member
const double DataRetrievalAnimation::BeamAmbientColorBlue1   =  0.5;  // static member
const double DataRetrievalAnimation::BeamAmbientColorAlpha1  =  0.0;  // static member
const double DataRetrievalAnimation::BeamSpecularColorRed1   =  1.0;  // static member
const double DataRetrievalAnimation::BeamSpecularColorGreen1 =  1.0;  // static member
const double DataRetrievalAnimation::BeamSpecularColorBlue1  =  1.0;  // static member
const double DataRetrievalAnimation::BeamSpecularColorAlpha1 =  0.0;  // static member
const double DataRetrievalAnimation::BeamShininess1          = 50.0;  // static member
const double DataRetrievalAnimation::SplashThickness0        =  5.0;  // static member
const double DataRetrievalAnimation::SplashThickness1        =  1.0;  // static member
const double DataRetrievalAnimation::SplashColorRed0         =  1.0;  // static member
const double DataRetrievalAnimation::SplashColorGreen0       =  1.0;  // static member
const double DataRetrievalAnimation::SplashColorBlue0        =  0.5;  // static member
const double DataRetrievalAnimation::SplashColorRed1         =  1.0;  // static member
const double DataRetrievalAnimation::SplashColorGreen1       =  1.0;  // static member
const double DataRetrievalAnimation::SplashColorBlue1        =  0.5;  // static member



void DataRetrievalAnimation::showAt(Misc::Time t, GLContextData& contextData) const
{
    const double progress = Animation::timeToSeconds(t - t0);

    if ((0.0 <= progress) && (progress <= duration))
    {
        const double fraction = (duration == 0.0) ? 1.0 : (progress / duration);

        glPushAttrib(GL_ENABLE_BIT|GL_LINE_BIT|GL_POLYGON_BIT|GL_LIGHTING_BIT);

        {
            glDisable(GL_LIGHTING);

            const GLfloat beamAmbientColorRed    = BeamAmbientColorRed0    + fraction*(BeamAmbientColorRed1    - BeamAmbientColorRed0);
            const GLfloat beamAmbientColorGreen  = BeamAmbientColorGreen0  + fraction*(BeamAmbientColorGreen1  - BeamAmbientColorGreen0);
            const GLfloat beamAmbientColorBlue   = BeamAmbientColorBlue0   + fraction*(BeamAmbientColorBlue1   - BeamAmbientColorBlue0);
            const GLfloat beamAmbientColorAlpha  = BeamAmbientColorAlpha0  + fraction*(BeamAmbientColorAlpha1  - BeamAmbientColorAlpha0);
            const GLfloat beamSpecularColorRed   = BeamSpecularColorRed0   + fraction*(BeamSpecularColorRed1   - BeamSpecularColorRed0);
            const GLfloat beamSpecularColorGreen = BeamSpecularColorGreen0 + fraction*(BeamSpecularColorGreen1 - BeamSpecularColorGreen0);
            const GLfloat beamSpecularColorBlue  = BeamSpecularColorBlue0  + fraction*(BeamSpecularColorBlue1  - BeamSpecularColorBlue0);
            const GLfloat beamSpecularColorAlpha = BeamSpecularColorAlpha0 + fraction*(BeamSpecularColorAlpha1 - BeamSpecularColorAlpha0);
            const GLfloat beamShininess          = BeamShininess0          + fraction*(BeamShininess1          - BeamShininess0);

            const GLfloat beamThickness = BeamThickness0 + fraction*(BeamThickness1 - BeamThickness0);

            const GLMaterial beamMaterial( GLMaterial::Color(beamAmbientColorRed,  beamAmbientColorGreen,  beamAmbientColorBlue,  beamAmbientColorAlpha),
                                           GLMaterial::Color(beamSpecularColorRed, beamSpecularColorGreen, beamSpecularColorBlue, beamSpecularColorAlpha),
                                           beamShininess );

            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            glLineWidth(beamThickness);
            glColor3f(beamAmbientColorRed, beamAmbientColorGreen, beamAmbientColorBlue);

            glBegin(GL_TRIANGLE_FAN);
                glVertex(toCenter);
                glVertex(from00);
                glVertex(from10);
                glVertex(from11);
                glVertex(from01);
                glVertex(from00);
            glEnd();
        }

        {
            glDisable(GL_LIGHTING);

            const GLfloat splashThickness = SplashThickness0 + fraction*(SplashThickness1 - SplashThickness0);

            const GLfloat splashColorRed    = SplashColorRed0    + fraction*(SplashColorRed1    - SplashColorRed0);
            const GLfloat splashColorGreen  = SplashColorGreen0  + fraction*(SplashColorGreen1  - SplashColorGreen0);
            const GLfloat splashColorBlue   = SplashColorBlue0   + fraction*(SplashColorBlue1   - SplashColorBlue0);

            const GLMotif::Point p00 = toCenter + fraction*(to00 - toCenter);
            const GLMotif::Point p10 = toCenter + fraction*(to10 - toCenter);
            const GLMotif::Point p11 = toCenter + fraction*(to11 - toCenter);

            const GLMotif::Point p01 = p00 + (p11 - p10);

            glPolygonMode(GL_FRONT, GL_LINE);
            glLineWidth(splashThickness);
            glColor3f(splashColorRed, splashColorGreen, splashColorBlue);

            glBegin(GL_QUADS);
                glVertex(p00);
                glVertex(p10);
                glVertex(p11);
                glVertex(p01);
            glEnd();
        }

        glPopAttrib();
    }
}



//----------------------------------------------------------------------

class ErrorOccurrenceAnimation : public VncTool::Animation
{
public:
    static const double RunningTime;
    static const double StartColorRed;
    static const double StartColorGreen;
    static const double StartColorBlue;

public:
    ErrorOccurrenceAnimation(GLMotif::Widget& displayWidget) :
        Animation(RunningTime),
        displayWidget(displayWidget),
        startColor(GLMotif::Color(StartColorRed, StartColorGreen, StartColorBlue)),
        originalColor(displayWidget.getBackgroundColor())
    {
    }

    virtual ~ErrorOccurrenceAnimation();

    void showAt(Misc::Time t, GLContextData& contextData) const;

protected:
    GLMotif::Widget&     displayWidget;
    const GLMotif::Color startColor;
    const GLMotif::Color originalColor;
};



const double ErrorOccurrenceAnimation::RunningTime     = 3.0;  // static member
const double ErrorOccurrenceAnimation::StartColorRed   = 1.0;  // static member
const double ErrorOccurrenceAnimation::StartColorGreen = 0.0;  // static member
const double ErrorOccurrenceAnimation::StartColorBlue  = 0.0;  // static member



ErrorOccurrenceAnimation::~ErrorOccurrenceAnimation()
{
    displayWidget.setBackgroundColor(originalColor);
}



void ErrorOccurrenceAnimation::showAt(Misc::Time t, GLContextData& contextData) const
{
    const double fraction = (duration == 0.0) ? 1.0 : (Animation::timeToSeconds(t - t0) / duration);

    const GLMotif::Color newColor = GLMotif::Color( startColor[0] + fraction*(originalColor[0] - startColor[0]),
                                                    startColor[1] + fraction*(originalColor[1] - startColor[1]),
                                                    startColor[2] + fraction*(originalColor[2] - startColor[2])  );

    displayWidget.setBackgroundColor(newColor);
}



//----------------------------------------------------------------------

void VncTool::BeamedDataTagInputCompletionCallback::keyboardDialogDidComplete(KeyboardDialog& keyboardDialog, bool cancelled)
{
    UpperLeftCornerPreserver upperLeftCornerPreserver(popupWindow);

    if (!cancelled)
        fieldToUpdate.setLabel(keyboardDialog.getBuffer().c_str());

    keyboardDialog.getManager()->popdownWidget(&keyboardDialog);
    keyboardDialog.getManager()->deleteWidget(&keyboardDialog);
    if (vncTool->dataEntryKeyboardDialog == &keyboardDialog)
        vncTool->dataEntryKeyboardDialog = 0;
}



void VncTool::ManualHostnameEntryCompletionCallback::keyboardDialogDidComplete(KeyboardDialog& keyboardDialog, bool cancelled)
{
    UpperLeftCornerPreserver upperLeftCornerPreserver(popupWindow);

    if (cancelled)
        vncTool->clearHostSelectorButtons();
    else
    {
        const char* const hostName = keyboardDialog.getBuffer().c_str();
        vncTool->hostDescriptorForManualEntry.setDesktopHost(hostName);

        if (vncTool->hostSelector && vncTool->hostSelector->getFirstChild())
        {
            GLMotif::ToggleButton* const toggle = dynamic_cast<GLMotif::ToggleButton*>(vncTool->hostSelector->getFirstChild());
            toggle->setLabel(hostName);
        }

    }

    keyboardDialog.getManager()->popdownWidget(&keyboardDialog);
    keyboardDialog.getManager()->deleteWidget(&keyboardDialog);
    if (vncTool->dataEntryKeyboardDialog == &keyboardDialog)
        vncTool->dataEntryKeyboardDialog = 0;

    if (!cancelled)
        vncTool->startVncDialogWithHostDescriptor(&vncTool->hostDescriptorForManualEntry);
}



//----------------------------------------------------------------------

VncToolFactory* VncTool::factory = 0;  // static member



VncTool::VncTool(const Vrui::ToolFactory* factory, const Vrui::ToolInputAssignment& inputAssignment) :
    Vrui::UtilityTool(factory, inputAssignment),
    insideWidget(false),
    active(false),
    selectionRay(),
    hostDescriptor(0),
    hostDescriptorForManualEntry(static_cast<const VncToolFactory*>(factory)->getBlankHostDescriptor()),
    popupWindow(0),
    hostSelector(0),
    enableClickThroughToggle(0),
    enableBeamDataToggle(0),
    timestampBeamedDataToggle(0),
    autoBeamToggle(0),
    beamedDataTagField(0),
    lastSelectedDialogDisplay(0),
    lastSelectedDialog(0),
    messageLabel(0),
    vncDialog(0),
    manualHostnameEntryCompletionCallback(0),
    beamedDataTagInputCompletionCallback(0),
    dataEntryKeyboardDialog(0),
    animations()
{
    // Create the popup window with the controls in it:

    popupWindow = new GLMotif::PopupWindow("VncTool", Vrui::getWidgetManager(), "Vnc Tool");
    {
        GLMotif::PopupWindow* const parent = popupWindow;

        GLMotif::RowColumn* const controlsRowCol = new GLMotif::RowColumn("controlsRowCol", parent, false);
        {
            GLMotif::RowColumn* const parent = controlsRowCol;
            parent->setOrientation(GLMotif::RowColumn::HORIZONTAL);
            parent->setPacking(GLMotif::RowColumn::PACK_TIGHT);

            GLMotif::RowColumn* const hostarea = new GLMotif::RowColumn("hostarea", parent, false);
            {
                GLMotif::RowColumn* const parent = hostarea;

                hostSelector = new GLMotif::RadioBox("Hosts", parent, false);
                {
                    hostSelector->setBorderWidth(hostSelector->getStyleSheet()->textfieldBorderWidth);
                    hostSelector->setBorderType(GLMotif::Widget::LOWERED);
                    hostSelector->setPacking(GLMotif::RowColumn::PACK_GRID);

                    hostSelector->addToggle("");  // for blankHostDescriptor
                    const VncToolFactory::HostDescriptorList& hostDescriptors = VncTool::factory->getHostDescriptors();
                    for (VncToolFactory::HostDescriptorList::const_iterator it = hostDescriptors.begin(); it != hostDescriptors.end(); ++it)
                        hostSelector->addToggle(it->desktopHost);

                    // Reset the new toggle buttons' horizontal alignment to Left
                    for (GLMotif::Widget* child = hostSelector->getFirstChild(); child; child = hostSelector->getNextChild(child))
                    {
                        GLMotif::ToggleButton* const toggle = dynamic_cast<GLMotif::ToggleButton*>(child);
                        if (toggle)
                            toggle->setHAlignment(GLFont::Left);
                    }

                    clearHostSelectorButtons();  // sets standard text for zero-th (manual entry) toggle

                    hostSelector->getValueChangedCallbacks().add(this, &VncTool::changeHostCallback);
                }
                hostSelector->manageChild();

                new GLMotif::Blind("blind1", parent);
            }
            hostarea->manageChild();

            GLMotif::RowColumn* const infoBox = new GLMotif::RowColumn("infoBox", parent, false);
            {
                GLMotif::RowColumn* const parent = infoBox;
                parent->setPacking(GLMotif::RowColumn::PACK_TIGHT);

                enableClickThroughToggle = new GLMotif::ToggleButton("enableClickThroughToggle", parent, "Enable click-through");
                enableClickThroughToggle->getSelectCallbacks().add(this, &VncTool::enableClickThroughToggleCallback);

                enableBeamDataToggle = new GLMotif::ToggleButton("enableBeamDataToggle", parent, "Enable data beaming");

                timestampBeamedDataToggle = new GLMotif::ToggleButton("timestampBeamedDataToggle", parent, "Timestamp beamed data");

                GLMotif::RowColumn* const beamedDataTagFieldBox = new GLMotif::RowColumn("beamedDataTagFieldBox", parent, false);
                {
                    GLMotif::RowColumn* const parent = beamedDataTagFieldBox;
                    parent->setOrientation(GLMotif::RowColumn::HORIZONTAL);
                    parent->setPacking(GLMotif::RowColumn::PACK_TIGHT);

                    new GLMotif::Label("beamedDataTagFieldLabel", parent, "Beamed data tag:");

                    beamedDataTagField = new GLMotif::Label("beamedDataTagFieldLabel", parent, "");

                    (new GLMotif::Button("changeBeamedDataTagFieldButton", parent, "Change"))->getSelectCallbacks().add(this, &VncTool::changeBeamedDataTagCallback);
                }
                beamedDataTagFieldBox->manageChild();

                new GLMotif::Blind("blind2", parent);

                GLMotif::RowColumn* const messageLabelBox = new GLMotif::RowColumn("messageLabelBox", parent, false);
                {
                    GLMotif::RowColumn* const parent = messageLabelBox;
                    parent->setOrientation(GLMotif::RowColumn::HORIZONTAL);
                    parent->setPacking(GLMotif::RowColumn::PACK_TIGHT);

                    new GLMotif::Label("messageLabelLabel", parent, "Status:");

                    messageLabel = new GLMotif::Label("messageLabel", parent, "Ready", false);
                    messageLabel->setHAlignment(GLFont::Left);
                    messageLabel->manageChild();
                }
                messageLabelBox->manageChild();

                autoBeamToggle = new GLMotif::ToggleButton("autoBeamToggle", parent, "Auto-beam");
                autoBeamToggle->manageChild();

                lastSelectedDialogDisplay = new GLMotif::Label("lastSelectedDialogDisplay", parent, "", false);
                lastSelectedDialogDisplay->setHAlignment(GLFont::Left);
                lastSelectedDialogDisplay->manageChild();

                new GLMotif::Blind("blind2", parent);
            }
            infoBox->manageChild();

            new GLMotif::Blind("blind3", parent);
        }
        controlsRowCol->manageChild();
    }
    Vrui::popupPrimaryWidget(popupWindow, Vrui::getNavigationTransformation().transform(Vrui::getDisplayCenter()));
}



VncTool::~VncTool()
{
    for (AnimationList::iterator it = animations.begin(); it != animations.end(); ++it)
        delete *it;
    animations.clear();

    hostSelector              = 0;
    enableClickThroughToggle  = 0;
    enableBeamDataToggle      = 0;
    timestampBeamedDataToggle = 0;
    autoBeamToggle            = 0;
    beamedDataTagField        = 0;
    lastSelectedDialogDisplay = 0;
    lastSelectedDialog        = 0;
    messageLabel              = 0;
    closePopupWindow(popupWindow);

    closePopupWindow(dataEntryKeyboardDialog);

    if (manualHostnameEntryCompletionCallback)
    {
        delete manualHostnameEntryCompletionCallback;
        manualHostnameEntryCompletionCallback = 0;
    }

    if (beamedDataTagInputCompletionCallback)
    {
        delete beamedDataTagInputCompletionCallback;
        beamedDataTagInputCompletionCallback = 0;
    }

    resetConnection();
}



const Vrui::ToolFactory* VncTool::getFactory() const
{
    return factory;
}



void VncTool::resetConnection()
{
    closePopupWindow(vncDialog);

    if (enableBeamDataToggle)
        enableBeamDataToggle->setToggle(false);

    hostDescriptor = 0;
}



void VncTool::changeHostCallback(GLMotif::RadioBox::ValueChangedCallbackData* cbData)
{
    if (cbData)
    {
        if (!cbData->newSelectedToggle)
        {
            resetConnection();
            clearHostSelectorButtons();
        }
        else
        {
            // Note: We rely on the fact that the host descriptors list in VncToolFactory does not ever change....
            // See also the note in VncToolFactory::VncToolFactory().
            const int toggleIndex = cbData->radioBox->getToggleIndex(cbData->newSelectedToggle);
            if (toggleIndex <= 0)
            {
                // The zero-th (manual entry) selection

                resetConnection();

                closePopupWindow(dataEntryKeyboardDialog);
                if (manualHostnameEntryCompletionCallback)
                {
                    delete manualHostnameEntryCompletionCallback;
                    manualHostnameEntryCompletionCallback = 0;
                }

                manualHostnameEntryCompletionCallback = new ManualHostnameEntryCompletionCallback(this, popupWindow);
                dataEntryKeyboardDialog               = new KeyboardDialog("ManualEntryHostnameInputDialog", Vrui::getWidgetManager(), "Enter hostname:", hostDescriptorForManualEntry.desktopHost);
                Vrui::popupPrimaryWidget(dataEntryKeyboardDialog, Vrui::getNavigationTransformation().transform(Vrui::getDisplayCenter()));

                dataEntryKeyboardDialog->activate(*manualHostnameEntryCompletionCallback);
            }
            else
            {
                startVncDialogWithHostDescriptor(&static_cast<const VncToolFactory*>(getFactory())->getHostDescriptors().at(toggleIndex-1));
            }
        }
    }
}



void VncTool::startVncDialogWithHostDescriptor(const VncToolFactory::HostDescriptor* newHostDescriptor)
{
    resetConnection();

    hostDescriptor = newHostDescriptor;
    if (hostDescriptor)
    {
        // Reset the controls for a new VncDialog instance:
        if (enableBeamDataToggle)      enableBeamDataToggle->setToggle(false);
        if (autoBeamToggle)            autoBeamToggle->setToggle(hostDescriptor->initialAutoBeam);
        if (enableClickThroughToggle)  enableClickThroughToggle->setToggle(hostDescriptor->initialEnableClickThrough);
        if (timestampBeamedDataToggle) timestampBeamedDataToggle->setToggle(hostDescriptor->initialTimestampBeamedData);
        if (beamedDataTagField)        beamedDataTagField->setLabel(hostDescriptor->initialBeamedDataTag.c_str());

        // Start up a new VncDialog instance:
        vncDialog = new VncDialog( "VncDialog",
                                   Vrui::getWidgetManager(),
                                   hostDescriptor->desktopHost,
                                   0 /* no password specified */,
                                   hostDescriptor->rfbPort,
                                   hostDescriptor->initViaConnect,
                                   hostDescriptor->requestedEncodings,
                                   hostDescriptor->sharedDesktopFlag,
                                   (enableClickThroughToggle && enableClickThroughToggle->getToggle()) );

        vncDialog->addCloseButtonCallback(this, &VncTool::vncDialogCloseButtonCallback);
    }
}



void VncTool::enableClickThroughToggleCallback(GLMotif::Button::CallbackData* cbData)
{
    if (vncDialog && enableClickThroughToggle)
        vncDialog->setEnableClickThrough(enableClickThroughToggle->getToggle());
}



void VncTool::changeBeamedDataTagCallback(GLMotif::Button::CallbackData* cbData)
{
    closePopupWindow(dataEntryKeyboardDialog);
    if (beamedDataTagInputCompletionCallback)
    {
        delete beamedDataTagInputCompletionCallback;
        beamedDataTagInputCompletionCallback = 0;
    }

    if (beamedDataTagField)
    {
        beamedDataTagInputCompletionCallback = new BeamedDataTagInputCompletionCallback(this, popupWindow, *beamedDataTagField);
        dataEntryKeyboardDialog              = new KeyboardDialog("BeamedDataTagInputDialog", Vrui::getWidgetManager(), "Enter New Tag For Beamed Data:");
        Vrui::popupPrimaryWidget(dataEntryKeyboardDialog, Vrui::getNavigationTransformation().transform(Vrui::getDisplayCenter()));

        dataEntryKeyboardDialog->activate(*beamedDataTagInputCompletionCallback);
    }
}



void VncTool::buttonCallback(int deviceIndex, int buttonIndex, Vrui::InputDevice::ButtonCallbackData* cbData)
{
    if (cbData->newButtonState)  // button has just been pressed
    {
        selectionRay = calcSelectionRay();

        // If the widget manager accepts the event, preempt any cascaded tools until the button is released:
        GLMotif::Event event(true);
        event.setWorldLocation(selectionRay);

        if (Vrui::getWidgetManager()->pointerButtonDown(event))
        {
            // Activate this tool:
            active = true;

            if (enableBeamDataToggle && enableBeamDataToggle->getToggle() && hostDescriptor && vncDialog)
            {
                GLMotif::Widget* const target =
                    (autoBeamToggle && autoBeamToggle->getToggle() && lastSelectedDialog && Vrui::getWidgetManager()->isVisible(lastSelectedDialog))
                        ? lastSelectedDialog
                        : event.getTargetWidget();

                if (target)
                {
                    GLMotif::Widget* const targetRoot = target->getRoot();

                    if (targetRoot)
                    {
                        GLMotif::Widget* const vncDialogRoot = vncDialog->getRootWidget();

                        if (targetRoot != vncDialogRoot)  // don't beam from the vncDialog's container to the vncDialog
                        {
                            const char* const beamedDataTagStringFromField = beamedDataTagField ? beamedDataTagField->getLabel() : 0;
                            const char* const beamedDataTagString =
                                (beamedDataTagStringFromField && (strlen(beamedDataTagStringFromField) > 0))
                                    ? beamedDataTagStringFromField
                                    : 0;

                            lastSelectedDialog = targetRoot;
                            const char* targetTitle = getTitleFromTitleBarOfRootContaining(lastSelectedDialog);
                            if (!targetTitle)
                                targetTitle = "(unknown dialog)";

                            WidgetDataRetrievalFunctor retrievalFunctor( hostDescriptor->beginDataString.c_str(),
                                                                         hostDescriptor->interDatumString.c_str(),
                                                                         hostDescriptor->endDataString.c_str(),
                                                                         ((timestampBeamedDataToggle && timestampBeamedDataToggle->getToggle()) ? "%Y-%m-%d %H:%M:%S" : 0),
                                                                         beamedDataTagString,
                                                                         targetTitle );

                            traverseWidgetTree(targetRoot, retrievalFunctor);
                            retrievalFunctor.complete();

                            if (retrievalFunctor.anyDataSeen)
                            {
                                const char* const resultCStr = retrievalFunctor.result.c_str();

                                animations.push_back(new DataRetrievalAnimation(vncDialog->getLastClickPoint(), target->getRoot(), vncDialog->getVncWidget(), resultCStr));

                                if (!vncDialog->getRfbIsSameMachine())
                                    vncDialog->sendCStringViaKeyEvents(resultCStr);

                                {
                                    UpperLeftCornerPreserver upperLeftCornerPreserver(popupWindow);

                                    // Note: lastSelectedDialogDisplay->setLabel() (below) must be called after initiating
                                    // the animation because this call triggers a resize which screws up the transforms,
                                    // presumably until the next refresh (?).

                                    std::string lastSelectedDialogTitle = "Last selected dialog: ";
                                    lastSelectedDialogTitle += targetTitle;

                                    if (lastSelectedDialogDisplay)
                                        lastSelectedDialogDisplay->setLabel(lastSelectedDialogTitle.c_str());
                                }
                            }
                        }
                    }
                }
            }

            // Cancel processing of this callback to preempt cascaded tools:
            cbData->callbackList->requestInterrupt();
        }
    }
    else  // button has just been released
    {
        if (active)
        {
            selectionRay = calcSelectionRay();

            // Deliver the event:
            GLMotif::Event event(false);
            event.setWorldLocation(selectionRay);

            Vrui::getWidgetManager()->pointerButtonUp(event);

            // Deactivate this tool:
            active = false;

            // Cancel processing of this callback to preempt cascaded tools:
            cbData->callbackList->requestInterrupt();
        }
    }
}



GLMotif::Ray VncTool::calcSelectionRay() const
{
    // Get pointer to input device:
    Vrui::InputDevice* device = input.getDevice(0);

    // Calculate ray equation:
    Vrui::Point  start     = device->getPosition();
    Vrui::Vector direction = device->getRayDirection();

    return GLMotif::Ray(start, direction);
}



void VncTool::updateUIState()
{
    if (vncDialog)
        vncDialog->checkForUpdates();

    if (messageLabel && vncDialog)
    {
        const char* const message = vncDialog->getMessageString();
        messageLabel->setLabel(message ? message : "");
    }

    for (AnimationList::iterator it = animations.begin(); it != animations.end(); )
        if (!(*it)->expired())
            ++it;
        else
        {
            delete *it;
            it = animations.erase(it);
        }
}



void VncTool::clearHostSelectorButtons() const
{
    if (hostSelector)
        for (GLMotif::Widget* child = hostSelector->getFirstChild(); child; child = hostSelector->getNextChild(child))
        {
            GLMotif::ToggleButton* const toggle = dynamic_cast<GLMotif::ToggleButton*>(child);
            if (toggle)
            {
                if (child == hostSelector->getFirstChild())
                    toggle->setLabel("(Manual Entry)");

                toggle->setToggle(false);
            }
        }

    if (messageLabel)
        messageLabel->setLabel("Ready");
}



void VncTool::frame()
{
    updateUIState();

    // Update the selection ray:
    selectionRay = calcSelectionRay();
    insideWidget = (Vrui::getWidgetManager()->findPrimaryWidget(selectionRay) != 0);

    if (active)
    {
        // Deliver a motion event:
        GLMotif::Event event(true);
        event.setWorldLocation(selectionRay);
        Vrui::getWidgetManager()->pointerMotion(event);
    }
}



void VncTool::display(GLContextData& contextData) const
{
    for (AnimationList::const_iterator it = animations.begin(); it != animations.end(); ++it)
        (*it)->show(contextData);

    if (insideWidget || active)
    {
        // Draw the menu selection ray:
        glPushAttrib(GL_ENABLE_BIT|GL_LINE_BIT);
        glDisable(GL_LIGHTING);
        glColor3f(1.0f, 0.0f, 0.0f);
        glLineWidth(3.0f);
        glBegin(GL_LINES);
        glVertex(selectionRay.getOrigin());
        glVertex(selectionRay(Vrui::getDisplaySize()*Vrui::Scalar(5)));
        glEnd();
        glPopAttrib();
    }
}



void VncTool::vncDialogCloseButtonCallback(GLMotif::Button::CallbackData* cbData)
{
    resetConnection();
    clearHostSelectorButtons();
}



template<class PopupWindowClass>
    void VncTool::closePopupWindow(PopupWindowClass*& var)
{
    if (var)
    {
        var->getManager()->popdownWidget(var);
        var->getManager()->deleteWidget(var);
        var = 0;
    }
}

}  // end of namespace Voltaic
