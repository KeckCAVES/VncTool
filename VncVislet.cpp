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
#include <Vrui/Vrui.h>
#include <ctype.h>



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

void VncVislet::PasswordDialogCompletionCallback::keyboardDialogDidComplete(KeyboardDialog& keyboardDialog, bool cancelled)
{
    std::string retrievedPassword;
    if (!cancelled)
        retrievedPassword = keyboardDialog.getBuffer();

    keyboardDialog.getManager()->popdownWidget(&keyboardDialog);
    keyboardDialog.getManager()->deleteWidget(&keyboardDialog);
    if (vncVislet->passwordKeyboardDialog == &keyboardDialog)
        vncVislet->passwordKeyboardDialog = 0;

    if (vncVislet && retrievedPassword.size() > 0)
    {
        // Must be performed last before returning:
        passwordRetrievalCompletionThunk.postPassword(retrievedPassword.c_str());
    }

    // The following is OK to do before returning...
    VncManager::eraseStringContents(retrievedPassword);
}



//----------------------------------------------------------------------

VncVisletFactory* VncVislet::factory = 0;  // static member



VncVislet::VncVislet(int numArguments, const char* const arguments[]) :
    Vrui::Vislet(),
    GLObject(),
    VncManager::MessageManager(),
    VncManager::PasswordRetrievalThunk(),
    closeCompleted(false),
    initViaConnect(true),
    hostname(),
    rfbPort(0),
    requestedEncodings(),
    sharedDesktopFlag(false),
    enableClickThrough(true),
    initializedWithPassword(false),
    password(),
    popupWindow(0),
    passwordCompletionCallback(0),
    passwordKeyboardDialog(0),
    vncWidget(0),
    closeButton(0),
    messageLabel(0)
{
    parseArguments(numArguments, arguments);
}



VncVislet::~VncVislet()
{
    resetConnection();
    closeAllPopupWindows();
}



Vrui::VisletFactory* VncVislet::getFactory() const
{
    return factory;
}



void VncVislet::disable()
{
    this->Vislet::disable();
    closeAllPopupWindows();
    updateUIState();
    Vrui::requestUpdate();
}



void VncVislet::enable()
{
    closeCompleted = false;
    this->Vislet::enable();
    updateUIState();
    Vrui::requestUpdate();
}



void VncVislet::initContext(GLContextData& contextData) const
{
    // nothing...
}



void VncVislet::frame()
{
    updateUIState();
    Vrui::requestUpdate();  // keep the requests coming so we can keep calling the update loop
}



void VncVislet::display(GLContextData& contextData) const
{
    // nothing...
}



//----------------------------------------------------------------------
// VncManager::MessageManager methods

void VncVislet::internalErrorMessage(const char* where, const char* message)
{
    if (Vrui::isMaster())
        fprintf(stderr, "** VncVislet: internal error at %s: %s\n", where, message);
}



void VncVislet::errorMessage(const char* where, const char* message)
{
    // When changing the implementation of this method, think
    // about possible injection attacks from a malicious server.
    // If any are possible, reimplement errorMessageFromServer()
    // below to prevent exploitation.

    messageLabel->setLabel("Connection error");
}



void VncVislet::errorMessageFromServer(const char* where, const char* message)
{
    // Note: a malicious message from the server won't hurt us here, i.e., there
    // are no known injection attacks for the way we're handling this message....

    this->errorMessage(where, message);
}



void VncVislet::infoServerInitStarted()
{
    messageLabel->setLabel("Connecting...");
}



void VncVislet::infoProtocolVersion(int serverMajorVersion, int serverMinorVersion, int clientMajorVersion, int clientMinorVersion)
{
    // do nothing...
}



void VncVislet::infoAuthenticationResult(bool succeeded, rfbCARD32 authScheme, rfbCARD32 authResult)
{
    // do nothing...
}



void VncVislet::infoServerInitCompleted(bool succeeded)
{
    messageLabel->setLabel(succeeded ? "Connected" : "Connection failed");
}



