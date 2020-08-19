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

#include "TextDrawer.h"
#include "IGUIEnvironment.h"
#include "util/string.h"
#include "client/client.h"
#include "client/fontengine.h"
#include <typeinfo>
#include "IVideoDriver.h"

// Style list management

void lower(std::string str)
{
	for(size_t index=0;index < str.size();index++)
		str[index]=tolower(str[index]);
}

std::string TextDrawer::StyleList::get(std::string name)
{
	try {
		return m_styles.at(name);
	} catch (const std::out_of_range &e) {
		return "";
	}
};

void TextDrawer::StyleList::set(std::string name, std::string value)
{
	lower(value); // All values are lowercase
	m_styles[name] = value;
};

bool TextDrawer::StyleList::has(std::string name)
{
	try {
		std::string val = m_styles.at(name);
		return true;
	} catch (const std::out_of_range &e) {
		return false;
	}
};

bool TextDrawer::StyleList::isYes(std::string name)
{
	return is_yes(get(name));
};

void TextDrawer::StyleList::copyTo(StyleList &styles)
{
	for (auto style: m_styles)
		if (style.second != "")
			styles.set(style.first, style.second);
}

/*
bool check_color(const std::string &str)
{
	irr::video::SColor color;
	return parseColorString(str, color, false);
}

bool check_integer(const std::string &str)
{
	if (str.empty())
		return false;

	char *endptr = nullptr;
	strtol(str.c_str(), &endptr, 10);

	return *endptr == '\0';
}

ParsedText::Element *GUIHyperText::getElementAt(s32 X, s32 Y)
{
	core::position2d<s32> pos{X, Y};
	pos -= m_display_text_rect.UpperLeftCorner;
	pos -= m_text_scrollpos;
	return m_drawer.getElementAt(pos);
}

*/

bool TextDrawer::Item::tryPlaceFloating(s32 y, s32 &left, s32 &right, u32 itemswidth, Pos offset)
{
	layout(Dim(right - left - itemswidth, 0 /* ? */), Pos(offset.X + left, offset.Y + y));
	if (dim.Width > right - left - itemswidth /* +/- margins */)
		return false;

	// Can be placed on the line
	pos.Y = y + offset.Y; // Floating Y is relative to ROOT, i.e. absolute

	if (display_type == DT_FLOAT_RIGHT) {
		right -= dim.Width;
		pos.X = right + offset.X; // Floating X is relative to ROOT, i.e. absolute
	}

	if (display_type == DT_FLOAT_LEFT) {
		pos.X = left + offset.X; // Floating X is relative to ROOT, i.e. absolute
		left += dim.Width;
	}
	drawer->m_placed_floating_items.push_back(this);

	return true;
};

TextDrawer::TextFragment::TextFragment(
		TextDrawer *drawer,
		ParsedText::TextFragment *fragment):
	Item(drawer, DT_INLINE_BLOCK), font(nullptr), underline(false),
	strikethrough(false)
{
	if (fragment->omissible)
		display_type = DT_OMISSIBLE;
	text = fragment->text;
}

void TextDrawer::TextFragment::applyStyles(StyleList *styles) {
	underline = styles->isYes("underline");
	strikethrough = styles->isYes("strikethrough");

	video::SColor colorval;

	if (parseColorString(styles->get("color"), colorval, false))
		color = colorval;
//	if (parseColorString(style["hovercolor"], color, false))
//		this->hovercolor = color;

	unsigned int font_size = std::atoi(styles->get("fontsize").c_str());
	FontMode font_mode = FM_Standard;
	if (styles->get("fontstyle") == "mono")
		font_mode = FM_Mono;

	FontSpec spec(font_size, font_mode,
		styles->isYes("bold"), styles->isYes("italic"));

	font = g_fontengine->getFont(spec);

	if (!font)
		printf("No font found ! Size=%d, mode=%d, bold=%s, italic=%s\n",
				font_size, font_mode, styles->get("bold").c_str(),
				styles->get("italic").c_str());
}

