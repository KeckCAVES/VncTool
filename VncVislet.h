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

#include "VncDialog.h"



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
        public GLObject
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

    public:
        // Vrui::Vislet methods:
        virtual Vrui::VisletFactory* getFactory() const;
        virtual void disable();
        virtual void enable();
        virtual void initContext(GLContextData& contextData) const;
        virtual void frame();
        virtual void display(GLContextData& contextData) const;

    protected:
        void parseArguments(int numArguments, const char* const arguments[]);
        template<class PopupWindowClass>
            void closePopupWindow(PopupWindowClass*& var);

    private:
        static VncVisletFactory* factory;  // pointer to the factory object for this class

    protected:
        bool        initViaConnect;
        std::string hostname;
        unsigned    rfbPort;
        std::string requestedEncodings;
        bool        sharedDesktopFlag;
        bool        enableClickThrough;
        bool        initializedWithPassword;
        std::string password;  // the password from the initialization arguments if initializedWithPassword is true
        VncDialog*  vncDialog;

    private:
        // Disable these copiers:
        VncVislet& operator=(const VncVislet&);
        VncVislet(const VncVislet&);
    };

}  // namespace Voltaic

#endif  // #ifndef __VNCVISLET_H_INCLUDED__
