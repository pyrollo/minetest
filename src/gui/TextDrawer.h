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
#include "irrlichttypes_bloated.h"
#include "irrString.h"
using namespace irr;
#include "ParsedText.h"
#include "util/string.h"
#include "config.h" // USE_FREETYPE
#if USE_FREETYPE
#include "irrlicht_changes/CGUITTFont.h"
#endif

typedef core::dimension2d<u32> Dim;
typedef core::position2d<s32> Pos;

class TextDrawer
{
public:
	TextDrawer(core::stringw source);
	void layout(const core::rect<s32> &rect);
	void draw(irr::video::IVideoDriver *driver,
			const core::rect<s32> &clip_rect,
			const Pos &offset);
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
	//	DT_FLOAT_LEFT,
	//	DT_FLOAT_RIGHT,
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
		Item(DisplayType display_type): display_type(display_type), dim(0, 0),
			drawdim(0, 0), pos(0, 0) {};

		virtual void applyStyles(StyleList styles) = 0; // Use only styles that applies to this bloc
		virtual void layout(Dim size) = 0;
		virtual void draw(irr::video::IVideoDriver *driver,
			const core::rect<s32> &clip_rect, const Pos &offset) = 0;
		virtual void populatePlaceableChildren(std::vector<Item *> *children) {};

		DisplayType display_type;

		// Effective size and pos
		Dim dim;
		Dim drawdim;
		Pos pos;
		u32 baseline = 0;
	};

	struct TextFragment : Item {
		TextFragment(ParsedText::TextFragment *fragment);

		void applyStyles(StyleList styles) override;
		void layout(Dim size) override;
		void draw(irr::video::IVideoDriver *driver,
			const core::rect<s32> &clip_rect, const Pos &offset) override;

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
		Container(DisplayType display_type) : Item(display_type) {};
		void applyStyles(StyleList styles) override;
		void layout(Dim size) override;
		void draw(irr::video::IVideoDriver *driver,
			const core::rect<s32> &clip_rect, const Pos &offset) override;
		void populatePlaceableChildren(std::vector<Item *> *children) override;

		// Container specific
		std::vector<std::unique_ptr<Item>> children;

		// Styles
		HalignType halign;
		int margin;
	};

	struct ItemFactory {
		ItemFactory(DisplayType display_type, std::map<std::string, std::string> styles):
			m_display_type(display_type), m_styles(styles) {};

		Item *newItem(ParsedText::AttrsList attrs, StyleList *current_styles);

		virtual Item *create(ParsedText::AttrsList attrs, StyleList *current_styles) = 0;

		DisplayType m_display_type;
		std::map<std::string, std::string> m_styles;
	};

	struct ContainerFactory : ItemFactory {
		ContainerFactory(DisplayType display_type, std::map<std::string, std::string> styles):
			ItemFactory(display_type, styles) {};
		Item *create(ParsedText::AttrsList attrs, StyleList *current_styles) override {
			return (Item *)new Container(m_display_type);
		};
	};

	struct ParagraphFactory : ContainerFactory {
		ParagraphFactory(DisplayType display_type, std::map<std::string, std::string> styles):
			ContainerFactory(display_type, styles) {};

		Item *create(ParsedText::AttrsList attrs, StyleList *current_styles) override {
			Container* item = (Container *)ContainerFactory::create(attrs, current_styles);
			if (attrs.count("align") > 0)
				current_styles->set("halign", attrs["align"]);
			return (Item *)item;
		};
	};

	struct Image : Item {
		Image(DisplayType display_type) : Item(display_type) {};

		void applyStyles(StyleList styles) override {};
		void layout(Dim size) override {};
		void draw(irr::video::IVideoDriver *driver,
			const core::rect<s32> &clip_rect, const Pos &offset) override {};

		core::stringw texture_name;
	};
/*
	struct ItemImage : Image {
		ItemImage();
		v3s16 angle{0, 0, 0};
		v3s16 rotation{0, 0, 0};
	};
*/
	struct ImageFactory: ItemFactory {
		Item *create(ParsedText::AttrsList attrs, StyleList *styles) override
		{
			Image *item = new Image(m_display_type);
			if (!attrs.count("name"))
				return nullptr;

			item->texture_name = utf8_to_stringw(attrs["name"]);

			if (attrs.count("width")) {
				int width = stoi(attrs["width"]);
				if (width > 0)
					item->dim.Width = width;
			}

			if (attrs.count("height")) {
				int height = stoi(attrs["height"]);
				if (height > 0)
					item->dim.Height = height;
			}

			return (Item *)item;
		};
	};
/*
	struct ImageItemFactory : ImageFactory {
		Item *create(ParsedText::AttrsList attrs, StyleList *styles) override
		{

		}

	}

				// Rotate attribute is only for <item>
				if (attrs.count("rotate") && name != "item")
				// Angle attribute is only for <item>
				if (attrs.count("angle") && name != "item")
					return 0;
*/
	void initDefaultStyle();
	void initItemFactories();
	void create(ParsedText::Node *node);

	Container *m_root;
	Container *m_current_parent;
	StyleList m_current_style;

	s32 m_voffset = 0;

	std::map<std::string, std::unique_ptr<ItemFactory>> itemFactories;

};