void TextDrawer::TextFragment::layout(Dim size, Pos offset) {
	if (font) {
		dim.Width = font->getDimension(text.c_str()).Width;
		dim.Height = font->getDimension(L"Yy").Height;
#if USE_FREETYPE
		if (font->getType() == irr::gui::EGFT_CUSTOM) {
			baseline = dim.Height - 1 -
				((irr::gui::CGUITTFont *)font)->getAscender() / 64;
		}
#endif
	} else {
		dim.Width = 0;
		dim.Height = 0;
	}

	drawdim.Width = dim.Width;
	drawdim.Height = dim.Height;
}

void TextDrawer::TextFragment::draw(const core::rect<s32> &clip_rect,
		const Pos &offset)
{
	core::rect<s32> rect(pos + offset, dim);
	if (!rect.isRectCollided(clip_rect))
		return;

	if (!font)
		return;

	font->draw(text, rect, color, false, true, &clip_rect);

	if (underline && drawdim.Width) {
		s32 linepos = pos.Y + offset.Y + dim.Height - (baseline >> 1);

		core::rect<s32> linerect(
				pos.X + offset.X, linepos - (baseline >> 3) - 1,
				pos.X + offset.X + drawdim.Width, linepos + (baseline >> 3));

		drawer->getVideoDriver()->draw2DRectangle(color, linerect, &clip_rect);
	}
}

void TextDrawer::Container::applyStyles(StyleList *styles) {
	std::string val = styles->get("halign");
	if (val == "center")
		halign = HALIGN_CENTER;
	else if (val == "right")
		halign = HALIGN_RIGHT;
	else if (val == "justify")
		halign = HALIGN_JUSTIFY;
	else
		halign = HALIGN_LEFT;

	margin = 0;
}

