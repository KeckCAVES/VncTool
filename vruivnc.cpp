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

#include <stdlib.h>
#include <iostream>
#include <deque>
#include <GL/GLMaterial.h>
#include <GL/GLContextData.h>
#include <Images/RGBImage.h>
#include <Vrui/Vrui.h>
#include <Vrui/Application.h>
#include <Geometry/OrthogonalTransformation.h>

#include "vruivnc.h"



namespace Voltaic {

//----------------------------------------------------------------------
// vruivnc methods

vruivnc::vruivnc(int& argc, char**& argv, char**& appDefaults) :
    Vrui::Application(argc, argv, appDefaults),
    rfbProtocolStartupData(),
    vncManager(new VncManager(*this, *this, !Vrui::isMaster(), Vrui::openPipe())),
    scaleToEnvironment(true),
    surfaceMaterial(GLMaterial::Color(1.0f, 1.0f, 1.0f, 0.333f), GLMaterial::Color(0.333f, 0.333f, 0.333f), 10.0f)
{
    // Parse the command line:
    for (int i = 1; i < argc; ++i)
    {
        if (argv[i][0] == '-')
        {
            if (strcasecmp(argv[i]+1, "host") == 0)
            {
                i++;
                rfbProtocolStartupData.desktopHost    = argv[i++];
                rfbProtocolStartupData.initViaConnect = true;
            }
            else if (strcasecmp(argv[i]+1, "port") == 0)
            {
                i++;
                rfbProtocolStartupData.rfbPort = atoi(argv[i++]);
            }
            else if (strcasecmp(argv[i]+1, "encodings") == 0)
            {
                i++;
                rfbProtocolStartupData.requestedEncodings = argv[i++];
            }
            else if (strcasecmp(argv[i]+1, "shared") == 0)
            {
                i++;
                rfbProtocolStartupData.sharedDesktopFlag = true;
            }
            else
            {
                std::cout << "Unrecognized switch " << argv[i] << std::endl;
            }
        }
    }

    // Initialize Vrui navigation transformation:
    centerDisplayCallback(0);
}



vruivnc::~vruivnc()
{
    if (vncManager)
    {
        vncManager->shutdown();
        delete vncManager;
    }
}



void vruivnc::initContext(GLContextData& contextData) const
{
    if (vncManager)
        vncManager->startup(rfbProtocolStartupData);
}



void vruivnc::frame()
{
    (void)vncManager->performQueuedActions();

    Vrui::requestUpdate();
}



void vruivnc::display(GLContextData& contextData) const
{
    // Save OpenGL state:
    glPushAttrib(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_LIGHTING_BIT|GL_POLYGON_BIT);

    // Save coordinate system:
    glPushMatrix();

    // Render all opaque surfaces:
    glDisable(GL_CULL_FACE);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);

    // Set up OpenGL to render the surface:
    glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL, GL_SEPARATE_SPECULAR_COLOR);
    glMaterial(GLMaterialEnums::FRONT_AND_BACK, surfaceMaterial);

    // Display the graphics:
    drawRemoteDisplaySurface();

    // Go back to original coordinate system:
    glPopMatrix();

    // Restore OpenGL state:
    glPopAttrib();
}



void vruivnc::centerDisplayCallback(Misc::CallbackData* cbData)
{
    static const float dist = 400;

    if (scaleToEnvironment)
    {
        // Center the model in the available display space:
        Vrui::setNavigationTransformation(Vrui::Point::origin, Vrui::Scalar(3.0*dist));
    }
    else
    {
        // Center the model in the available display space, but do not scale it:
        Vrui::NavTransform nav = Vrui::NavTransform::identity;
        nav *= Vrui::NavTransform::translateFromOriginTo(Vrui::getDisplayCenter());
        nav *= Vrui::NavTransform::scale(Vrui::Scalar(8)*Vrui::getInchFactor() / Vrui::Scalar(dist));
        Vrui::setNavigationTransformation(nav);
    }
}



