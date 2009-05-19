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

#ifndef VNCWIDGET_INCLUDED
#define VNCWIDGET_INCLUDED

#include <deque>
#include <string.h>
#include <Vrui/Vrui.h>
#include <GLMotif/Widget.h>
#include <GL/GLObject.h>
#include <GL/GLContextData.h>
#include <GLMotif/Event.h>
#include <Images/RGBImage.h>
#include <Threads/Thread.h>
#include <Threads/Mutex.h>
#include <Comm/MulticastPipe.h>

#include "VncManager.h"



namespace Voltaic {

    class VncWidget :
        public GLMotif::Widget,
        public GLObject
    {
    public:
        VncWidget( VncManager::MessageManager&         messageManager,
                   VncManager::PasswordRetrievalThunk& passwordRetrievalThunk,
                   bool                                isSlave,
                   Comm::MulticastPipe*                clusterMulticastPipe,
                   bool                                trackDisplaySize,
                   const char*                         sName,
                   GLMotif::Container*                 sParent,
                   bool                                sManageChild = true );

        virtual ~VncWidget();

    public:
        // Overrides for GLMotif::Widget methods:

        virtual void initContext(GLContextData& contextData) const;
        virtual void draw(GLContextData& contextData) const;

        // calcNaturalSize() overrides GLMotif::Widget::calcNaturalSize().
        // Its behavior is controlled by the trackDisplaySize, displayWidthMultiplier,
        // displayHeightMultiplier and configuredInteriorSize values as follows:
        //
        // If trackDisplaySize:
        //     * interior size of the widget is set to:
        //           ( getRemoteDisplayWidth()*displayWidthMultiplier,
        //             getRemoteDisplayHeight()*displayHeightMultiplier,
        //             0.0 )
        //     * size changes on the remote host's display cause a size
        //       update to this widget.
        //
        // If !trackDisplaySize:
        //     * interior size of the widget is set according to configuredInteriorSize.
        //
        // calcNaturalSize() returns the exterior size, i.e., the
        // interior size expanded by the borders, etc for this widget.
        virtual GLMotif::Vector calcNaturalSize() const;

        virtual void pointerButtonDown(GLMotif::Event& event);
        virtual void pointerButtonUp(GLMotif::Event& event);
        virtual void pointerMotion(GLMotif::Event& event);

    protected:
        virtual void handlePointerEvent(GLMotif::Event& event, int buttonMask);

    public:
        bool         getEnableClickThrough() const { return enableClickThrough; }
        virtual void setEnableClickThrough(bool value);  // also calls attemptSizeUpdate()

        bool         getTrackDisplaySize() const { return trackDisplaySize; }
        virtual void setTrackDisplaySize(bool value);  // also calls attemptSizeUpdate()

        GLfloat      getDisplayWidthMultiplier()  const { return displayWidthMultiplier;  }
        GLfloat      getDisplayHeightMultiplier() const { return displayHeightMultiplier; }
        void         setDisplayWidthMultiplier(GLfloat value)  { setDisplaySizeMultipliers(value, displayHeightMultiplier); }
        void         setDisplayHeightMultiplier(GLfloat value) { setDisplaySizeMultipliers(displayWidthMultiplier, value); }
        virtual void setDisplaySizeMultipliers(GLfloat widthValue, GLfloat heightValue);  // also calls attemptSizeUpdate()

        const GLMotif::Vector& getConfiguredInteriorSize() const { return configuredInteriorSize; }
        void                   setConfiguredInteriorSize(const GLMotif::Vector& sizeValue) { setConfiguredInteriorSize(sizeValue, trackDisplaySize); }
        virtual void           setConfiguredInteriorSize(const GLMotif::Vector& sizeValue, bool trackDisplaySizeValue);  // also calls attemptSizeUpdate()

        // attemptSizeUpdate() tries to set the size of the widget using the
        // interior size returned from calcNaturalSize().  If this widget is
        // managed, the size update is requested via parent->requestResize(),
        // otherwise the resize is performed directly.
        virtual void attemptSizeUpdate();

    public:
        // checkForUpdates() must be called periodically to make sure that updates
        // from the remote desktop are posted to the remoteDisplay object.
        // true is returned iff something changed (which may be display size or
        // simply content of the display).  Also, if trackDisplaySize is true,
        // then attemptSizeUpdate() is called if the display size changed.
        virtual bool checkForUpdates();

        // These methods return the current screen size of the remote host.
        // They both return 0 before the remote host is connected.
        GLsizei getRemoteDisplayWidth()  const { return (vncManager == 0) ? 0 : vncManager->getRemoteDisplayWidth();  }
        GLsizei getRemoteDisplayHeight() const { return (vncManager == 0) ? 0 : vncManager->getRemoteDisplayHeight(); }

        // The send* methods are pass-throughs to the rfbProto object in the vncManager object.
        // They return false (always) if Vrui::isMaster() is false, i.e., they are usable
        // only from the master node.
        virtual bool sendKeyEvent(rfbCARD32 key, bool down);
        virtual bool sendPointerEvent(int x, int y, int buttonMask);
        virtual bool sendClientCutText(const char* str, size_t len);

        virtual bool sendStringViaKeyEvents( const char* str,
                                             size_t      len,
                                             rfbCARD32   tabKeySym         = 0xff09,
                                             rfbCARD32   enterKeySym       = 0xff0d,
                                             rfbCARD32   leftControlKeySym = 0xffe3 );

        bool sendCStringViaKeyEvents( const char* cstr,
                                      rfbCARD32   tabKeySym         = 0xff09,
                                      rfbCARD32   enterKeySym       = 0xff0d,
                                      rfbCARD32   leftControlKeySym = 0xffe3 )
        {
            return !cstr || sendCStringViaKeyEvents(cstr, tabKeySym, enterKeySym, leftControlKeySym);
        }

        // The startup and shutdown  methods are pass-throughs to the vncManager object.
        virtual void startup(const VncManager::RFBProtocolStartupData& rfbProtocolStartupData);
        virtual void shutdown();

    public:
        bool                      getRfbIsOpen()             const { return !vncManager ? false                        : vncManager->getRfbIsOpen(); }
        bool                      getRfbIsSameMachine()      const { return !vncManager ? false                        : vncManager->getRfbIsSameMachine(); }
        const char*               getRfbDesktopHost()        const { return !vncManager ? 0                            : vncManager->getRfbDesktopHost(); }  // 0 if connected by listening
        const struct sockaddr_in* getRfbDesktopIPAddress()   const { return !vncManager ? (const struct sockaddr_in*)0 : vncManager->getRfbDesktopIPAddress(); }
        unsigned                  getRfbPort()               const { return !vncManager ? 0                            : vncManager->getRfbRfbPort(); }
        unsigned                  getRfbPortOffset()         const { return !vncManager ? 0                            : vncManager->getRfbPortOffset(); }
        unsigned                  getRfbTcpPort()            const { return !vncManager ? 0                            : vncManager->getRfbTcpPort(); }
        const rfbPixelFormat*     getRfbPixelFormat()        const { return !vncManager ? (const rfbPixelFormat*)0     : vncManager->getRfbPixelFormat(); }
        bool                      getRfbShouldMapColor()     const { return !vncManager ? false                        : vncManager->getRfbShouldMapColor(); }
        const char*               getRfbRequestedEncodings() const { return !vncManager ? 0                            : vncManager->getRfbRequestedEncodings(); }
        const char*               getRfbDesktopName()        const { return !vncManager ? 0                            : vncManager->getRfbDesktopName(); }

        bool                      getRfbSockConnected()      const { return !vncManager ? false                        : vncManager->getRfbSockConnected(); }
        const rfbServerInitMsg*   getRfbServerInitMsg()      const { return !vncManager ? (const rfbServerInitMsg*)0   : vncManager->getRfbServerInitMsg(); }
        rfbCARD8                  getRfbCurrentEncoding()    const { return !vncManager ? (rfbCARD8)rfbEncodingRaw     : vncManager->getRfbCurrentEncoding(); }
        bool                      getRfbIsBigEndian()        const { return !vncManager ? false                        : vncManager->getRfbIsBigEndian(); }

        const GLMotif::Point&     getLastClickPoint()        const { return lastClickPoint; }

    public:
        static const GLfloat DefaultDisplayWidthMultiplier;   // = 1.0
        static const GLfloat DefaultDisplayHeightMultiplier;  // = 1.0
        static const GLfloat DefaultConfiguredInteriorSizeX;  // = 640.0
        static const GLfloat DefaultConfiguredInteriorSizeY;  // = 480.0
        static const GLfloat DefaultConfiguredInteriorSizeZ;  // = 0.0

    protected:
        VncManager* const vncManager;
        bool              enableClickThrough;       // defaults to true
        bool              trackDisplaySize;         // defaults to false
        GLfloat           displayWidthMultiplier;   // defaults to DefaultDisplayWidthMultiplier
        GLfloat           displayHeightMultiplier;  // defaults to DefaultDisplayHeightMultiplier
        GLMotif::Vector   configuredInteriorSize;   // defaults to (DefaultConfiguredInteriorSizeX, DefaultConfiguredInteriorSizeY, DefaultConfiguredInteriorSizeZ)
        GLMotif::Point    lastClickPoint;           // point on widget last clicked through to remote computer

    private:
        // Disable these copiers:
        VncWidget& operator=(const VncWidget&);
        VncWidget(const VncWidget&);
    };

}  // end of namespace Voltaic

#endif  // VNCWIDGET_INCLUDED
