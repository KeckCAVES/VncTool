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

#ifndef __VNCVISLET_H_INCLUDED__
#define __VNCVISLET_H_INCLUDED__

#include <string>
#include <vector>
#include <list>
#include <Geometry/OrthogonalTransformation.h>
#include <GLMotif/PopupWindow.h>
#include <GLMotif/Button.h>
#include <GLMotif/Label.h>
#include <Vrui/Geometry.h>
#include <Vrui/Vislet.h>
#include <Vrui/VisletManager.h>

#include "VncWidget.h"
#include "KeyboardDialog.h"



namespace Voltaic {

    //----------------------------------------------------------------------
    class VncVisletFactory : public Vrui::VisletFactory
    {
    friend class VncVislet;

    public:
        VncVisletFactory(Vrui::VisletManager& visletManager);
        virtual ~VncVisletFactory();

        virtual Vrui::Vislet* createVislet(int numVisletArguments, const char* const visletArguments[]) const;
        virtual void destroyVislet(Vrui::Vislet* vislet) const;

    private:
        // Disable these copiers:
        VncVisletFactory& operator=(const VncVisletFactory&);
        VncVisletFactory(const VncVisletFactory&);
    };



    //----------------------------------------------------------------------
    class VncVislet :
        public Vrui::Vislet,
        public GLObject,
        public VncManager::MessageManager,
        public VncManager::PasswordRetrievalThunk
    {
    friend class VncVisletFactory;

    protected:
        friend class PasswordDialogCompletionCallback;
        class PasswordDialogCompletionCallback : public KeyboardDialog::CompletionCallback
        {
        public:
            PasswordDialogCompletionCallback( VncVislet*                                    vncVislet,
                                              VncManager::PasswordRetrievalCompletionThunk& passwordRetrievalCompletionThunk ) :
                vncVislet(vncVislet),
                passwordRetrievalCompletionThunk(passwordRetrievalCompletionThunk)
            {
            }

        public:
            virtual void keyboardDialogDidComplete(KeyboardDialog& keyboardDialog, bool cancelled);

        protected:
            VncVislet* const                              vncVislet;
            VncManager::PasswordRetrievalCompletionThunk& passwordRetrievalCompletionThunk;
        };

    public:
        VncVislet(int numArguments, const char* const arguments[]);
        virtual ~VncVislet();

    protected:
        virtual Vrui::VisletFactory* getFactory() const;

    public:
        virtual void disable();
        virtual void enable();
        virtual void initContext(GLContextData& contextData) const;
        virtual void frame();
        virtual void display(GLContextData& contextData) const;

    public:
        // VncManager::MessageManager methods:
        virtual void internalErrorMessage(const char* where, const char* message);
        virtual void errorMessage(const char* where, const char* message);
        virtual void errorMessageFromServer(const char* where, const char* message);
        virtual void infoServerInitStarted();
        virtual void infoProtocolVersion(int serverMajorVersion, int serverMinorVersion, int clientMajorVersion, int clientMinorVersion);
        virtual void infoAuthenticationResult(bool succeeded, rfbCARD32 authScheme, rfbCARD32 authResult);
        virtual void infoServerInitCompleted(bool succeeded);
        virtual void infoCloseStarted();
        virtual void infoCloseCompleted();

    public:
        // VncManager::PasswordRetrievalThunk method:
        virtual void getPassword(VncManager::PasswordRetrievalCompletionThunk& passwordRetrievalCompletionThunk);

    public:
        VncWidget*       getVncWidget()  const { return vncWidget; }
        GLMotif::Widget* getRootWidget() const { return !vncWidget ? 0 : vncWidget->getRoot(); }

        GLsizei getRemoteDisplayWidth()  const { return !vncWidget ? 0 : vncWidget->getRemoteDisplayWidth();  }
        GLsizei getRemoteDisplayHeight() const { return !vncWidget ? 0 : vncWidget->getRemoteDisplayHeight(); }

        bool sendKeyEvent(rfbCARD32 key, bool down)         { return !vncWidget ? false : vncWidget->sendKeyEvent(key, down); }
        bool sendPointerEvent(int x, int y, int buttonMask) { return !vncWidget ? false : vncWidget->sendPointerEvent(x, y, buttonMask); }
        bool sendClientCutText(const char* str, size_t len) { return !vncWidget ? false : vncWidget->sendClientCutText(str, len); }

        bool sendStringViaKeyEvents( const char* str,
                                     size_t      len,
                                     rfbCARD32   tabKeySym         = 0xff09,
                                     rfbCARD32   enterKeySym       = 0xff0d,
                                     rfbCARD32   leftControlKeySym = 0xffe3 )
        {
            return (vncWidget != 0) && vncWidget->sendStringViaKeyEvents(str, len, tabKeySym, enterKeySym, leftControlKeySym);
        }

        bool sendCStringViaKeyEvents( const char* cstr,
                                      rfbCARD32   tabKeySym         = 0xff09,
                                      rfbCARD32   enterKeySym       = 0xff0d,
                                      rfbCARD32   leftControlKeySym = 0xffe3 )
        {
            return (vncWidget != 0) && vncWidget->sendCStringViaKeyEvents(cstr, tabKeySym, enterKeySym, leftControlKeySym);
        }

        bool                      getRfbIsOpen()             const { return !vncWidget ? false                        : vncWidget->getRfbIsOpen(); }
        bool                      getRfbIsSameMachine()      const { return !vncWidget ? false                        : vncWidget->getRfbIsSameMachine(); }
        const char*               getRfbDesktopHost()        const { return !vncWidget ? 0                            : vncWidget->getRfbDesktopHost(); }  // 0 if connected by listening
        const struct sockaddr_in* getRfbDesktopIPAddress()   const { return !vncWidget ? (const struct sockaddr_in*)0 : vncWidget->getRfbDesktopIPAddress(); }
        unsigned                  getRfbPort()               const { return !vncWidget ? 0                            : vncWidget->getRfbPort(); }
        unsigned                  getRfbPortOffset()         const { return !vncWidget ? 0                            : vncWidget->getRfbPortOffset(); }
        unsigned                  getRfbTcpPort()            const { return !vncWidget ? 0                            : vncWidget->getRfbTcpPort(); }
        const rfbPixelFormat*     getRfbPixelFormat()        const { return !vncWidget ? (const rfbPixelFormat*)0     : vncWidget->getRfbPixelFormat(); }
        bool                      getRfbShouldMapColor()     const { return !vncWidget ? false                        : vncWidget->getRfbShouldMapColor(); }
        const char*               getRfbRequestedEncodings() const { return !vncWidget ? 0                            : vncWidget->getRfbRequestedEncodings(); }
        const char*               getRfbDesktopName()        const { return !vncWidget ? 0                            : vncWidget->getRfbDesktopName(); }

        bool                      getRfbSockConnected()      const { return !vncWidget ? false                        : vncWidget->getRfbSockConnected(); }
        const rfbServerInitMsg*   getRfbServerInitMsg()      const { return !vncWidget ? (const rfbServerInitMsg*)0   : vncWidget->getRfbServerInitMsg(); }
        rfbCARD8                  getRfbCurrentEncoding()    const { return !vncWidget ? (rfbCARD8)rfbEncodingRaw     : vncWidget->getRfbCurrentEncoding(); }
        bool                      getRfbIsBigEndian()        const { return !vncWidget ? false                        : vncWidget->getRfbIsBigEndian(); }

        const GLMotif::Point&     getLastClickPoint()        const { return !vncWidget ? GLMotif::Point::origin       : vncWidget->getLastClickPoint(); }

        bool getEnableClickThrough() const     { return !vncWidget ? false : vncWidget->getEnableClickThrough(); }
        void setEnableClickThrough(bool value) { if (vncWidget) vncWidget->setEnableClickThrough(value); }

        const char* getMessageString() const { return messageLabel ? messageLabel->getLabel() : ""; }

        bool getCloseCompleted() const { return closeCompleted; }

    private:
        static VncVisletFactory* factory;  // pointer to the factory object for this class

    protected:
        virtual void closeButtonCallback(GLMotif::Button::CallbackData* cbData);
        void parseArguments(int numArguments, const char* const arguments[]);
        template<class PopupWindowClass>
            void closePopupWindow(PopupWindowClass*& var);
        virtual void closeAllPopupWindows();
        virtual void updateUIState();
        virtual void clearPasswordDialog();
        virtual void resetConnection();

    protected:
        bool                              closeCompleted;
        bool                              initViaConnect;
        std::string                       hostname;
        unsigned                          rfbPort;
        std::string                       requestedEncodings;
        bool                              sharedDesktopFlag;
        bool                              enableClickThrough;
        bool                              initializedWithPassword;
        std::string                       password;  // the password from the initialization arguments if initializedWithPassword is true
        GLMotif::PopupWindow*             popupWindow;
        PasswordDialogCompletionCallback* passwordCompletionCallback;
        KeyboardDialog*                   passwordKeyboardDialog;
        VncWidget*                        vncWidget;
        GLMotif::Button*                  closeButton;
        GLMotif::Label*                   messageLabel;

    private:
        // Disable these copiers:
        VncVislet& operator=(const VncVislet&);
        VncVislet(const VncVislet&);
    };

}  // namespace Voltaic

#endif  // #ifndef __VNCVISLET_H_INCLUDED__