void vruivnc::drawRemoteDisplaySurface() const
{
    static const float depth      = 10.0;
    static const float bezelWidth =  3.0;

    const float dw0 = vncManager->getRemoteDisplayWidth()  / 2.0;
    const float dh0 = vncManager->getRemoteDisplayHeight() / 2.0;
    const float dd0 = depth                                / 2.0;

    const float dw1 = dw0 + bezelWidth;
    const float dh1 = dh0 + bezelWidth;
    const float dd1 = dd0 + bezelWidth;

    const float sqrtHalf = sqrt(0.5);

    glBegin(GL_QUADS);
        glColor3f(0.0, 0.0, 1.0);

        // Bottom:
        glNormal3f(0, 0, 1);
        glVertex3f(-dw1, -dh1, -dd1);
        glVertex3f( dw1, -dh1, -dd1);
        glVertex3f( dw1,  dh1, -dd1);
        glVertex3f(-dw1,  dh1, -dd1);

        // Sides:
        glNormal3f(-1, 0, 0);
        glVertex3f(-dw1, -dh1, -dd1);
        glVertex3f(-dw1, -dh1,  dd0);
        glVertex3f( dw1, -dh1,  dd0);
        glVertex3f( dw1, -dh1, -dd1);

        glNormal3f(0, -1, 0);
        glVertex3f(-dw1, -dh1, -dd1);
        glVertex3f(-dw1, -dh1,  dd0);
        glVertex3f(-dw1,  dh1,  dd0);
        glVertex3f(-dw1,  dh1, -dd1);

        glNormal3f(-1, 0, 0);
        glVertex3f(-dw1,  dh1, -dd1);
        glVertex3f(-dw1,  dh1,  dd0);
        glVertex3f( dw1,  dh1,  dd0);
        glVertex3f( dw1,  dh1, -dd1);

        glNormal3f(0, -1, 0);
        glVertex3f( dw1, -dh1, -dd1);
        glVertex3f( dw1, -dh1,  dd0);
        glVertex3f( dw1,  dh1,  dd0);
        glVertex3f( dw1,  dh1, -dd1);

        // Bezel:
        glNormal3f(-sqrtHalf, 0, sqrtHalf);
        glVertex3f(-dw1, -dh1,  dd0);
        glVertex3f(-dw0, -dh0,  dd1);
        glVertex3f( dw0, -dh0,  dd1);
        glVertex3f( dw1, -dh1,  dd0);

        glNormal3f(0, -sqrtHalf, sqrtHalf);
        glVertex3f(-dw1, -dh1,  dd0);
        glVertex3f(-dw0, -dh0,  dd1);
        glVertex3f(-dw0,  dh0,  dd1);
        glVertex3f(-dw1,  dh1,  dd0);

        glNormal3f(-sqrtHalf, 0, sqrtHalf);
        glVertex3f(-dw1,  dh1,  dd0);
        glVertex3f(-dw0,  dh0,  dd1);
        glVertex3f( dw0,  dh0,  dd1);
        glVertex3f( dw1,  dh1,  dd0);

        glNormal3f(0, -sqrtHalf, sqrtHalf);
        glVertex3f( dw1, -dh1,  dd0);
        glVertex3f( dw0, -dh0,  dd1);
        glVertex3f( dw0,  dh0,  dd1);
        glVertex3f( dw1,  dh1,  dd0);
    glEnd();

    // Top face: the video surface:
    (void)vncManager->drawRemoteDisplaySurface( -dw0, -dh0, dd1,
                                                 dw0, -dh0, dd1,
                                                 dw0,  dh0, dd1 );
}



//----------------------------------------------------------------------
// VncManager::MessageManager methods

void vruivnc::internalErrorMessage(const char* where, const char* message)
{
    fprintf(stderr, "*** vruivnc: internal error at %s: %s", where, message);
    perror("; errno");  // prints newline at end of line
}



void vruivnc::errorMessage(const char* where, const char* message)
{
    fprintf(stderr, "*** vruivnc: %s: %s", where, message);
    perror("; errno");  // prints newline at end of line
}



void vruivnc::errorMessageFromServer(const char* where, const char* message)
{
    // Note: a malicious message from the server won't hurt us here, i.e., there
    // are no known injection attacks for the way we're handling this message....

    this->errorMessage(where, message);
}



void vruivnc::infoServerInitStarted()
{
    fprintf(stderr, "vruivnc info: RFB protocol: server init started\n");
}



void vruivnc::infoProtocolVersion(int serverMajorVersion, int serverMinorVersion, int clientMajorVersion, int clientMinorVersion)
{
    fprintf(stderr, "vruivnc info: RFB protocol version: server:%03d.%03d, client: %03d.%03d\n", (int)serverMajorVersion, (int)serverMinorVersion, (int)clientMajorVersion, (int)clientMinorVersion);
}



void vruivnc::infoAuthenticationResult(bool succeeded, rfbCARD32 authScheme, rfbCARD32 authResult)
{
    fprintf(stderr, "vruivnc info: RFB protocol: authentication result: succeeded = %d, authScheme = %ld, authResult = %ld\n", (int)succeeded, (long)authScheme, (long)authResult);
}



void vruivnc::infoServerInitCompleted(bool succeeded)
{
    fprintf(stderr, "vruivnc info: RFB protocol: server init completed: succeeded = %d\n", (int)succeeded);
}



void vruivnc::infoCloseStarted()
{
    fprintf(stderr, "vruivnc info: RFB protocol: close started\n");
}



void vruivnc::infoCloseCompleted()
{
    fprintf(stderr, "vruivnc info: RFB protocol: close completed\n");
}



//----------------------------------------------------------------------
// VncManager::PasswordRetrievalThunk method

void vruivnc::getPassword(VncManager::PasswordRetrievalCompletionThunk& passwordRetrievalCompletionThunk)
{
    passwordRetrievalCompletionThunk.postPassword(getpass("Enter VNC password:"));
}

}  // end of namespace Voltaic



//----------------------------------------------------------------------

int main(int argc, char* argv[])
{
    try
    {
        // Create the Vrui application object:
        char** appDefaults = 0;
        Voltaic::vruivnc app(argc, argv, appDefaults);

        // Run the Vrui application:
        app.run();

        // Return to the OS:
        return 0;
    }
    catch(std::runtime_error err)
    {
        // Print an error message and return to the OS:
        std::cerr << "Exception: " << err.what() << std::endl;
        return 1;
    }
}
