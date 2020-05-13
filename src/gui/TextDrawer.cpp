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
#include "util/string.h"
#include "client/fontengine.h"
#include <typeinfo>

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

TextDrawer::TextFragment::TextFragment(ParsedText::TextFragment *fragment) :
	Item(DT_INLINE), font(nullptr), underline(false), strikethrough(false)
{
	if (fragment->omissible)
		display_type = DT_OMISSIBLE;
	text = fragment->text;
}

void TextDrawer::TextFragment::applyStyles(StyleList styles) {
	underline = styles.isYes("underline");
	strikethrough = styles.isYes("strikethrough");

	video::SColor colorval;

	if (parseColorString(styles.get("color"), colorval, false))
		color = colorval;
//	if (parseColorString(style["hovercolor"], color, false))
//		this->hovercolor = color;

	unsigned int font_size = std::atoi(styles.get("fontsize").c_str());
	FontMode font_mode = FM_Standard;
	if (styles.get("fontstyle") == "mono")
		font_mode = FM_Mono;

	FontSpec spec(font_size, font_mode,
		styles.isYes("bold"), styles.isYes("italic"));

	font = g_fontengine->getFont(spec);

	if (!font)
		printf("No font found ! Size=%d, mode=%d, bold=%s, italic=%s\n",
				font_size, font_mode, styles.get("bold").c_str(),
				styles.get("italic").c_str());
}

void TextDrawer::TextFragment::layout(Dim size) {
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

void TextDrawer::TextFragment::draw(irr::video::IVideoDriver *driver,
	const core::rect<s32> &clip_rect, const Pos &offset)
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

		driver->draw2DRectangle(color, linerect, &clip_rect);
	}
}

