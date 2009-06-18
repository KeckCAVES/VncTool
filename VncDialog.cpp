/***********************************************************************
  VncDialog - Class for dialogs that can interact with remote VNC servers.
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

#include "VncDialog.h"

#include <GLMotif/WidgetManager.h>
#include <GLMotif/StyleSheet.h>
#include <GLMotif/RowColumn.h>
#include <GLMotif/Blind.h>
#include <Vrui/Vrui.h>
#include <ctype.h>



namespace Voltaic {

//----------------------------------------------------------------------

class UpperLeftCornerPreserver
{
public:
    UpperLeftCornerPreserver(VncDialog* vncDialog) :
        vncDialog(vncDialog)
    {
        if (vncDialog)
        {
            const GLMotif::Box priorExterior = vncDialog->getExterior();
            priorUpperLeft[0] = priorExterior.origin[0];
            priorUpperLeft[1] = (priorExterior.origin[1] + priorExterior.size[1]);
            priorUpperLeft[2] = 0.0;
            Vrui::getWidgetManager()->calcWidgetTransformation(vncDialog).transform(priorUpperLeft);  // convert to world coordinates
        }
    }

    ~UpperLeftCornerPreserver()
    {
        if (vncDialog)
        {
            const GLMotif::WidgetManager::Transformation xform = Vrui::getWidgetManager()->calcWidgetTransformation(vncDialog);

            GLMotif::Box         exterior = vncDialog->getExterior();
            const GLMotif::Point newUpperLeft(exterior.origin[0], (exterior.origin[1] + exterior.size[1]), 0.0);
            xform.transform(newUpperLeft);  // convert to world coordinates

            if (newUpperLeft[1] != priorUpperLeft[1])
            {
                xform.inverseTransform(priorUpperLeft);  // convert back to widget coordinates
                exterior.origin[1] = priorUpperLeft[1] - exterior.size[1];
                vncDialog->resize(exterior);
            }
        }
    }

private:
    VncDialog* const vncDialog;
    GLMotif::Point   priorUpperLeft;
};



//----------------------------------------------------------------------

void VncDialog::PasswordDialogCompletionCallback::keyboardDialogDidComplete(KeyboardDialog& keyboardDialog, bool cancelled)
{
    std::string retrievedPassword;
    if (!cancelled)
        retrievedPassword = keyboardDialog.getBuffer();

    keyboardDialog.getManager()->popdownWidget(&keyboardDialog);
    keyboardDialog.getManager()->deleteWidget(&keyboardDialog);
    if (vncDialog->passwordKeyboardDialog == &keyboardDialog)
        vncDialog->passwordKeyboardDialog = 0;

    if (vncDialog)
    {
        if (retrievedPassword.size() <= 0)
            vncDialog->infoServerInitCompleted(false);  // simulate failed connection
        else
        {
            // Must be performed last before returning:
            passwordRetrievalCompletionThunk.postPassword(retrievedPassword.c_str());
        }
    }

    // The following is OK to do before returning...
    VncManager::eraseStringContents(retrievedPassword);
}



//----------------------------------------------------------------------

VncDialog::VncDialog( const char*             sName,
                      GLMotif::WidgetManager* sManager,
                      const char*             hostname,
                      const char*             password,
                      unsigned                rfbPort,
                      bool                    initViaConnect,
                      const char*             requestedEncodings,
                      bool                    sharedDesktopFlag,
                      bool                    enableClickThrough ) :
    GLMotif::PopupWindow(sName, sManager, ""),
    VncManager::MessageManager(),
    VncManager::PasswordRetrievalThunk(),
    serverInitFailed(false),
    initViaConnect(initViaConnect),
    hostname(hostname ? hostname : ""),
    rfbPort(rfbPort),
    requestedEncodings(requestedEncodings ? requestedEncodings : ""),
    sharedDesktopFlag(sharedDesktopFlag),
    enableClickThrough(enableClickThrough),
    initializedWithPassword(password != 0),
    password(password ? password : ""),
    passwordCompletionCallback(0),
    passwordKeyboardDialog(0),
    vncWidget(0),
    closeButton(0),
    messageLabel(0)
{
    // Create the popup window with the VncWidget and controls in it:

    std::string title = "Connection to: ";
    title += (hostname ? hostname : "(unknown)");
    this->setTitleString(title.c_str());
    {
        GLMotif::RowColumn* const topRowCol = new GLMotif::RowColumn("topRowCol", this, false);
        {
            topRowCol->setOrientation(GLMotif::RowColumn::VERTICAL);
            topRowCol->setPacking(GLMotif::RowColumn::PACK_TIGHT);

            GLMotif::RowColumn* const controlsRowCol = new GLMotif::RowColumn("controlsRowCol", topRowCol, false);
            {
                controlsRowCol->setOrientation(GLMotif::RowColumn::HORIZONTAL);
                controlsRowCol->setPacking(GLMotif::RowColumn::PACK_TIGHT);

                closeButton  = new GLMotif::Button("CloseButton", controlsRowCol, "Close");
                messageLabel = new GLMotif::Label("Messages", controlsRowCol, "Ready");
                new GLMotif::Blind("Blind1", controlsRowCol);

                closeButton->getSelectCallbacks().add(this, &VncDialog::closeButtonCallback);
            }
            controlsRowCol->manageChild();

            vncWidget = new VncWidget(*this, *this, !Vrui::isMaster(), Vrui::openPipe(), true, "VncWidget", topRowCol, false);
            vncWidget->setBorderType(GLMotif::Widget::RAISED);
            const GLfloat uiSize = vncWidget->getStyleSheet()->size;
            vncWidget->setDisplaySizeMultipliers(uiSize/10.0, uiSize/10.0);
            vncWidget->manageChild();
        }
        topRowCol->manageChild();
    }
    Vrui::popupPrimaryWidget(this, Vrui::getNavigationTransformation().transform(Vrui::getDisplayCenter()));

    if (vncWidget)
    {
        vncWidget->setEnableClickThrough(enableClickThrough);

        VncManager::RFBProtocolStartupData rfbProtocolStartupData;
        rfbProtocolStartupData.initViaConnect     = this->initViaConnect;
        rfbProtocolStartupData.desktopHost        = this->hostname.c_str();
        rfbProtocolStartupData.rfbPort            = this->rfbPort;
        rfbProtocolStartupData.requestedEncodings = this->requestedEncodings.c_str();
        rfbProtocolStartupData.sharedDesktopFlag  = this->sharedDesktopFlag;
        vncWidget->startup(rfbProtocolStartupData);
    }
}



VncDialog::~VncDialog()
{
    close();
}



void VncDialog::close()
{
    this->getManager()->popdownWidget(this);
    resetConnection();
}



bool VncDialog::checkForUpdates()
{
    bool updated = false;

    if (vncWidget)
    {
        UpperLeftCornerPreserver upperLeftCornerPreserver(this);
        updated = vncWidget->checkForUpdates();
    }

    return updated;
}



//----------------------------------------------------------------------
// VncManager::MessageManager methods

void VncDialog::internalErrorMessage(const char* where, const char* message)
{
    if (Vrui::isMaster())
        fprintf(stderr, "** VncDialog: internal error at %s: %s\n", where, message);
}



void VncDialog::errorMessage(const char* where, const char* message)
{
    // When changing the implementation of this method, think
    // about possible injection attacks from a malicious server.
    // If any are possible, reimplement errorMessageFromServer()
    // below to prevent exploitation.

    messageLabel->setLabel("Connection error");
fprintf(stderr, "!!! VncDialog::errorMessage: %s: %s\n", where, message);//!!!
}



void VncDialog::errorMessageFromServer(const char* where, const char* message)
{
    // Note: a malicious message from the server won't hurt us here, i.e., there
    // are no known injection attacks for the way we're handling this message....

    this->errorMessage(where, message);
}



void VncDialog::infoServerInitStarted()
{
    messageLabel->setLabel(initViaConnect ? "Connecting..." : "Waiting for connection...");
}



void VncDialog::infoProtocolVersion(int serverMajorVersion, int serverMinorVersion, int clientMajorVersion, int clientMinorVersion)
{
    // do nothing...
}



void VncDialog::infoAuthenticationResult(bool succeeded, rfbCARD32 authScheme, rfbCARD32 authResult)
{
    // do nothing...
}



void VncDialog::infoServerInitCompleted(bool succeeded)
{
    serverInitFailed = !succeeded;
    messageLabel->setLabel(succeeded ? "Connected" : "Connection failed");
}



void VncDialog::infoDesktopSizeReceived(rfbCARD16 newWidth, rfbCARD16 newHeight)
{
    // do nothing...
}



void VncDialog::infoCloseStarted()
{
    // do nothing...
}



void VncDialog::infoCloseCompleted()
{
    // do nothing...
}



//----------------------------------------------------------------------
// VncManager::PasswordRetrievalThunk method

void VncDialog::getPassword(VncManager::PasswordRetrievalCompletionThunk& passwordRetrievalCompletionThunk)
{
    if (initializedWithPassword)
    {
        // Must be performed last before returning:
        passwordRetrievalCompletionThunk.postPassword(password.c_str());
    }
    else
    {
        clearPasswordDialog();

        passwordCompletionCallback = new PasswordDialogCompletionCallback(this, passwordRetrievalCompletionThunk);

        std::string passwordKeyboardDialogTitle = "Enter VNC Password For Host: ";
        passwordKeyboardDialogTitle += hostname;
        passwordKeyboardDialog = new KeyboardDialog("PasswordDialog", Vrui::getWidgetManager(), passwordKeyboardDialogTitle.c_str());

        Vrui::popupPrimaryWidget(passwordKeyboardDialog, Vrui::getNavigationTransformation().transform(Vrui::getDisplayCenter()));

        passwordKeyboardDialog->activate(*passwordCompletionCallback, true);  // passwordCompletionCallback will perform passwordRetrievalCompletionThunk.postPassword()
    }
}



//----------------------------------------------------------------------

void VncDialog::closeButtonCallback(GLMotif::Button::CallbackData* cbData)
{
    close();
}



template<class PopupWindowClass>
    void VncDialog::closePopupWindow(PopupWindowClass*& var)
{
    if (var)
    {
        var->getManager()->popdownWidget(var);
        var->getManager()->deleteWidget(var);
        var = 0;
    }
}



void VncDialog::clearPasswordDialog()
{
    closePopupWindow(passwordKeyboardDialog);

    if (passwordCompletionCallback)
    {
        delete passwordCompletionCallback;
        passwordCompletionCallback = 0;
    }
}



void VncDialog::resetConnection()
{
    if (vncWidget)
    {
        vncWidget->shutdown();
        vncWidget = 0;
        // Note: vncWidget is owned by (a descendent of) this
    }

    clearPasswordDialog();
}

}  // end of namespace Voltaic
