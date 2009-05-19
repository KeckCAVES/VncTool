/***********************************************************************
  VncTool - Class for tools that can interact with remote VNC servers.
  Copyright (c) 2007,2008 Voltaic

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

#ifndef __VNCTOOL_H_INCLUDED__
#define __VNCTOOL_H_INCLUDED__

#include <string>
#include <vector>
#include <list>
#include <Misc/Time.h>
#include <Misc/ThrowStdErr.h>
#include <Geometry/OrthogonalTransformation.h>
#include <GLMotif/PopupWindow.h>
#include <GLMotif/RadioBox.h>
#include <GLMotif/Label.h>
#include <GLMotif/TextField.h>
#include <GLMotif/ToggleButton.h>
#include <Vrui/Geometry.h>
#include <Vrui/Tools/Tool.h>
#include <Vrui/Tools/UtilityTool.h>

#include "VncVislet.h"
#include "KeyboardDialog.h"



namespace Voltaic {

    //----------------------------------------------------------------------
    class VncToolFactory : public Vrui::ToolFactory
    {
    friend class VncTool;

    public:
        class HostDescriptor : public VncManager::RFBProtocolStartupData
        {
        public:
            HostDescriptor();

            HostDescriptor( Misc::ConfigurationFileSection& cfs,
                            const char*                     configFileSection,
                            const char*                     hostName );

            HostDescriptor(const HostDescriptor& other);

            HostDescriptor& operator=(const HostDescriptor& other);

        public:
            std::string beginDataString;
            std::string interDatumString;
            std::string endDataString;
            bool        initialEnableClickThrough;
            bool        initialTimestampBeamedData;
            std::string initialBeamedDataTag;
            bool        initialAutoBeam;

        protected:
            std::string desktopHostString;
            std::string requestedEncodingsString;
        };

        typedef std::vector<HostDescriptor> HostDescriptorList;

    public:
        VncToolFactory(Vrui::ToolManager& toolManager);
        virtual ~VncToolFactory();

        virtual Vrui::Tool* createTool(const Vrui::ToolInputAssignment& inputAssignment) const;
        virtual void        destroyTool(Vrui::Tool* tool) const;

        const HostDescriptorList& getHostDescriptors() const { return hostDescriptors; }

    protected:
        HostDescriptorList hostDescriptors;

    private:
        // Disable these copiers:
        VncToolFactory& operator=(const VncToolFactory&);
        VncToolFactory(const VncToolFactory&);
    };



    //----------------------------------------------------------------------
    class VncTool :
        public Vrui::UtilityTool
    {
    friend class VncToolFactory;

    public:
        class Animation
        {
        public:
            static double timeToSeconds(Misc::Time t)
            {
                return (double)t.tv_sec + t.tv_nsec/1.0e9;
            }

            Animation( const Misc::Time& t0,
                       const Misc::Time& t1 ) :
                t0(t0),
                t1(t1),
                duration(Animation::timeToSeconds(t1-t0))
            {
                if (this->t0 > this->t1)
                    Misc::throwStdErr("Bad VncTool::Animation time range");
            }

            Animation(double runningTime) :
                t0(Misc::Time::now()),
                t1(),
                duration(runningTime)
            {
                if (runningTime < 0)
                    Misc::throwStdErr("Bad VncTool::Animation time range");

                t1 = t0;
                t1.increment(runningTime);
            }

            virtual ~Animation();

            virtual void showAt(Misc::Time t, GLContextData& contextData) const = 0;

            void show(GLContextData& contextData) const
            {
                Misc::Time t = Misc::Time::now();

                if (t < t0)
                    t = t0;
                else if (t > t1)
                    t = t1;

                showAt(t, contextData);
            }

            bool expired() const { return (Misc::Time::now() > t1); }

            const Misc::Time& getT0()       const { return t0; }
            const Misc::Time& getT1()       const { return t1; }
            double            getDuration() const { return duration; }

        protected:
            Misc::Time t0;
            Misc::Time t1;
            double     duration;  // in seconds
        };

        typedef std::list<Animation*> AnimationList;

    public:
        friend class PasswordDialogCompletionCallback;
        class PasswordDialogCompletionCallback : public KeyboardDialog::CompletionCallback
        {
        public:
            PasswordDialogCompletionCallback(VncTool* vncTool) :
                vncTool(vncTool)
            {
            }

        public:
            virtual void keyboardDialogDidComplete(KeyboardDialog& keyboardDialog, bool cancelled);

        protected:
            VncTool* const vncTool;
        };

    public:
        friend class BeamedDataTagInputCompletionCallback;
        class BeamedDataTagInputCompletionCallback : public KeyboardDialog::CompletionCallback
        {
        public:
            BeamedDataTagInputCompletionCallback( VncTool*              vncTool,
                                                  GLMotif::PopupWindow* popupWindow,
                                                  GLMotif::Label&       fieldToUpdate ) :
                vncTool(vncTool),
                popupWindow(popupWindow),
                fieldToUpdate(fieldToUpdate)
            {
            }

        public:
            virtual void keyboardDialogDidComplete(KeyboardDialog& keyboardDialog, bool cancelled);

        protected:
            VncTool* const        vncTool;
            GLMotif::PopupWindow* popupWindow;
            GLMotif::Label&       fieldToUpdate;
        };

    public:
        VncTool(const Vrui::ToolFactory* factory, const Vrui::ToolInputAssignment& inputAssignment);
        virtual ~VncTool();

    protected:
        virtual const Vrui::ToolFactory* getFactory() const;
        virtual void resetConnection();
        virtual void changeHostCallback(GLMotif::RadioBox::ValueChangedCallbackData* cbData);
        virtual void enableClickThroughToggleCallback(GLMotif::Button::CallbackData* cbData);
        virtual void changeBeamedDataTagCallback(GLMotif::Button::CallbackData* cbData);
        virtual void buttonCallback(int deviceIndex, int buttonIndex, Vrui::InputDevice::ButtonCallbackData* cbData);
        virtual GLMotif::Ray calcSelectionRay() const;  // calculates the selection ray based on current device position/orientation
        virtual void updateUIState();
        virtual void clearHostSelectorButtons() const;
        virtual void initiatePasswordRetrieval();

    public:
        virtual void frame();
        virtual void display(GLContextData& contextData) const;

    protected:
        template<class PopupWindowClass>
            void closePopupWindow(PopupWindowClass*& var);

    private:
        static VncToolFactory* factory;  // pointer to the factory object for this class

    protected:
        bool                                  insideWidget;                      // flag if the tool is currently able to interact with a widget
        bool                                  active;                            // flag if the tool is currently active
        GLMotif::Ray                          selectionRay;                      // current selection ray
        const VncToolFactory::HostDescriptor* hostDescriptor;                    // currently selected HostDescriptor, or 0 if none
        GLMotif::PopupWindow*                 popupWindow;
        GLMotif::RadioBox*                    hostSelector;
        GLMotif::ToggleButton*                enableClickThroughToggle;          // controls whether the user can click through to the remote terminal
        GLMotif::ToggleButton*                enableBeamDataToggle;              // controls whether the tool's "beam data" feature is enabled
        GLMotif::ToggleButton*                timestampBeamedDataToggle;         // controls whether beamed data is timestamped
        GLMotif::ToggleButton*                autoBeamToggle;                    // controls whether beamed data is timestamped
        GLMotif::Label*                       beamedDataTagField;                // sets extra "tag" field for beamed data
        GLMotif::Label*                       lastSelectedDialogDisplay;
        GLMotif::Widget*                      lastSelectedDialog;
        GLMotif::Label*                       messageLabel;
        VncVislet*                            vncVislet;
        PasswordDialogCompletionCallback*     passwordCompletionCallback;
        BeamedDataTagInputCompletionCallback* beamedDataTagInputCompletionCallback;
        KeyboardDialog*                       passwordKeyboardDialog;
        KeyboardDialog*                       dataEntryKeyboardDialog;
        AnimationList                         animations;

    private:
        // Disable these copiers:
        VncTool& operator=(const VncTool&);
        VncTool(const VncTool&);
    };

}  // end of namespace Voltaic

#endif  // #ifndef __VNCTOOL_H_INCLUDED__