void TextDrawer::Container::layout(Dim size, Pos offset) {
	// Here, we place words and inline content

	// Take not only direct children but all descendant in DT_INLINE elements
	std::vector<Item *> items;
	populatePlaceableChildren(&items);

	s32 y = 0;
	s32 left, right;
	std::vector<Item *>::iterator item = items.begin();

	while (item != items.end()) {
		// Line by line placing

		// Take in account floating items for line size and pos
		left = margin;
 		right = size.Width - margin;
		drawer->adjustLine(y, left, right, offset);

		// Place remaining floating items (those that could not be placed yet)
		while (drawer->m_floating_items_to_place.size() > 0) {
			auto floating = drawer->m_floating_items_to_place.front();
			if (!floating->tryPlaceFloating(y, left, right, 0, offset))
				// Wont fit this time, try later
				break;

			drawer->m_floating_items_to_place.pop_front();
		}

		u32 lineheight = 0; // Calulated line height (including all invisible fragments)

		// Skip beginning of line separators but include them in height
		// computation.
		while (item != items.end() && (*item)->display_type == DT_OMISSIBLE) {
			(*item)->drawdim.Width = 0;
			if (lineheight < (*item)->dim.Height)
				lineheight = (*item)->dim.Height;
			item++;
		}

		std::vector<Item *>::iterator linestart = item;
		std::vector<Item *>::iterator lineend = item;

		// First pass, find elements fitting into line
		// (or at least one element)
		u32 itemswidth = 0; // Total displayed item width (used for justification)
		bool eol = false;

		while (!eol && item != items.end()) {
			switch((*item)->display_type) {

			// Out of line blocks
			case DT_BLOCK:
				// If stuff already on this line, end line at this time,
				// and place block next time
				if (itemswidth == 0) {
					(*item)->layout(Dim(right - left, size.Height), Pos(left, y));
					lineheight = (*item)->dim.Height; // On this line: only this block
					item++;
				}
				eol = true;
				break;

			// Floating blocks
			case DT_FLOAT_RIGHT:
			case DT_FLOAT_LEFT:
				if (!(*item)->tryPlaceFloating(y, left, right, itemswidth, offset))
					drawer->m_floating_items_to_place.push_back((*item));
				item++;
				break;

			// Inline stuff
			case DT_OMISSIBLE:
			case DT_INLINE_BLOCK:
				(*item)->layout(Dim(right - left, size.Height), Pos(left, y));

				if (itemswidth > 0 && itemswidth + (*item)->dim.Width > size.Width) {
					eol = true;
					break; // No more room on this line
				}

				itemswidth += (*item)->dim.Width;

				if ((*item)->display_type == DT_INLINE_BLOCK)
					lineend = item;

				if (lineheight < (*item)->dim.Height)
					lineheight = (*item)->dim.Height;

				item++;
				break;

			// Not managed items (should not occur)
			default:
				item++;
			}
		}

		lineend++;

		// Second pass, compute printable line width and adjustments
		u32 spacecount = 0;
		itemswidth = 0;
		u32 top = 0;
		u32 bottom = 0;

		// Adjust top & bottom according to baseline
		// + compute actual itemswidth
		for (auto it = linestart; it != lineend; ++it) {
			if ((*it)->display_type == DT_OMISSIBLE ||
					(*it)->display_type == DT_BLOCK ||
					(*it)->display_type == DT_INLINE_BLOCK) {
				itemswidth += (*it)->dim.Width;
				if (top < (*it)->dim.Height - (*it)->baseline)
					top = (*it)->dim.Height - (*it)->baseline;
				if (bottom < (*it)->baseline)
					bottom = (*it)->baseline;
				if ((*it)->display_type == DT_OMISSIBLE)
					spacecount++;
			}
		}

		// Horizontal align
		float extraspace = 0.f;
		float x = 0.f;

		if (halign == HALIGN_CENTER)
			x = (right - left - itemswidth) / 2.f;

		if (halign == HALIGN_JUSTIFY &&
				spacecount > 0 && // Justification only if at least two items
				!(lineend == items.end())) // Don't justify last line
			extraspace = ((float)(right - left - itemswidth)) / spacecount;

		if (halign == HALIGN_RIGHT)
			x = right - left - itemswidth;

		// Third pass, actually place everything
		for (auto it = linestart; it != lineend; ++it) {
			if ((*it)->display_type == DT_OMISSIBLE ||
					(*it)->display_type == DT_BLOCK ||
					(*it)->display_type == DT_INLINE_BLOCK) {
				(*it)->drawdim.Width = (*it)->dim.Width;

				(*it)->pos.X = x;

				x += (*it)->dim.Width;

				if ((*it)->display_type == DT_OMISSIBLE) {
					spacecount--;
					x += extraspace;
					(*it)->drawdim.Width = (u32)x - (*it)->pos.X;
				}

				(*it)->pos.Y = y + top + (*it)->baseline - (*it)->dim.Height;
			}
		}

		// Next line
		y += lineheight;
		// y+= margin.bottom;
	} // End of line

	dim.Height = y;
	dim.Width = size.Width; // Possibility to reduce it to block size if no text
}

void TextDrawer::Container::draw(const core::rect<s32> &clip_rect,
		const Pos &offset)
{
	Pos newoffset = pos + offset;

	core::rect<s32> rect(newoffset, dim);

	// DT_INLINE is not really placed but has drawable child
	if (display_type != DT_INLINE && !rect.isRectCollided(clip_rect))
		return;

	for (auto &item: children)
		item->draw(clip_rect, newoffset);
}

void TextDrawer::Container::populatePlaceableChildren(std::vector<Item *> *placeablechildren)
{
	for (auto &child: children)
		if (child->display_type == DT_INLINE)
			child->populatePlaceableChildren(placeablechildren);
		else
			placeablechildren->push_back(child.get());
}

void TextDrawer::Image::applyStyles(StyleList *styles) {
	/*

					// Rotate attribute is only for <item>
					if (attrs.count("rotate") && name != "item")
					// Angle attribute is only for <item>
					if (attrs.count("angle") && name != "item")
						return 0;
	*/
}

