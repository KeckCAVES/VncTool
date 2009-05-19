/***********************************************************************
  VncWidget - Vrui Widget which connects to and displays a VNC server.
  Copyright (c) 2007,2008 Voltaic

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2 of the License, or (at your
  option) any later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
***********************************************************************/

#include <GLMotif/Container.h>

#include "VncWidget.h"



namespace Voltaic {

//----------------------------------------------------------------------
// VncWidget methods

VncWidget::VncWidget( VncManager::MessageManager&         messageManager,
                      VncManager::PasswordRetrievalThunk& passwordRetrievalThunk,
                      bool                                isSlave,
                      Comm::MulticastPipe*                clusterMulticastPipe,
                      bool                                trackDisplaySize,
                      const char*                         sName,
                      GLMotif::Container*                 sParent,
                      bool                                sManageChild ) :
    GLMotif::Widget(sName, sParent, false),
    GLObject(),
    vncManager(new VncManager(messageManager, passwordRetrievalThunk, isSlave, clusterMulticastPipe)),
    enableClickThrough(true),
    trackDisplaySize(trackDisplaySize),
    displayWidthMultiplier(DefaultDisplayWidthMultiplier),
    displayHeightMultiplier(DefaultDisplayHeightMultiplier),
    configuredInteriorSize(DefaultConfiguredInteriorSizeX, DefaultConfiguredInteriorSizeY, DefaultConfiguredInteriorSizeZ),
    lastClickPoint()
{
    if (sManageChild)
        manageChild();  // this has not been safe to do during construction...
}



VncWidget::~VncWidget()
{
    if (vncManager)
    {
        vncManager->shutdown();
        delete vncManager;
    }
}



void VncWidget::initContext(GLContextData& contextData) const
{
    // nothing...
}



void VncWidget::draw(GLContextData& contextData) const
{
    Widget::draw(contextData);

    if (vncManager)
    {
        const GLMotif::Box    bounds = getInterior();
        const GLMotif::Vector c0     = bounds.getCorner(0);  // (z, y, x) = (0, 0, 0)
        const GLMotif::Vector c1     = bounds.getCorner(1);  // (z, y, x) = (0, 0, 1)
        const GLMotif::Vector c3     = bounds.getCorner(3);  // (z, y, x) = (0, 1, 1)
        vncManager->drawRemoteDisplaySurface( c0[0], c0[1], c0[2],
                                              c1[0], c1[1], c1[2],
                                              c3[0], c3[1], c3[2]  );
    }
}



GLMotif::Vector VncWidget::calcNaturalSize() const
{
    GLMotif::Vector interior;

    if (trackDisplaySize)
    {
        interior = GLMotif::Vector( getRemoteDisplayWidth()  * displayWidthMultiplier,
                                    getRemoteDisplayHeight() * displayHeightMultiplier,
                                    0.0 );
    }
    else
    {
        interior = configuredInteriorSize;
    }

    return calcExteriorSize(interior);
}



void VncWidget::pointerButtonDown(GLMotif::Event& event)
{
    handlePointerEvent(event, 1);//!!! left mouse button always reported...
}



void VncWidget::pointerButtonUp(GLMotif::Event& event)
{
    handlePointerEvent(event, 0);//!!! should remember any other (chorded) buttons...
}



void VncWidget::pointerMotion(GLMotif::Event& event)
{
    handlePointerEvent(event, 1);//!!! left mouse button always reported...
}



void VncWidget::handlePointerEvent(GLMotif::Event& event, int buttonMask)
{
    if (enableClickThrough && vncManager && !vncManager->getRfbIsSameMachine())
    {
        const GLMotif::Box interior  = getInterior();
        const GLfloat      interiorW = interior.size[0];
        const GLfloat      interiorH = interior.size[1];

        if ((interiorW > 0) && (interiorH > 0))
        {
            const GLfloat interiorMinX = interior.origin[0];
            const GLfloat interiorMaxX = interiorMinX + interiorW;
            const GLfloat interiorMinY = interior.origin[1];
            const GLfloat interiorMaxY = interiorMinY + interiorH;

            const GLMotif::Point point = event.getWidgetPoint().getPoint();

            if ((interiorMinX <= point[0]) && (point[0] <= interiorMaxX) && (interiorMinY <= point[1]) && (point[1] <= interiorMaxY))
            {
                const GLfloat px = point[0] - interiorMinX;
                const GLfloat py = interiorH - (point[1] - interiorMinY);  // Note: sendPointerEvent() wants y to increase downward...

                const GLfloat x = vncManager->getRemoteDisplayWidth()  * (px / interiorW);
                const GLfloat y = vncManager->getRemoteDisplayHeight() * (py / interiorH);

                lastClickPoint = point;
                (void)sendPointerEvent((int)(x+0.5), (int)(y+0.5), buttonMask);
            }
        }
    }
}



void VncWidget::setEnableClickThrough(bool value)
{
    enableClickThrough = value;
}



void VncWidget::setTrackDisplaySize(bool value)
{
    trackDisplaySize = value;

    attemptSizeUpdate();
}



void VncWidget::setDisplaySizeMultipliers(GLfloat widthValue, GLfloat heightValue)
{
    displayWidthMultiplier  = widthValue;
    displayHeightMultiplier = heightValue;

    attemptSizeUpdate();
}



void VncWidget::setConfiguredInteriorSize( const GLMotif::Vector& sizeValue,
                                           bool                   trackDisplaySizeValue )
{

    configuredInteriorSize = sizeValue;
    configuredInteriorSize[2] = 0.0;  // z size value is always 0

    trackDisplaySize = trackDisplaySizeValue;

    attemptSizeUpdate();
}



void VncWidget::attemptSizeUpdate()
{
    const GLMotif::Vector newExteriorSize = calcNaturalSize();

    if (parent && isManaged)
        parent->requestResize(this, newExteriorSize);
    else
    {
        // resize directly, preserving upper-left corner of widget
       resize(GLMotif::Box(getExterior().origin, newExteriorSize));
    }
}



bool VncWidget::checkForUpdates()
{
    bool updated = false;

    if (vncManager)
    {
        const GLsizei oldWidth  = getRemoteDisplayWidth();
        const GLsizei oldHeight = getRemoteDisplayHeight();

        if (vncManager->performQueuedActions())
            updated = true;

        if ( (getRemoteDisplayWidth()  != oldWidth) ||
             (getRemoteDisplayHeight() != oldHeight)   )
        {
            updated = true;

            if (trackDisplaySize)
                attemptSizeUpdate();
        }
    }

    return updated;
}



bool VncWidget::sendKeyEvent(rfbCARD32 key, bool down)
{
    return (vncManager != 0) && vncManager->sendKeyEvent(key, down);
}



bool VncWidget::sendPointerEvent(int x, int y, int buttonMask)
{
    return (vncManager != 0) && vncManager->sendPointerEvent(x, y, buttonMask);
}



bool VncWidget::sendClientCutText(const char* str, size_t len)
{
    return (vncManager != 0) && vncManager->sendClientCutText(str, len);
}



void VncWidget::startup(const VncManager::RFBProtocolStartupData& rfbProtocolStartupData)
{
    if (vncManager)
        vncManager->startup(rfbProtocolStartupData);
}



void VncWidget::shutdown()
{
    if (vncManager)
        vncManager->shutdown();
}



const GLfloat VncWidget::DefaultDisplayWidthMultiplier  =   1.0;  // static member
const GLfloat VncWidget::DefaultDisplayHeightMultiplier =   1.0;  // static member
const GLfloat VncWidget::DefaultConfiguredInteriorSizeX = 640.0;  // static member
const GLfloat VncWidget::DefaultConfiguredInteriorSizeY = 480.0;  // static member
const GLfloat VncWidget::DefaultConfiguredInteriorSizeZ =   0.0;  // static member; z size value is always 0

}  // end of namespace Voltaic
