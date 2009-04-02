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

#include <stdlib.h>
#include <iostream>
#include <Geometry/OrthogonalTransformation.h>
#include <GLMotif/RowColumn.h>
#include <GLMotif/StyleSheet.h>

#include "TestVncWidget.h"



namespace Voltaic {

//----------------------------------------------------------------------

TestVncWidget::TestVncWidget(int& argc, char**& argv, char**& appDefaults) :
    Vrui::Application(argc, argv, appDefaults),
    GLObject(),
    VncManager::MessageManager(),
    VncManager::PasswordRetrievalThunk(),
    rfbProtocolStartupData(),
    scaleToEnvironment(true),
    popupWindow(0),
    vncWidget(0)
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

    // Create the popup window with the VncWidget in it
    popupWindow = new GLMotif::PopupWindow("VncWidgetTestWindow", Vrui::getWidgetManager(), "VncWidget Test");
    GLMotif::RowColumn* const dialog = new GLMotif::RowColumn("dialog", popupWindow, false);

    vncWidget = new VncWidget(*this, *this, !Vrui::isMaster(), Vrui::openPipe(), true, "VncWidget", dialog, false);  // Note: vncWidget is owned by (a descendent of) popupWindow
    vncWidget->setBorderType(GLMotif::Widget::RAISED);
    const GLfloat uiSize = vncWidget->getStyleSheet()->size;
    vncWidget->setDisplaySizeMultipliers(uiSize/10.0, uiSize/10.0);

    vncWidget->manageChild();

    dialog->manageChild();
    Vrui::popupPrimaryWidget(popupWindow, Vrui::getNavigationTransformation().transform(Vrui::getDisplayCenter()));
}



TestVncWidget::~TestVncWidget()
{
    // Note: vncWidget is owned by (a descendent of) popupWindow

    if (popupWindow)
    {
        Vrui::popdownPrimaryWidget(popupWindow);
        delete popupWindow;
    }
}



void TestVncWidget::initContext(GLContextData& contextData) const
{
    if (vncWidget)
        vncWidget->startup(rfbProtocolStartupData);
}



void TestVncWidget::frame()
{
    GLMotif::Box exteriorPriorToUpdates;
    if (popupWindow)
        exteriorPriorToUpdates = popupWindow->getExterior();

    if (vncWidget)
        (void)vncWidget->checkForUpdates();

    // Preserve position of upper-left corner of dialog:
    if (popupWindow)
    {
        const GLfloat priorUpperLeftYPos = exteriorPriorToUpdates.origin[1] + exteriorPriorToUpdates.size[1];
        GLMotif::Box exterior = popupWindow->getExterior();
        exterior.origin[1] = priorUpperLeftYPos - exterior.size[1];
        popupWindow->resize(exterior);
    }

    Vrui::requestUpdate();
}



void TestVncWidget::display(GLContextData& contextData) const
{
    // nothing...
}



void TestVncWidget::centerDisplayCallback(Misc::CallbackData* cbData)
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



//----------------------------------------------------------------------
// VncManager::MessageManager methods

void TestVncWidget::internalErrorMessage(const char* where, const char* message)
{
    fprintf(stderr, "*** TestVncWidget: internal error at %s: %s", where, message);
    perror("; errno");
}



void TestVncWidget::errorMessage(const char* where, const char* message)
{
    fprintf(stderr, "*** TestVncWidget: %s: %s", where, message);
    perror("; errno");
}



void TestVncWidget::errorMessageFromServer(const char* where, const char* message)
{
    // Note: a malicious message from the server won't hurt us here, i.e., there
    // are no known injection attacks for the way we're handling this message....

    this->errorMessage(where, message);
}



void TestVncWidget::infoServerInitStarted()
{
    fprintf(stderr, "TestVncWidget info: RFB protocol: server init started\n");
}



void TestVncWidget::infoProtocolVersion(int serverMajorVersion, int serverMinorVersion, int clientMajorVersion, int clientMinorVersion)
{
    fprintf(stderr, "TestVncWidget info: RFB protocol version: server:%03d.%03d, client: %03d.%03d\n", (int)serverMajorVersion, (int)serverMinorVersion, (int)clientMajorVersion, (int)clientMinorVersion);
}



void TestVncWidget::infoAuthenticationResult(bool succeeded, rfbCARD32 authScheme, rfbCARD32 authResult)
{
    fprintf(stderr, "TestVncWidget info: RFB protocol: authentication result: succeeded = %d, authScheme = %ld, authResult = %ld\n", (int)succeeded, (long)authScheme, (long)authResult);
}



void TestVncWidget::infoServerInitCompleted(bool succeeded)
{
    fprintf(stderr, "TestVncWidget info: RFB protocol: server init completed: succeeded = %d\n", (int)succeeded);
}



void TestVncWidget::infoCloseStarted()
{
    fprintf(stderr, "TestVncWidget info: RFB protocol: close started\n");
}



void TestVncWidget::infoCloseCompleted()
{
    fprintf(stderr, "TestVncWidget info: RFB protocol: close completed\n");
}



//----------------------------------------------------------------------
// VncManager::PasswordRetrievalThunk method

void TestVncWidget::getPassword(VncManager::PasswordRetrievalCompletionThunk& passwordRetrievalCompletionThunk)
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
        Voltaic::TestVncWidget app(argc, argv, appDefaults);

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