void TextDrawer::Image::layout(Dim size, Pos offset) {
	if (wanteddim.Width > 0 && wanteddim.Height > 0) {
		dim = wanteddim;
		return;
	}

	dim = getImageSize();

	if (wanteddim.Width > 0) {
		dim.Height = dim.Height * wanteddim.Width / dim.Width;
		dim.Width = wanteddim.Width;
	}

	if (wanteddim.Height > 0) {
		dim.Width = dim.Width * wanteddim.Height / dim.Height;
		dim.Height = wanteddim.Height;
	}
}

void TextDrawer::Image::draw(const core::rect<s32> &clip_rect,
		const Pos &offset) {

	video::ITexture *texture =
		drawer->getTextureSource()->getTexture(texture_name);

	if (texture != 0)
		drawer->getVideoDriver()->draw2DImage(
				texture, core::rect<s32>(pos + offset, dim),
				irr::core::rect<s32>(
						core::position2d<s32>(0, 0),
						texture->getOriginalSize()),
				&clip_rect, 0, true);
}

Dim TextDrawer::Image::getImageSize() {
	video::ITexture *texture =
		drawer->getTextureSource()->getTexture(texture_name);
	if (texture)
		return texture->getOriginalSize();
	else
		return Dim(80, 80);
}

// =============================================================================
//
// TextDrawer class
//
// =============================================================================

TextDrawer::TextDrawer(core::stringw source, gui::IGUIEnvironment *environment,
		Client *client):
	m_videodriver(environment->getVideoDriver()),
	m_texturesource(client->getTextureSource()),
	m_itemdefmanager(client->idef())
{
	// Default styles
	m_current_style.set("underline", "no");
	m_current_style.set("bold",      "no");
	m_current_style.set("italic",    "no");
	m_current_style.set("halign",    "left");
	m_current_style.set("color",     "#EEEEEE");
	m_current_style.set("fontsize",  "20");
	m_current_style.set("fontstyle", "normal");

	m_root = new Container(this, DT_ROOT);
	m_root->applyStyles(&m_current_style);
	m_current_parent = m_root;

	ParsedText parsed_text(source, true);

	for (auto &node : ((ParsedText::Element *)parsed_text.getNodes())->children)
		create(node.get());
}

// Nodes to item conversion
void TextDrawer::create(ParsedText::Node *node)
{
	if (node->type == ParsedText::NT_TEXT) {
		Item *item = (Item *)new TextFragment(this, (ParsedText::TextFragment *)node);
		item->applyStyles(&m_current_style);
		m_current_parent->children.emplace_back(item);
		return;
	}

	if (node->type != ParsedText::NT_ELEMENT)
		return;

	// Save parent context
	Container *old_parent = m_current_parent;
	StyleList old_style = m_current_style;

	ParsedText::Element *element = (ParsedText::Element *)node;

	Item *item = newItem(element->name, element->attrs);
	if (item != nullptr) {
		m_current_parent->children.emplace_back(item);
		try
		{
			m_current_parent = dynamic_cast<Container *>(item);
		} catch (const std::bad_cast& e) {} // Not a container -- Skip change parent
	}

	for (auto &node : element->children)
		create(node.get());

	// Restore parent context
	m_current_style = old_style;
	m_current_parent = old_parent;
}

// Items creation
TextDrawer::Item *TextDrawer::newItem(std::string tagName, ParsedText::AttrsList attrs)
{
	Item *item = nullptr;
	std::unordered_map<std::string, std::string> styles;

	// Hardcoded tags
	if (tagName == "p") {
		item = newParagraph(attrs, DT_BLOCK);
	}

	if (tagName == "img") {
		item = newImage(attrs, DT_BLOCK);
	}

	if (tagName == "b") {
		item = newContainer(attrs, DT_INLINE);
		styles["bold"] = "yes";
	}

	if (tagName == "i") {
		item = newContainer(attrs, DT_INLINE);
		styles["italic"] = "yes";
	}

	if (tagName == "u") {
		item = newContainer(attrs, DT_INLINE);
		styles["underline"] = "yes";
	}

	if (item == nullptr)
		return item;

	// Change current style
	for (auto style: styles)
		if (style.second != "")
			m_current_style.set(style.first, style.second);

	// Apply current style
	item->applyStyles(&m_current_style);

	return item;
}