void VncVislet::infoCloseStarted()
{
    // do nothing...
}



void VncVislet::infoCloseCompleted()
{
    // This will cause vncWidget->shutdown() to be called during next frame()->updateUIState() call.
    // We avoid some nasty reentrancy by doing it this way....
    closeCompleted = true;
}



//----------------------------------------------------------------------
// VncManager::PasswordRetrievalThunk method

void VncVislet::getPassword(VncManager::PasswordRetrievalCompletionThunk& passwordRetrievalCompletionThunk)
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

        std::string passwordKeyboardDialogTitle = "Enter VNC Password For Host:";
        passwordKeyboardDialogTitle += hostname;
        passwordKeyboardDialog = new KeyboardDialog("PasswordDialog", Vrui::getWidgetManager(), passwordKeyboardDialogTitle.c_str());

        Vrui::popupPrimaryWidget(passwordKeyboardDialog, Vrui::getNavigationTransformation().transform(Vrui::getDisplayCenter()));

        passwordKeyboardDialog->activate(*passwordCompletionCallback, true);  // passwordCompletionCallback will perform passwordRetrievalCompletionThunk.postPassword()
    }
}



//----------------------------------------------------------------------

void VncVislet::closeButtonCallback(GLMotif::Button::CallbackData* cbData)
{
    disable();
}



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



void VncVislet::closeAllPopupWindows()
{
    closePopupWindow(popupWindow);
    vncWidget    = 0;  // vncWidget is owned by (a descendent of) popupWindow
    closeButton  = 0;  // closeButton is owned by (a descendent of) popupWindow
    messageLabel = 0;  // messageLabel is owned by (a descendent of) popupWindow
}



void VncVislet::updateUIState()
{
    if (!isActive() || closeCompleted)
        resetConnection();
    else
    {
        if (!vncWidget)
        {
            // Create the popup window with the VncWidget and controls in it:

            if (popupWindow)
                closeAllPopupWindows();

            std::string popupWindowTitle = "Connection to ";
            popupWindowTitle.append(hostname);
            popupWindow = new GLMotif::PopupWindow("VncVislet", Vrui::getWidgetManager(), popupWindowTitle.c_str());
            {
                GLMotif::RowColumn* const topRowCol = new GLMotif::RowColumn("topRowCol", popupWindow, false);
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

                        closeButton->getSelectCallbacks().add(this, &VncVislet::closeButtonCallback);
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
            Vrui::popupPrimaryWidget(popupWindow, Vrui::getNavigationTransformation().transform(Vrui::getDisplayCenter()));

            if (vncWidget)
            {
                vncWidget->setEnableClickThrough(enableClickThrough);

                VncManager::RFBProtocolStartupData rfbProtocolStartupData;
                rfbProtocolStartupData.initViaConnect     = initViaConnect;
                rfbProtocolStartupData.desktopHost        = hostname.c_str();
                rfbProtocolStartupData.rfbPort            = rfbPort;
                rfbProtocolStartupData.requestedEncodings = requestedEncodings.c_str();
                rfbProtocolStartupData.sharedDesktopFlag  = sharedDesktopFlag;
                vncWidget->startup(rfbProtocolStartupData);
            }
        }

        if (popupWindow && vncWidget)
        {
            UpperLeftCornerPreserver upperLeftCornerPreserver(popupWindow);
            if (vncWidget)
                (void)vncWidget->checkForUpdates();
        }
    }
}



void VncVislet::clearPasswordDialog()
{
    closePopupWindow(passwordKeyboardDialog);

    if (passwordCompletionCallback)
    {
        delete passwordCompletionCallback;
        passwordCompletionCallback = 0;
    }
}



void VncVislet::resetConnection()
{
    if (vncWidget)
    {
        vncWidget->shutdown();
        vncWidget = 0;
        // Note: vncWidget is owned by (a descendent of) popupWindow
    }

    clearPasswordDialog();
}

}  // end of namespace Voltaic
