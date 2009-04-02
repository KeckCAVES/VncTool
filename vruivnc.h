/***********************************************************************
  vruivnc - Simple Vrui application to connect to a VNC server.
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

#ifndef VRUIVNC_INCLUDED
#define VRUIVNC_INCLUDED

#include <deque>
#include <GL/gl.h>
#include <GL/GLMaterial.h>
#include <GL/GLObject.h>
#include <Threads/Thread.h>
#include <Threads/Mutex.h>
#include <Comm/MulticastPipe.h>
#include <Vrui/Application.h>

#include "VncManager.h"



namespace Voltaic {

    class vruivnc :
        public Vrui::Application,
        public GLObject,
        public VncManager::MessageManager,
        public VncManager::PasswordRetrievalThunk
    {
    public:
        vruivnc(int& argc, char**& argv, char**& appDefaults);
        virtual ~vruivnc();

    public:
        virtual void initContext(GLContextData& contextData) const;
        virtual void frame();
        virtual void display(GLContextData& contextData) const;
        virtual void centerDisplayCallback(class Misc::CallbackData* cbData);
        virtual void drawRemoteDisplaySurface() const;

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
        VncManager* const                  vncManager;
        bool                               scaleToEnvironment;      // flag if the model should be scaled to fit the environment
        GLMaterial                         surfaceMaterial;         // OpenGL material properties for the display surface

    private:
        // Disable these copiers:
        vruivnc& operator=(const vruivnc&);
        vruivnc(const vruivnc&);
    };

}  // end of namespace Voltaic

#endif  // VRUIVNC_INCLUDED
