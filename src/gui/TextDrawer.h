/*
Part of Minetest
Copyright (C) 2020 Pierre-Yves Rollo <dev@pyrollo.com>

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

/* Missing regarding previous version:
- Floating
- Hover
*/

/*
enum FloatType {
	FT_NONE,
	FT_LEFT,
	FT_RIGHT,
};
*/
#pragma once

#include <string>
#include <unordered_map>
#include <map>
#include <deque>
#include "irrlichttypes_bloated.h"
#include "irrString.h"
//#include "IGUIEnvironment.h"
//using namespace irr;
#include "ParsedText.h"
#include "util/string.h"
#include "config.h" // USE_FREETYPE
#if USE_FREETYPE
#include "irrlicht_changes/CGUITTFont.h"
#endif

class Client;
namespace irr {
	namespace video {
		class IVideoDriver;
	}
}
class ITextureSource;
class IItemDefManager;

typedef core::dimension2d<u32> Dim;
typedef core::position2d<s32> Pos;

class TextDrawer
{
public:
	TextDrawer(core::stringw source, gui::IGUIEnvironment *environment, Client *client);
	void layout(const core::rect<s32> &rect);
	void draw(const core::rect<s32> &clip_rect, const Pos &offset);
	u32 getHeight();

protected:
	enum DisplayType {
		DT_NONE,
		// Do not display (not developped)
		DT_OMISSIBLE,
		// (not for standard elements)
		// Placed at right of last DT_INLINE_BLOCK or DT_OMISSIBLE, can be
		// ommited if on end or beginning of line.
		// Separators text fragments (space, newlines) are of this type.
		DT_INLINE_BLOCK,
		// Placed at right of last DT_INLINE_BLOCK or DT_OMISSIBLE.
		// Non blank text fragments are of this type.
		DT_INLINE,
		// Inline content (can be fragmented in several lines).
		DT_BLOCK,
		// Standard block, are placed out of inline content.
		DT_FLOAT_LEFT,
		// Block floating on left
		DT_FLOAT_RIGHT,
		// Block floating on right
		DT_ROOT,
		// (not for standard elements)
		// Only for root element
	};

	enum HalignType
	{
		HALIGN_CENTER,
		HALIGN_LEFT,
		HALIGN_RIGHT,
		HALIGN_JUSTIFY
	};

	struct StyleList {
		std::string get(std::string name);
		void set(std::string name, std::string value);
		bool has(std::string name);
		bool isYes(std::string name);
		void copyTo(StyleList &styles);

		std::unordered_map<std::string, std::string> m_styles;
	};

	struct Item {
		Item(TextDrawer *drawer, DisplayType display_type): drawer(drawer),
			display_type(display_type), dim(0, 0), drawdim(0, 0), pos(0, 0) {};

		virtual void applyStyles(StyleList *styles) = 0; // Use only styles that applies to this bloc

		// Size item according to given parent container size and inner atributes
		// Pos is computed by the parent container
		virtual void layout(Dim size, Pos offset) = 0;

		// Draw item with offset
		virtual void draw(const core::rect<s32> &clip_rect, const Pos &offset) = 0;

		virtual void populatePlaceableChildren(std::vector<Item *> *children) {};

		bool tryPlaceFloating(s32 y, s32 &left, s32 &right, u32 itemswidth, Pos offset);

		TextDrawer *drawer;

		DisplayType display_type;

		// Effective size and pos
		Dim dim;          // Intrinsic dimension of the item
		Dim drawdim;      // Drawn dimension of the item (including extra space)
		Pos pos;          // Position of the item computed during layout
		u32 baseline = 0; // Baseline used for vertical item alignment (if inline)
	};

	struct TextFragment : Item {
		TextFragment(TextDrawer *drawer, ParsedText::TextFragment *fragment);

		void applyStyles(StyleList *styles) override;
		void layout(Dim size, Pos offset) override;
		void draw(const core::rect<s32> &clip_rect, const Pos &offset) override;

		// TextFragment specific
		core::stringw text;

		// Styles
		gui::IGUIFont *font;
		irr::video::SColor color;
		//irr::video::SColor hovercolor;
		bool underline;
		bool strikethrough;
	};

	struct Container : Item {
		Container(TextDrawer *drawer, DisplayType display_type):
			Item(drawer, display_type) {};
		void applyStyles(StyleList *styles) override;
		void layout(Dim size, Pos offset) override;
		void draw(const core::rect<s32> &clip_rect, const Pos &offset) override;
		void populatePlaceableChildren(std::vector<Item *> *children) override;

		// Container specific
		std::vector<std::unique_ptr<Item>> children;

		// Styles
		HalignType halign;
		int margin;
	};

	Item *newContainer(ParsedText::AttrsList attrs, DisplayType display_type);
	Item *newParagraph(ParsedText::AttrsList attrs, DisplayType display_type);

	struct Image : Item {
		Image(TextDrawer *drawer, DisplayType display_type):
			Item(drawer, display_type) {};

		void applyStyles(StyleList *styles) override;
		void layout(Dim size, Pos offset) override;
		void draw(const core::rect<s32> &clip_rect, const Pos &offset) override;

		virtual Dim getImageSize();

		std::string texture_name;
		Dim wanteddim;
	};

	Item *newImage(ParsedText::AttrsList attrs, DisplayType display_type);

/*
	struct ItemImage : Image {
		ItemImage();
		v3s16 angle{0, 0, 0};
		v3s16 rotation{0, 0, 0};
	};
*/


	Item *newItem(std::string tagName, ParsedText::AttrsList attrs);
	void create(ParsedText::Node *node);
	void adjustLine(s32 &y, s32 &left, s32 &right, Pos offset);

	video::IVideoDriver *getVideoDriver() { return m_videodriver; };
	ITextureSource*  getTextureSource()  { return m_texturesource; };
	IItemDefManager* getItemDefManager() { return m_itemdefmanager; };

	video::IVideoDriver *m_videodriver;
	ITextureSource *m_texturesource;
	IItemDefManager *m_itemdefmanager;

	Container *m_root;
	Container *m_current_parent;
	StyleList m_current_style;

	// In this version, there is only one floating context : root item.
	// This vector is filled during layout with placed floating items.
	std::vector<Item *> m_placed_floating_items;
	std::deque<Item *> m_floating_items_to_place;

	s32 m_voffset = 0;
};
