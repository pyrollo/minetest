/*
Minetest
Copyright (C) 2019 EvicenceBKidscode / Pierre-Yves Rollo <dev@pyrollo.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 2.1 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "guiScrollBar.h"
#include "TextDrawer.h"
#include "IGUIFont.h"
#include "IGUIElement.h"
#include "IGUIEnvironment.h"
using namespace irr::gui;

/*
#include <vector>
#include <list>
#include <unordered_map>
#include "client/fontengine.h"
#include <SColor.h>
#include "client/tile.h"
#include "IVideoDriver.h"
*/
#include "client/client.h"
#include "client/renderingengine.h"
//#include "hud.h"
#include "guiHyperText.h"
//#include "util/string.h"

// -----------------------------------------------------------------------------
// GUIHyperText - The formated text area formspec item

//! constructor
GUIHyperText::GUIHyperText(const wchar_t *text, IGUIEnvironment *environment,
		IGUIElement *parent, s32 id, const core::rect<s32> &rectangle,
		Client *client, ISimpleTextureSource *tsrc) :
		IGUIElement(EGUIET_ELEMENT, environment, parent, id, rectangle),
		m_client(client), m_vscrollbar(nullptr), m_text_scrollpos(0, 0)
{

#ifdef _DEBUG
	setDebugName("GUIHyperText");
#endif

	IGUISkin *skin = 0;
	if (Environment)
		skin = Environment->getSkin();

	m_scrollbar_width = skin ? skin->getSize(gui::EGDS_SCROLLBAR_SIZE) : 16;

	core::rect<s32> rect = irr::core::rect<s32>(
			RelativeRect.getWidth() - m_scrollbar_width, 0,
			RelativeRect.getWidth(), RelativeRect.getHeight());

	m_vscrollbar = new GUIScrollBar(Environment, this, -1, rect, false, true);
	m_vscrollbar->setVisible(false);

	m_drawer = new TextDrawer(text, environment, m_client);
}

//! destructor
GUIHyperText::~GUIHyperText()
{
	delete m_drawer;
	m_vscrollbar->remove();
	m_vscrollbar->drop();
}
void GUIHyperText::checkHover(s32 X, s32 Y)
{
/*
	m_drawer.m_hovertag = nullptr;

	if (AbsoluteRect.isPointInside(core::position2d<s32>(X, Y))) {
		ParsedText::Element *element = getElementAt(X, Y);

		if (element) {
			for (auto &tag : element->tags) {
				if (tag->name == "action") {
					m_drawer.m_hovertag = tag;
					break;
				}
			}
		}
	}

#ifndef HAVE_TOUCHSCREENGUI
	if (m_drawer.m_hovertag)
		RenderingEngine::get_raw_device()->getCursorControl()->setActiveIcon(
				gui::ECI_HAND);
	else
		RenderingEngine::get_raw_device()->getCursorControl()->setActiveIcon(
				gui::ECI_NORMAL);
#endif
*/
}

bool GUIHyperText::OnEvent(const SEvent &event)
{
/*
	// Scroll bar
	if (event.EventType == EET_GUI_EVENT &&
			event.GUIEvent.EventType == EGET_SCROLL_BAR_CHANGED &&
			event.GUIEvent.Caller == m_vscrollbar) {
		m_text_scrollpos.Y = -m_vscrollbar->getPos();
	}

	// Reset hover if element left
	if (event.EventType == EET_GUI_EVENT &&
			event.GUIEvent.EventType == EGET_ELEMENT_LEFT) {
		m_drawer.m_hovertag = nullptr;
#ifndef HAVE_TOUCHSCREENGUI
		gui::ICursorControl *cursor_control =
				RenderingEngine::get_raw_device()->getCursorControl();
		if (cursor_control->isVisible())
			cursor_control->setActiveIcon(gui::ECI_NORMAL);
#endif
	}

	if (event.EventType == EET_MOUSE_INPUT_EVENT) {
		if (event.MouseInput.Event == EMIE_MOUSE_MOVED)
			checkHover(event.MouseInput.X, event.MouseInput.Y);

		if (event.MouseInput.Event == EMIE_MOUSE_WHEEL) {
			m_vscrollbar->setPos(m_vscrollbar->getPos() -
					event.MouseInput.Wheel * m_vscrollbar->getSmallStep());
			m_text_scrollpos.Y = -m_vscrollbar->getPos();
			m_drawer.draw(m_display_text_rect, m_text_scrollpos);
			checkHover(event.MouseInput.X, event.MouseInput.Y);
			return true;

		} else if (event.MouseInput.Event == EMIE_LMOUSE_PRESSED_DOWN) {
			ParsedText::Element *element = getElementAt(
					event.MouseInput.X, event.MouseInput.Y);

			if (element) {
				for (auto &tag : element->tags) {
					if (tag->name == "action") {
						Text = core::stringw(L"action:") +
						       utf8_to_stringw(tag->attrs["name"]);
						if (Parent) {
							SEvent newEvent;
							newEvent.EventType = EET_GUI_EVENT;
							newEvent.GUIEvent.Caller = this;
							newEvent.GUIEvent.Element = 0;
							newEvent.GUIEvent.EventType = EGET_BUTTON_CLICKED;
							Parent->OnEvent(newEvent);
						}
						break;
					}
				}
			}
		}
	}
*/
	return IGUIElement::OnEvent(event);
}

//! draws the element and its children
void GUIHyperText::draw()
{
	if (!IsVisible)
		return;

	// Text
	m_display_text_rect = AbsoluteRect;
	m_drawer->layout(m_display_text_rect);

	// Show scrollbar if text overflow
	if (m_drawer->getHeight() > (u32)m_display_text_rect.getHeight()) {
		m_vscrollbar->setSmallStep(m_display_text_rect.getHeight() * 0.1f);
		m_vscrollbar->setLargeStep(m_display_text_rect.getHeight() * 0.5f);
		m_vscrollbar->setMax(m_drawer->getHeight() - m_display_text_rect.getHeight());

		m_vscrollbar->setVisible(true);

		m_vscrollbar->setPageSize(s32(m_drawer->getHeight()));

		core::rect<s32> smaller_rect = m_display_text_rect;

		smaller_rect.LowerRightCorner.X -= m_scrollbar_width;
		m_drawer->layout(smaller_rect);
	} else {
		m_vscrollbar->setMax(0);
		m_vscrollbar->setPos(0);
		m_vscrollbar->setVisible(false);
	}

	m_drawer->draw(AbsoluteClippingRect,
			m_display_text_rect.UpperLeftCorner + m_text_scrollpos);

	// draw children
	IGUIElement::draw();
}
