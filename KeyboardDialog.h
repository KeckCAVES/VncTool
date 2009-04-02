/***********************************************************************
  KeyboardDialog - Vrui PopupWindow which implements a keyboard
  interface.
  Copyright (c) 2008 Voltaic

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

#ifndef KEYBOARDDIALOG_INCLUDED
#define KEYBOARDDIALOG_INCLUDED

#include <string>
#include <Misc/Time.h>
#include <Vrui/Vrui.h>
#include <GLMotif/PopupWindow.h>
#include <GLMotif/Label.h>
#include <GLMotif/Button.h>
#include <GLMotif/ToggleButton.h>



namespace Voltaic {

    class KeyboardDialog :
        public GLMotif::PopupWindow
    {
    public:
        enum
        {
            MODIFIER_BIT_SHIFT    = 0x01,
            MODIFIER_BIT_CONTROL  = 0x02,
            MODIFIER_BIT_ALT      = 0x04,
            MODIFIER_BIT_PLATFORM = 0x08,

            MODIFIER_KEY_VARIATIONS = 16
        };

    public:
        class KeyValueDef
        {
        public:
            typedef uint32_t keysym_t;

        public:
            KeyValueDef( keysym_t v____,     keysym_t v___S = 0, keysym_t v__C_ = 0, keysym_t v__CS = 0,
                         keysym_t v_A__ = 0, keysym_t v_A_S = 0, keysym_t v_AC_ = 0, keysym_t v_ACS = 0,
                         keysym_t vP___ = 0, keysym_t vP__S = 0, keysym_t vP_C_ = 0, keysym_t vP_CS = 0,
                         keysym_t vPA__ = 0, keysym_t vPA_S = 0, keysym_t vPAC_ = 0, keysym_t vPACS = 0 );

        public:
            keysym_t getKeysym(unsigned modifierMask) const { return values[modifierMask]; }

            char getChar(unsigned modifierMask) const
            {
                const keysym_t keysym = getKeysym(modifierMask);
                return (keysym > 0xff) ? (char)0 : (char)(keysym & 0xff);
            }

        protected:
            keysym_t values[MODIFIER_KEY_VARIATIONS];
        };

    public:
        class KeyButton : public GLMotif::Button
        {
        public:
            KeyButton( const char*         name,
                       GLMotif::Container* parent,
                       KeyboardDialog&     keyboard,
                       bool                repeatable );

            virtual ~KeyButton();

        protected:
            virtual void armCallback(GLMotif::Button::ArmCallbackData* cbData);  // for repeatable keys
            void selectCallback(GLMotif::Button::SelectCallbackData* cbData);    // for nonrepeatable keys
            virtual void fire() = 0;

        protected:
            KeyboardDialog& keyboard;
            const bool      repeatable;
            bool            keyArmed;
            Misc::Time      keyArmTime;
            double          lastKeyFireTimeDelta;
        };

        class ValueKeyButton : public KeyButton
        {
        public:
            ValueKeyButton( const char*         name,
                            GLMotif::Container* parent,
                            KeyboardDialog&     keyboard,
                            bool                repeatable,
                            KeyValueDef::keysym_t v____,     KeyValueDef::keysym_t v___S = 0, KeyValueDef::keysym_t v__C_ = 0, KeyValueDef::keysym_t v__CS = 0,
                            KeyValueDef::keysym_t v_A__ = 0, KeyValueDef::keysym_t v_A_S = 0, KeyValueDef::keysym_t v_AC_ = 0, KeyValueDef::keysym_t v_ACS = 0,
                            KeyValueDef::keysym_t vP___ = 0, KeyValueDef::keysym_t vP__S = 0, KeyValueDef::keysym_t vP_C_ = 0, KeyValueDef::keysym_t vP_CS = 0,
                            KeyValueDef::keysym_t vPA__ = 0, KeyValueDef::keysym_t vPA_S = 0, KeyValueDef::keysym_t vPAC_ = 0, KeyValueDef::keysym_t vPACS = 0 );

        protected:
            virtual void fire();

        protected:
            KeyValueDef valueDef;
        };

        class UpdatingValueKeyButton : public ValueKeyButton
        {
        public:
            UpdatingValueKeyButton( const char*         name,
                                    GLMotif::Container* parent,
                                    KeyboardDialog&     keyboard,
                                    bool                repeatable,
                                    KeyValueDef::keysym_t v____,     KeyValueDef::keysym_t v___S = 0, KeyValueDef::keysym_t v__C_ = 0, KeyValueDef::keysym_t v__CS = 0,
                                    KeyValueDef::keysym_t v_A__ = 0, KeyValueDef::keysym_t v_A_S = 0, KeyValueDef::keysym_t v_AC_ = 0, KeyValueDef::keysym_t v_ACS = 0,
                                    KeyValueDef::keysym_t vP___ = 0, KeyValueDef::keysym_t vP__S = 0, KeyValueDef::keysym_t vP_C_ = 0, KeyValueDef::keysym_t vP_CS = 0,
                                    KeyValueDef::keysym_t vPA__ = 0, KeyValueDef::keysym_t vPA_S = 0, KeyValueDef::keysym_t vPAC_ = 0, KeyValueDef::keysym_t vPACS = 0 );

        public:
            virtual void updateFromModifierMask(unsigned modifierMask);
        };

        class ActionKeyButton : public KeyButton
        {
        public:
            ActionKeyButton( const char*         name,
                             GLMotif::Container* parent,
                             KeyboardDialog&     keyboard,
                             bool                repeatable,
                             void (KeyboardDialog::*action)() );

        protected:
            virtual void fire();

        protected:
            void (KeyboardDialog::* action)();
        };

        class ModifierKeyButton : public GLMotif::ToggleButton
        {
        public:
            class UpdateValueKeyButtonFunctor
            {
            protected:
                const unsigned modifierMask;

            public:
                UpdateValueKeyButtonFunctor(unsigned modifierMask) :
                    modifierMask(modifierMask)
                {
                }

                virtual void operator()(GLMotif::Widget* widget) const;
            };

        public:
            ModifierKeyButton( const char*         name,
                               GLMotif::Container* parent,
                               KeyboardDialog&     keyboard,
                               bool&               modifierFlag );

        protected:
            virtual void selectCallback(GLMotif::Button::SelectCallbackData* cbData);

        protected:
            KeyboardDialog& keyboard;
            bool&           modifierFlag;
        };

        class SwitchButton : public GLMotif::ToggleButton
        {
        public:
            SwitchButton( const char*         name,
                          GLMotif::Container* parent,
                          KeyboardDialog&     keyboard,
                          bool&               switchFlag,
                          void (KeyboardDialog::*updateAction)() );

        protected:
            virtual void selectCallback(GLMotif::Button::SelectCallbackData* cbData);

        protected:
            KeyboardDialog& keyboard;
            bool&           switchFlag;
            void (KeyboardDialog::*updateAction)();
        };

        friend class ValueKeyButton;
        friend class ModifierKeyButton;
        friend class ActionKeyButton;
        friend class SwitchButton;

    public:
        class CompletionCallback
        {
        public:
            virtual void keyboardDialogDidComplete(KeyboardDialog& keyboardDialog, bool cancelled) = 0;
        };

    public:
        KeyboardDialog( const char*             name,
                        GLMotif::WidgetManager* manager,
                        const char*             titleString,
                        double                  autoRepeatDelay    = 0.5,
                        double                  autoRepeatInterval = 0.25 );

        virtual ~KeyboardDialog();

    public:
        virtual void activate( CompletionCallback& theCompletionCallback,
                               bool                theObscureEntryFlag = false );

        const std::string& getBuffer() const { return buffer; }

        unsigned getModifierMask() const
        {
            return (unsigned)( (shiftKeyDown    ? MODIFIER_BIT_SHIFT    : 0) |
                               (controlKeyDown  ? MODIFIER_BIT_CONTROL  : 0) |
                               (altKeyDown      ? MODIFIER_BIT_ALT      : 0) |
                               (platformKeyDown ? MODIFIER_BIT_PLATFORM : 0)   );
        }

    public:
        double getAutoRepeatDelay()  const { return autoRepeatDelay; }
        virtual void setAutoRepeatDelay(double value);

        double getAutoRepeatInterval() const { return autoRepeatInterval; }
        virtual void setAutoRepeatInterval(double value);
                        
    protected:
        virtual void updateDisplay();
        virtual void addChar(char ch);
        virtual void backspace();
        virtual void reset();
        virtual void clear();
        virtual void submit();          // causes callback to be called with cancelled = false
        virtual void cancel();          // causes callback to be called with cancelled = true

    protected:
        double              autoRepeatDelay;
        double              autoRepeatInterval;
        CompletionCallback* completionCallback;  // 0 ==> inactive
        bool                obscureEntry;
        bool                shiftKeyDown;
        bool                controlKeyDown;
        bool                altKeyDown;
        bool                platformKeyDown;
        SwitchButton*       obscureEntryToggle;
        ModifierKeyButton*  leftShiftToggle;
        ModifierKeyButton*  rightShiftToggle;
        ModifierKeyButton*  controlToggle;
        ModifierKeyButton*  altToggle;
        ModifierKeyButton*  platformToggle;
        GLMotif::Label*     display;
        std::string         buffer;

    private:
        // Disable these copiers:
        KeyboardDialog& operator=(const KeyboardDialog&);
        KeyboardDialog(const KeyboardDialog&);
    };

}  // end of namespace Voltaic

#endif  // KEYBOARDDIALOG_INCLUDED
