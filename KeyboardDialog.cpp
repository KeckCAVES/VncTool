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

#include "KeyboardDialog.h"

#include <Misc/CallbackData.h>
#include <GLMotif/RowColumn.h>
#include <GLMotif/Blind.h>
#include <GLMotif/WidgetAlgorithms.h>
#include <GLMotif/StyleSheet.h>



namespace Voltaic {

//----------------------------------------------------------------------

static double timeToSeconds(Misc::Time t)
{
    return (double)t.tv_sec + t.tv_nsec/1.0e9;
}



//----------------------------------------------------------------------

KeyboardDialog::KeyValueDef::KeyValueDef( keysym_t v____, keysym_t v___S, keysym_t v__C_, keysym_t v__CS,
                                          keysym_t v_A__, keysym_t v_A_S, keysym_t v_AC_, keysym_t v_ACS,
                                          keysym_t vP___, keysym_t vP__S, keysym_t vP_C_, keysym_t vP_CS,
                                          keysym_t vPA__, keysym_t vPA_S, keysym_t vPAC_, keysym_t vPACS )
{
    values[ 0                                                                                    ] = v____;
    values[                                                                   MODIFIER_BIT_SHIFT ] = v___S;
    values[                                            MODIFIER_BIT_CONTROL                      ] = v__C_;
    values[                                            MODIFIER_BIT_CONTROL | MODIFIER_BIT_SHIFT ] = v__CS;
    values[                         MODIFIER_BIT_ALT                                             ] = v_A__;
    values[                         MODIFIER_BIT_ALT |                        MODIFIER_BIT_SHIFT ] = v_A_S;
    values[                         MODIFIER_BIT_ALT | MODIFIER_BIT_CONTROL                      ] = v_AC_;
    values[                         MODIFIER_BIT_ALT | MODIFIER_BIT_CONTROL | MODIFIER_BIT_SHIFT ] = v_ACS;
    values[ MODIFIER_BIT_PLATFORM                                                                ] = vP___;
    values[ MODIFIER_BIT_PLATFORM |                                           MODIFIER_BIT_SHIFT ] = vP__S;
    values[ MODIFIER_BIT_PLATFORM |                    MODIFIER_BIT_CONTROL                      ] = vP_C_;
    values[ MODIFIER_BIT_PLATFORM |                    MODIFIER_BIT_CONTROL | MODIFIER_BIT_SHIFT ] = vP_CS;
    values[ MODIFIER_BIT_PLATFORM | MODIFIER_BIT_ALT                                             ] = vPA__;
    values[ MODIFIER_BIT_PLATFORM | MODIFIER_BIT_ALT |                        MODIFIER_BIT_SHIFT ] = vPA_S;
    values[ MODIFIER_BIT_PLATFORM | MODIFIER_BIT_ALT | MODIFIER_BIT_CONTROL                      ] = vPAC_;
    values[ MODIFIER_BIT_PLATFORM | MODIFIER_BIT_ALT | MODIFIER_BIT_CONTROL | MODIFIER_BIT_SHIFT ] = vPACS;
}
KeyboardDialog::KeyButton::KeyButton( const char*         name,
                                      GLMotif::Container* parent,
                                      KeyboardDialog&     keyboard,
                                      bool                repeatable ) :
    GLMotif::Button(name, parent, name, false),
    keyboard(keyboard),
    repeatable(repeatable),
    keyArmed(false),
    keyArmTime(0),
    lastKeyFireTimeDelta(0)
{
    if (this->repeatable)
        this->getArmCallbacks().add(this, &KeyButton::armCallback);
    else
        this->getSelectCallbacks().add(this, &KeyButton::selectCallback);

    this->manageChild();
}



KeyboardDialog::KeyButton::~KeyButton()
{
}



void KeyboardDialog::KeyButton::armCallback(GLMotif::Button::ArmCallbackData* cbData)
{
    if (!cbData->isArmed)
    {
        keyArmed = false;
    }
    else
    {
        if (!keyArmed)
        {
            fire();  // first nonrepeated firing

            keyArmed             = true;
            keyArmTime           = Misc::Time::now();
            lastKeyFireTimeDelta = 0;
        }
        else
        {
            const double nowTimeDelta = timeToSeconds(Misc::Time::now() - keyArmTime);

            if ((lastKeyFireTimeDelta < keyboard.autoRepeatDelay) && (nowTimeDelta >= keyboard.autoRepeatDelay))
            {
                // Note: this won't happen if keyboard.autoRepeatDelay <= 0
                fire();
                lastKeyFireTimeDelta = keyboard.autoRepeatDelay;
            }

            if (keyboard.autoRepeatInterval <= 0)
            {
                // Just fire as often as we get this callback if keyboard.autoRepeatInterval == 0
                fire();
                lastKeyFireTimeDelta = nowTimeDelta;
            }
            else if (lastKeyFireTimeDelta >= keyboard.autoRepeatDelay)
            {
                for (double t = lastKeyFireTimeDelta + keyboard.autoRepeatInterval; t <= nowTimeDelta; t += keyboard.autoRepeatInterval)
                {
                    fire();
                    lastKeyFireTimeDelta = t;
                }
            }
        }
    }
}



void KeyboardDialog::KeyButton::selectCallback(GLMotif::Button::SelectCallbackData* cbData)
{
    // Handle nonrepeatable keys on key up.
    // This prevents a crash on cancel and submit which cause
    // the keyboard to be deleted.  Otherwise, a pointer event
    // is attempted to the keyboard after it is deleted.

    fire();
}



KeyboardDialog::ValueKeyButton::ValueKeyButton( const char*         name,
                                                GLMotif::Container* parent,
                                                KeyboardDialog&     keyboard,
                                                bool                repeatable,
                                                KeyValueDef::keysym_t v____, KeyValueDef::keysym_t v___S, KeyValueDef::keysym_t v__C_, KeyValueDef::keysym_t v__CS,
                                                KeyValueDef::keysym_t v_A__, KeyValueDef::keysym_t v_A_S, KeyValueDef::keysym_t v_AC_, KeyValueDef::keysym_t v_ACS,
                                                KeyValueDef::keysym_t vP___, KeyValueDef::keysym_t vP__S, KeyValueDef::keysym_t vP_C_, KeyValueDef::keysym_t vP_CS,
                                                KeyValueDef::keysym_t vPA__, KeyValueDef::keysym_t vPA_S, KeyValueDef::keysym_t vPAC_, KeyValueDef::keysym_t vPACS ) :
    KeyButton(name, parent, keyboard, repeatable),
    valueDef( v____, v___S, v__C_, v__CS,
              v_A__, v_A_S, v_AC_, v_ACS,
              vP___, vP__S, vP_C_, vP_CS,
              vPA__, vPA_S, vPAC_, vPACS  )
{
}



void KeyboardDialog::ValueKeyButton::fire()
{
    keyboard.addChar(valueDef.getChar(keyboard.getModifierMask()));
}



KeyboardDialog::UpdatingValueKeyButton::UpdatingValueKeyButton( const char*         name,
                                                                GLMotif::Container* parent,
                                                                KeyboardDialog&     keyboard,
                                                                bool                repeatable,
                                                                KeyValueDef::keysym_t v____, KeyValueDef::keysym_t v___S, KeyValueDef::keysym_t v__C_, KeyValueDef::keysym_t v__CS,
                                                                KeyValueDef::keysym_t v_A__, KeyValueDef::keysym_t v_A_S, KeyValueDef::keysym_t v_AC_, KeyValueDef::keysym_t v_ACS,
                                                                KeyValueDef::keysym_t vP___, KeyValueDef::keysym_t vP__S, KeyValueDef::keysym_t vP_C_, KeyValueDef::keysym_t vP_CS,
                                                                KeyValueDef::keysym_t vPA__, KeyValueDef::keysym_t vPA_S, KeyValueDef::keysym_t vPAC_, KeyValueDef::keysym_t vPACS ) :
    ValueKeyButton( name, parent, keyboard, repeatable,
                    v____, v___S, v__C_, v__CS,
                    v_A__, v_A_S, v_AC_, v_ACS,
                    vP___, vP__S, vP_C_, vP_CS,
                    vPA__, vPA_S, vPAC_, vPACS  )
{
}



void KeyboardDialog::UpdatingValueKeyButton::updateFromModifierMask(unsigned modifierMask)
{
    const char ch = valueDef.getChar(modifierMask); 

    char s[2];
    s[0] = ch ? ch : ' ';
    s[1] = '\0';

    this->setLabel(s);
}



KeyboardDialog::ActionKeyButton::ActionKeyButton( const char*         name,
                                                  GLMotif::Container* parent,
                                                  KeyboardDialog&     keyboard,
                                                  bool                repeatable,
                                                  void (KeyboardDialog::*action)() ) :
    KeyButton(name, parent, keyboard, repeatable),
    action(action)
{
}



void KeyboardDialog::ActionKeyButton::fire()
{
    (keyboard.*action)();
}



void KeyboardDialog::ModifierKeyButton::UpdateValueKeyButtonFunctor::operator()(GLMotif::Widget* widget) const
{
    UpdatingValueKeyButton* const updatingButton = dynamic_cast<UpdatingValueKeyButton*>(widget);

    if (updatingButton)
        updatingButton->updateFromModifierMask(modifierMask);
}



KeyboardDialog::ModifierKeyButton::ModifierKeyButton( const char*         name,
                                                      GLMotif::Container* parent,
                                                      KeyboardDialog&     keyboard,
                                                      bool&               modifierFlag ) :
    GLMotif::ToggleButton(name, parent, name, false),
    keyboard(keyboard),
    modifierFlag(modifierFlag)
{
    this->getSelectCallbacks().add(this, &ModifierKeyButton::selectCallback);
    this->manageChild();
}



void KeyboardDialog::ModifierKeyButton::selectCallback(GLMotif::Button::SelectCallbackData* cbData)
{
    modifierFlag = getToggle();

    const KeyboardDialog::ModifierKeyButton::UpdateValueKeyButtonFunctor updateKeyButtonFunctor(keyboard.getModifierMask());
    GLMotif::traverseWidgetTree(this->getRoot(), updateKeyButtonFunctor);
}



KeyboardDialog::SwitchButton::SwitchButton( const char*         name,
                                            GLMotif::Container* parent,
                                            KeyboardDialog&     keyboard,
                                            bool&               switchFlag,
                                            void (KeyboardDialog::*updateAction)() ) :
    GLMotif::ToggleButton(name, parent, name, false),
    keyboard(keyboard),
    switchFlag(switchFlag),
    updateAction(updateAction)
{
    this->getSelectCallbacks().add(this, &SwitchButton::selectCallback);
    this->manageChild();
}



void KeyboardDialog::SwitchButton::selectCallback(GLMotif::Button::SelectCallbackData* cbData)
{
    switchFlag = getToggle();
    (keyboard.*updateAction)();
}



//----------------------------------------------------------------------

KeyboardDialog::KeyboardDialog( const char*             name,
                                GLMotif::WidgetManager* manager,
                                const char*             titleString,
                                const char*             initialValue,
                                double                  autoRepeatDelay,
                                double                  autoRepeatInterval ) :
    GLMotif::PopupWindow(name, manager, titleString),
    autoRepeatDelay(autoRepeatDelay),
    autoRepeatInterval(autoRepeatInterval),
    completionCallback(0),
    obscureEntry(false),
    shiftKeyDown(false),
    controlKeyDown(false),
    altKeyDown(false),
    platformKeyDown(false),
    obscureEntryToggle(0),
    leftShiftToggle(0),
    rightShiftToggle(0),
    controlToggle(0),
    altToggle(0),
    platformToggle(0),
    display(0),
    buffer(initialValue ? initialValue : "")
{
    GLMotif::RowColumn* const keypad = new GLMotif::RowColumn("keypad", this, false);
    keypad->setPacking(GLMotif::RowColumn::PACK_GRID);

    GLMotif::RowColumn* const displayRow = new GLMotif::RowColumn("displayRow", keypad, false);
    displayRow->setOrientation(GLMotif::RowColumn::HORIZONTAL);
    displayRow->setPacking(GLMotif::RowColumn::PACK_TIGHT);

    GLMotif::RowColumn* const displayBox = new GLMotif::RowColumn("displayBox", displayRow, false);
    displayBox->setPacking(GLMotif::RowColumn::PACK_TIGHT);
    displayBox->setBorderWidth(0);
    displayBox->setMarginWidth(0);
    displayBox->setSpacing(0);
    GLMotif::Blind* displaySizer = new GLMotif::Blind("displaySizer", displayBox, false);
    displaySizer->setBorderWidth(0);
    displaySizer->setBorderType(GLMotif::Widget::PLAIN);
    displaySizer->setPreferredSize(GLMotif::Vector(100.0*displaySizer->getStyleSheet()->size, 0, 0));
    displaySizer->manageChild();
    display = new GLMotif::Label("display", displayBox, buffer.c_str(), false);
    display->setBackgroundColor(GLMotif::Color(1, 1, 1));
    display->setForegroundColor(GLMotif::Color(0, 0, 0));
    display->setHAlignment(GLFont::Left);
    display->setBorderWidth(display->getStyleSheet()->textfieldBorderWidth);
    display->setBorderType(GLMotif::Widget::LOWERED);
    display->manageChild();
    displayBox->manageChild();

    obscureEntryToggle = new SwitchButton("Hide typing", displayRow, *this, obscureEntry, &KeyboardDialog::updateDisplay);
    obscureEntryToggle->setToggle(obscureEntry);
    new ActionKeyButton("CLEAR", displayRow, *this, false, &KeyboardDialog::clear);
    new ActionKeyButton("CANCEL", displayRow, *this, false, &KeyboardDialog::cancel);

    displayRow->manageChild();

    GLMotif::RowColumn* const row1 = new GLMotif::RowColumn("row1", keypad, false);
    row1->setOrientation(GLMotif::RowColumn::HORIZONTAL);
    row1->setPacking(GLMotif::RowColumn::PACK_GRID);
    new UpdatingValueKeyButton("`", row1, *this, true, (KeyValueDef::keysym_t)'`', (KeyValueDef::keysym_t)'~' /*, ... !!!*/);
    new UpdatingValueKeyButton("1", row1, *this, true, (KeyValueDef::keysym_t)'1', (KeyValueDef::keysym_t)'!' /*, ... !!!*/);
    new UpdatingValueKeyButton("2", row1, *this, true, (KeyValueDef::keysym_t)'2', (KeyValueDef::keysym_t)'@' /*, ... !!!*/);
    new UpdatingValueKeyButton("3", row1, *this, true, (KeyValueDef::keysym_t)'3', (KeyValueDef::keysym_t)'#' /*, ... !!!*/);
    new UpdatingValueKeyButton("4", row1, *this, true, (KeyValueDef::keysym_t)'4', (KeyValueDef::keysym_t)'$' /*, ... !!!*/);
    new UpdatingValueKeyButton("5", row1, *this, true, (KeyValueDef::keysym_t)'5', (KeyValueDef::keysym_t)'%' /*, ... !!!*/);
    new UpdatingValueKeyButton("6", row1, *this, true, (KeyValueDef::keysym_t)'6', (KeyValueDef::keysym_t)'^' /*, ... !!!*/);
    new UpdatingValueKeyButton("7", row1, *this, true, (KeyValueDef::keysym_t)'7', (KeyValueDef::keysym_t)'&' /*, ... !!!*/);
    new UpdatingValueKeyButton("8", row1, *this, true, (KeyValueDef::keysym_t)'8', (KeyValueDef::keysym_t)'*' /*, ... !!!*/);
    new UpdatingValueKeyButton("9", row1, *this, true, (KeyValueDef::keysym_t)'9', (KeyValueDef::keysym_t)'(' /*, ... !!!*/);
    new UpdatingValueKeyButton("0", row1, *this, true, (KeyValueDef::keysym_t)'0', (KeyValueDef::keysym_t)')' /*, ... !!!*/);
    new UpdatingValueKeyButton("-", row1, *this, true, (KeyValueDef::keysym_t)'-', (KeyValueDef::keysym_t)'_' /*, ... !!!*/);
    new UpdatingValueKeyButton("=", row1, *this, true, (KeyValueDef::keysym_t)'=', (KeyValueDef::keysym_t)'+' /*, ... !!!*/);
    new ActionKeyButton("DEL", row1, *this, true, &KeyboardDialog::backspace);
    row1->manageChild();

    GLMotif::RowColumn* const row2 = new GLMotif::RowColumn("row2", keypad, false);
    row2->setOrientation(GLMotif::RowColumn::HORIZONTAL);
    row2->setPacking(GLMotif::RowColumn::PACK_GRID);
    new ValueKeyButton("TAB", row2, *this, true, (KeyValueDef::keysym_t)'\t', (KeyValueDef::keysym_t)'\t' /*, ... !!!*/);
    new UpdatingValueKeyButton("q",  row2, *this, true, (KeyValueDef::keysym_t)'q',  (KeyValueDef::keysym_t)'Q'  /*, ... !!!*/);
    new UpdatingValueKeyButton("w",  row2, *this, true, (KeyValueDef::keysym_t)'w',  (KeyValueDef::keysym_t)'W'  /*, ... !!!*/);
    new UpdatingValueKeyButton("e",  row2, *this, true, (KeyValueDef::keysym_t)'e',  (KeyValueDef::keysym_t)'E'  /*, ... !!!*/);
    new UpdatingValueKeyButton("r",  row2, *this, true, (KeyValueDef::keysym_t)'r',  (KeyValueDef::keysym_t)'R'  /*, ... !!!*/);
    new UpdatingValueKeyButton("t",  row2, *this, true, (KeyValueDef::keysym_t)'t',  (KeyValueDef::keysym_t)'T'  /*, ... !!!*/);
    new UpdatingValueKeyButton("y",  row2, *this, true, (KeyValueDef::keysym_t)'y',  (KeyValueDef::keysym_t)'Y'  /*, ... !!!*/);
    new UpdatingValueKeyButton("u",  row2, *this, true, (KeyValueDef::keysym_t)'u',  (KeyValueDef::keysym_t)'U'  /*, ... !!!*/);
    new UpdatingValueKeyButton("i",  row2, *this, true, (KeyValueDef::keysym_t)'i',  (KeyValueDef::keysym_t)'I'  /*, ... !!!*/);
    new UpdatingValueKeyButton("o",  row2, *this, true, (KeyValueDef::keysym_t)'o',  (KeyValueDef::keysym_t)'O'  /*, ... !!!*/);
    new UpdatingValueKeyButton("p",  row2, *this, true, (KeyValueDef::keysym_t)'p',  (KeyValueDef::keysym_t)'P'  /*, ... !!!*/);
    new UpdatingValueKeyButton("[",  row2, *this, true, (KeyValueDef::keysym_t)'[',  (KeyValueDef::keysym_t)'{'  /*, ... !!!*/);
    new UpdatingValueKeyButton("]",  row2, *this, true, (KeyValueDef::keysym_t)']',  (KeyValueDef::keysym_t)'}'  /*, ... !!!*/);
    new UpdatingValueKeyButton("\\", row2, *this, true, (KeyValueDef::keysym_t)'\\', (KeyValueDef::keysym_t)'|'  /*, ... !!!*/);
    row2->manageChild();

    GLMotif::RowColumn* const row3 = new GLMotif::RowColumn("row3", keypad, false);
    row3->setOrientation(GLMotif::RowColumn::HORIZONTAL);
    row3->setPacking(GLMotif::RowColumn::PACK_GRID);
    controlToggle = new ModifierKeyButton("CTRL", row3, *this, controlKeyDown);
    new UpdatingValueKeyButton("a",  row3, *this, true, (KeyValueDef::keysym_t)'a',  (KeyValueDef::keysym_t)'A'  /*, ... !!!*/);
    new UpdatingValueKeyButton("s",  row3, *this, true, (KeyValueDef::keysym_t)'s',  (KeyValueDef::keysym_t)'S'  /*, ... !!!*/);
    new UpdatingValueKeyButton("d",  row3, *this, true, (KeyValueDef::keysym_t)'d',  (KeyValueDef::keysym_t)'D'  /*, ... !!!*/);
    new UpdatingValueKeyButton("f",  row3, *this, true, (KeyValueDef::keysym_t)'f',  (KeyValueDef::keysym_t)'F'  /*, ... !!!*/);
    new UpdatingValueKeyButton("g",  row3, *this, true, (KeyValueDef::keysym_t)'g',  (KeyValueDef::keysym_t)'G'  /*, ... !!!*/);
    new UpdatingValueKeyButton("h",  row3, *this, true, (KeyValueDef::keysym_t)'h',  (KeyValueDef::keysym_t)'H'  /*, ... !!!*/);
    new UpdatingValueKeyButton("j",  row3, *this, true, (KeyValueDef::keysym_t)'j',  (KeyValueDef::keysym_t)'J'  /*, ... !!!*/);
    new UpdatingValueKeyButton("k",  row3, *this, true, (KeyValueDef::keysym_t)'k',  (KeyValueDef::keysym_t)'K'  /*, ... !!!*/);
    new UpdatingValueKeyButton("l",  row3, *this, true, (KeyValueDef::keysym_t)'l',  (KeyValueDef::keysym_t)'L'  /*, ... !!!*/);
    new UpdatingValueKeyButton(";",  row3, *this, true, (KeyValueDef::keysym_t)';',  (KeyValueDef::keysym_t)':'  /*, ... !!!*/);
    new UpdatingValueKeyButton("\"", row3, *this, true, (KeyValueDef::keysym_t)'"',  (KeyValueDef::keysym_t)'\'' /*, ... !!!*/);
    new ActionKeyButton("ENTER", row3, *this, false, &KeyboardDialog::submit);
    row3->manageChild();

    GLMotif::RowColumn* const row4 = new GLMotif::RowColumn("row4", keypad, false);
    row4->setOrientation(GLMotif::RowColumn::HORIZONTAL);
    row4->setPacking(GLMotif::RowColumn::PACK_GRID);
    leftShiftToggle = new ModifierKeyButton("SHIFT", row4, *this, shiftKeyDown);
    new UpdatingValueKeyButton("z", row4, *this, true, (KeyValueDef::keysym_t)'z',  (KeyValueDef::keysym_t)'Z'  /*, ... !!!*/);
    new UpdatingValueKeyButton("x", row4, *this, true, (KeyValueDef::keysym_t)'x',  (KeyValueDef::keysym_t)'X'  /*, ... !!!*/);
    new UpdatingValueKeyButton("c", row4, *this, true, (KeyValueDef::keysym_t)'c',  (KeyValueDef::keysym_t)'C'  /*, ... !!!*/);
    new UpdatingValueKeyButton("v", row4, *this, true, (KeyValueDef::keysym_t)'v',  (KeyValueDef::keysym_t)'V'  /*, ... !!!*/);
    new UpdatingValueKeyButton("b", row4, *this, true, (KeyValueDef::keysym_t)'b',  (KeyValueDef::keysym_t)'B'  /*, ... !!!*/);
    new UpdatingValueKeyButton("n", row4, *this, true, (KeyValueDef::keysym_t)'n',  (KeyValueDef::keysym_t)'N'  /*, ... !!!*/);
    new UpdatingValueKeyButton("m", row4, *this, true, (KeyValueDef::keysym_t)'m',  (KeyValueDef::keysym_t)'M'  /*, ... !!!*/);
    new UpdatingValueKeyButton(",", row4, *this, true, (KeyValueDef::keysym_t)',',  (KeyValueDef::keysym_t)'<'  /*, ... !!!*/);
    new UpdatingValueKeyButton(".", row4, *this, true, (KeyValueDef::keysym_t)'.',  (KeyValueDef::keysym_t)'>'  /*, ... !!!*/);
    new UpdatingValueKeyButton("/", row4, *this, true, (KeyValueDef::keysym_t)'/',  (KeyValueDef::keysym_t)'?'  /*, ... !!!*/);
    rightShiftToggle = new ModifierKeyButton("SHIFT", row4, *this, shiftKeyDown);
    row4->manageChild();

    GLMotif::RowColumn* const row5 = new GLMotif::RowColumn("row5", keypad, false);
    row5->setOrientation(GLMotif::RowColumn::HORIZONTAL);
    row5->setPacking(GLMotif::RowColumn::PACK_GRID);
    new GLMotif::Blind("blind1", row5);
    new ValueKeyButton("SPACE", row5, *this, true, (KeyValueDef::keysym_t)' ',  (KeyValueDef::keysym_t)' '  /*, ... !!!*/);
    new GLMotif::Blind("blind2", row5);
    row5->manageChild();

    keypad->manageChild();
}



KeyboardDialog::~KeyboardDialog()
{
    reset();
    clear();
}



void KeyboardDialog::activate( CompletionCallback& theCompletionCallback,
                               bool                theObscureEntryFlag )
{
    reset();

    completionCallback = &theCompletionCallback;
    obscureEntry       = theObscureEntryFlag;

    if (obscureEntryToggle)
        obscureEntryToggle->setToggle(obscureEntry);
}



void KeyboardDialog::setAutoRepeatDelay(double value)
{
    autoRepeatDelay = value;
}



void KeyboardDialog::setAutoRepeatInterval(double value)
{
    autoRepeatInterval = value;
}



void KeyboardDialog::updateDisplay()
{
    if (!obscureEntry)
        display->setLabel(buffer.c_str());
    else
    {
        std::string obscured;
        obscured.assign(buffer.size(), '*');
        display->setLabel(obscured.c_str());
    }
}



void KeyboardDialog::addChar(char ch)
{
    if (ch)
    {
        buffer += ch;
        updateDisplay();
    }
}



void KeyboardDialog::backspace()
{
    const std::string::size_type n = buffer.size();
    if (n > 0)
    {
        buffer.replace(n-1, 1, 1, ' ');
        buffer.resize(n-1);

        updateDisplay();
    }
}



void KeyboardDialog::reset()
{
    obscureEntry    = false;

    shiftKeyDown    = false;
    controlKeyDown  = false;
    altKeyDown      = false;
    platformKeyDown = false;

    if (obscureEntryToggle)
        obscureEntryToggle->setToggle(obscureEntry);

    if (leftShiftToggle)  leftShiftToggle->setToggle(shiftKeyDown);
    if (rightShiftToggle) rightShiftToggle->setToggle(shiftKeyDown);
    if (controlToggle)    controlToggle->setToggle(controlKeyDown);
    if (altToggle)        altToggle->setToggle(altKeyDown);
    if (platformToggle)   platformToggle->setToggle(platformKeyDown);
}



void KeyboardDialog::clear()
{
    const std::string::size_type n = buffer.size();
    if (n > 0)
    {
        buffer.replace(0, n, n, ' ');  // erase text in case sensitive data was entered...
        buffer.erase();
    }

    updateDisplay();
}



void KeyboardDialog::submit()
{
    if (completionCallback)
    {
        completionCallback->keyboardDialogDidComplete(*this, false);
        completionCallback = 0;
    }
}



void KeyboardDialog::cancel()
{
    if (completionCallback)
    {
        completionCallback->keyboardDialogDidComplete(*this, true);
        completionCallback = 0;
    }
}

}  // end of namespace Voltaic
