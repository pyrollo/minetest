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

#pragma once

using namespace irr;

class TextDrawer;
class ISimpleTextureSource;
class Client;

class GUIHyperText : public gui::IGUIElement
{
public:
	//! constructor
	GUIHyperText(const wchar_t *text, gui::IGUIEnvironment *environment,
			gui::IGUIElement *parent, s32 id,
			const core::rect<s32> &rectangle, Client *client,
			ISimpleTextureSource *tsrc);

	//! destructor
	virtual ~GUIHyperText();

	//! draws the element and its children
	virtual void draw();

	core::dimension2du getTextDimension();

	bool OnEvent(const SEvent &event);

protected:
	// GUI members
	Client *m_client;
	GUIScrollBar *m_vscrollbar;
	TextDrawer *m_drawer;

	// Positioning
	u32 m_scrollbar_width;
	core::rect<s32> m_display_text_rect;
	core::position2d<s32> m_text_scrollpos;

//	ParsedText::Element *getElementAt(s32 X, s32 Y);
	void checkHover(s32 X, s32 Y);
};