void TextDrawer::Container::applyStyles(StyleList styles) {
	std::string val = styles.get("halign");
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

void TextDrawer::Container::layout(Dim size) {
	// Here, we place words and inline content

	// Take not only direct children but all descendant in DT_STYLE elements
	std::vector<Item *> items;
	populatePlaceableChildren(&items);
	float y = 0;

	std::vector<Item *>::iterator item = items.begin();

	while (item != items.end()) {

		// Line by line placing
		// TODO:Check size.Width > margin *2
		u32 linewidth = size.Width - margin * 2; // Maximum line width
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

		while (item != items.end()) {
			if ((*item)->display_type == DT_BLOC) {
				// Ends ongoing line.
				if (itemswidth > 0)
					break;

				// Line containing this block
				(*item)->layout(Dim(linewidth, size.Height));
				lineheight = (*item)->dim.Height;
				item++;
				lineend = item;
				break;
			}

			if ((*item)->display_type == DT_OMISSIBLE ||
					(*item)->display_type == DT_INLINE) {

				(*item)->layout(Dim(linewidth, size.Height));

				if (itemswidth > 0 && itemswidth + (*item)->dim.Width > size.Width)
					break; // No more room on this line

				itemswidth += (*item)->dim.Width;

				if ((*item)->display_type == DT_INLINE)
					lineend = item;

				if (lineheight < (*item)->dim.Height)
					lineheight = (*item)->dim.Height;

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
		for (auto it = linestart; it != lineend; ++it) {
			itemswidth += (*it)->dim.Width;
			if ((*it)->display_type == DT_OMISSIBLE)
				spacecount++;
			if (top < (*it)->dim.Height - (*it)->baseline)
				top = (*it)->dim.Height - (*it)->baseline;
			if (bottom < (*it)->baseline)
				bottom = (*it)->baseline;
		}

		// Horizontal align
		float extraspace = 0.f;
		float x = 0.f;

		if (halign == HALIGN_CENTER)
			x = (linewidth - itemswidth) / 2.f;

		if (halign == HALIGN_JUSTIFY &&
				spacecount > 0 && // Justification only if at least two items
				!(lineend == items.end())) // Don't justify last line
			extraspace = ((float)(linewidth - itemswidth)) / spacecount;

		if (halign == HALIGN_RIGHT)
			x = linewidth - itemswidth;

		// Third pass, actually place everything
		for (auto it = linestart; it != lineend; ++it) {
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

		// Next line
		y += lineheight;
		// y+= margin.bottom;
	} // End of line

	dim.Height = y;
	dim.Width = size.Width; // Possibility to reduce it to block size if no text
}

void TextDrawer::Container::draw(irr::video::IVideoDriver *driver,
	const core::rect<s32> &clip_rect, const Pos &offset) {

	Pos newoffset = pos + offset;

	core::rect<s32> rect(newoffset, dim);

	// DT_STYLE is not really placed but has drawable child
	if (display_type != DT_STYLE && !rect.isRectCollided(clip_rect))
		return;

	for (auto &item: children)
		item->draw(driver, clip_rect, newoffset);
}

void TextDrawer::Container::populatePlaceableChildren(std::vector<Item *> *placeablechildren)
{
	for (auto &child: children)
		if (child->display_type == DT_STYLE)
			child->populatePlaceableChildren(placeablechildren);
		else
			placeablechildren->push_back(child.get());
}

TextDrawer::TextDrawer(core::stringw source)
{
	initItemDef();

	m_current_style.set("underline", "no");
	m_current_style.set("bold",      "no");
	m_current_style.set("italic",    "no");
	m_current_style.set("halign",    "left");
	m_current_style.set("color",     "#EEEEEE");
	m_current_style.set("fontsize",  "20");
	m_current_style.set("fontstyle", "normal");

	m_root = new Container(DT_ROOT);
	m_root->applyStyles(m_current_style);
	m_current_parent = m_root;

	ParsedText parsed_text(source, true);

	for (auto &node : ((ParsedText::Element *)parsed_text.getNodes())->children)
		create(node.get());
}

void TextDrawer::initItemDef() {
	itemDefs.emplace("p", (ItemDef *)new ItemDefParagraph());
	itemDefs["p"]->display_type = DT_BLOC;

	itemDefs.emplace("u", (ItemDef *)new ItemDefContainer());
	itemDefs["u"]->display_type = DT_STYLE;
	itemDefs["u"]->styles.set("underline", "yes");

	itemDefs.emplace("b", (ItemDef *)new ItemDefContainer());
	itemDefs["b"]->display_type = DT_STYLE;
	itemDefs["b"]->styles.set("bold", "yes");

	itemDefs.emplace("i", (ItemDef *)new ItemDefContainer());
	itemDefs["i"]->display_type = DT_STYLE;
	itemDefs["i"]->styles.set("italic", "yes");
}

TextDrawer::ItemDef * TextDrawer::getItemDef(std::string item_name) {
	try {
		return itemDefs.at(item_name).get();
	} catch(std::out_of_range &e) {
		return nullptr;
	}
}

void TextDrawer::create(ParsedText::Node *node)
{
	if (node->type == ParsedText::NT_TEXT) {
		Item *item = (Item *)new TextFragment((ParsedText::TextFragment *)node);
		item->applyStyles(m_current_style);
		m_current_parent->children.emplace_back(item);
		return;
	}

	if (node->type != ParsedText::NT_ELEMENT)
		return;

	// Save parent context
	Container *old_parent = m_current_parent;
	StyleList old_style = m_current_style;

	ParsedText::Element *element = (ParsedText::Element *)node;

	ItemDef *itemDef = getItemDef(element->name);

	if (itemDef != nullptr) {
		// Override styles
		itemDef->styles.copyTo(m_current_style);
		Item *item = itemDef->createItem(element->attrs, &m_current_style);
		item->applyStyles(m_current_style);

		m_current_parent->children.emplace_back(item);
		try
		{
			m_current_parent = dynamic_cast<Container *>(item);
		}
		catch (const std::bad_cast& e) {}
	}

	for (auto &node : element->children)
		create(node.get());

	// Restore parent context
	m_current_style = old_style;
	m_current_parent = old_parent;
}

void TextDrawer::layout(const core::rect<s32> &rect)
{
	m_root->layout(Dim(rect.getWidth(), rect.getHeight()));
}

u32 TextDrawer::getHeight()
{
	return m_root->dim.Height;
}

void TextDrawer::draw(irr::video::IVideoDriver *driver,
		const core::rect<s32> &clip_rect, const Pos &offset)
{
	Pos newoffset = offset;
	newoffset.Y += m_voffset; // For scrolling
/*
	if (m_text.background_type == ParsedText::BACKGROUND_COLOR)
		driver->draw2DRectangle(m_text.background_color, clip_rect);
*/
//printf(">>>DRAW\n");
	m_root->draw(driver, clip_rect, newoffset);
//printf("<<<DRAW\n");
}