TextDrawer::Item *TextDrawer::newContainer(ParsedText::AttrsList attrs,	DisplayType display_type)
{
	return (Item *)new Container(this, display_type);
}

TextDrawer::Item *TextDrawer::newParagraph(ParsedText::AttrsList attrs, DisplayType display_type)
{
	Container* item = (Container *)newContainer(attrs, display_type);
	if (attrs.count("align") > 0)
		m_current_style.set("halign", attrs["align"]);
	return (Item *)item;
}


/*
struct ItemImage : Image {
	ItemImage();
	v3s16 angle{0, 0, 0};
	v3s16 rotation{0, 0, 0};
};
*/

TextDrawer::Item *TextDrawer::newImage(ParsedText::AttrsList attrs, DisplayType display_type)
{
	Image *item = new Image(this, DT_INLINE_BLOCK);
	if (!attrs.count("texture"))
		return nullptr;

	item->texture_name = attrs["texture"];
	item->wanteddim = Dim(0, 0);

	if (attrs.count("width")) {
		int width = stoi(attrs["width"]);
		if (width > 0)
			item->wanteddim.Width = width;
	}

	if (attrs.count("height")) {
		int height = stoi(attrs["height"]);
		if (height > 0)
			item->wanteddim.Height = height;
	}

	if (attrs.count("float")) {
		if (attrs["float"] == "left")
			item->display_type = DT_FLOAT_LEFT;
		if (attrs["float"] == "right")
			item->display_type = DT_FLOAT_RIGHT;
	}

	return (Item *)item;
}

void TextDrawer::layout(const core::rect<s32> &rect)
{
	printf("Layout (%d, %d)\n", rect.getWidth(), rect.getHeight());
	m_placed_floating_items.clear();
	m_root->layout(Dim(rect.getWidth(), rect.getHeight()), Pos(0, 0));
}

u32 TextDrawer::getHeight()
{
	return m_root->dim.Height;
}


void TextDrawer::draw(const core::rect<s32> &clip_rect, const Pos &offset)
{
	Pos newoffset = offset;
	newoffset.Y += m_voffset; // For scrolling
/*
	if (m_text.background_type == ParsedText::BACKGROUND_COLOR)
		driver->draw2DRectangle(m_text.background_color, clip_rect);
*/
	m_root->draw(clip_rect, newoffset);
}

// PROBLEM: here we need absolute positon but caller does not know its final offset

// Adjust line according to floating items. In this version, floating context
// is always root element. Floating items can only appear on or after cursor
// line. So we don't need to care about line height.
// y and left may be increased as needed, and right may be decreased as needed
void TextDrawer::adjustLine(s32 &y, s32 &left, s32 &right, Pos offset) {
	s32 l = left;
	s32 r = right;
printf("Before y=%d left=%d right=%d\n", y, left, right);
	for (auto &item: m_placed_floating_items) {
		printf("Pos (%d, %d) size (%d, %d)\n", item->pos.X, item->pos.Y, item->dim.Width, item->dim.Height);
		// Exclude bottom boundary to avoid infinite loops
		if (y + offset.Y >= item->pos.Y &&
				y + offset.Y < item->pos.Y + (s32)item->dim.Height) /* + margins */
		{
			// Push right limit not to overlap
			if (r + offset.X >= item->pos.X &&
					r + offset.X <= item->pos.X + (s32)item->dim.Width)
				r = item->pos.X;

			// Push left limit not to overlap
			if (l + offset.X >= item->pos.X &&
					l + offset.X <= item->pos.X + (s32)item->dim.Width)
				l = item->pos.X + item->dim.Width;

			// No place left on this Y
			if (l >= r) {
				y = item->pos.Y + item->dim.Height - offset.Y; // Pass the item and try again
				adjustLine(y, left, right, offset);
				return;
			}
		}
	}
	left = l;
	right = r;
printf("After y=%d left=%d right=%d\n", y, left, right);
}
