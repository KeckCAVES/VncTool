/***********************************************************************
TestVncWidget - Simple Vrui application to test VncWidget.
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

#ifndef TESTVNCWIDGET_INCLUDED
#define TESTVNCWIDGET_INCLUDED

#include <Vrui/Application.h>
#include <GL/GLObject.h>
#include <GLMotif/PopupWindow.h>

#include "VncWidget.h"



namespace Voltaic {

    class TestVncWidget :
        public Vrui::Application,
        public GLObject,
        public VncManager::MessageManager,
        public VncManager::PasswordRetrievalThunk
    {
    public:
        TestVncWidget(int& argc, char**& argv, char**& appDefaults);
        virtual ~TestVncWidget();

    public:
        virtual void initContext(GLContextData& contextData) const;
        virtual void frame();
        virtual void display(GLContextData& contextData) const;
        virtual void centerDisplayCallback(class Misc::CallbackData* cbData);

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

        // VncManager::PasswordRetrievalThunk method:
        virtual void getPassword(VncManager::PasswordRetrievalCompletionThunk& passwordRetrievalCompletionThunk);

    protected:
        VncManager::RFBProtocolStartupData rfbProtocolStartupData;
        bool                               scaleToEnvironment;
        GLMotif::PopupWindow*              popupWindow;
        VncWidget*                         vncWidget;

    private:
        // Disable these copiers:
        TestVncWidget& operator=(const TestVncWidget&);
        TestVncWidget(const TestVncWidget&);
    };

}  // end of namespace Voltaic

#endif  // TESTVNCWIDGET_INCLUDED
