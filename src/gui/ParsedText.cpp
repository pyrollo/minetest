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

#include "ParsedText.h"
#include <deque>
#include "util/string.h"


enum TagType
{
	TT_OPEN,
	TT_CLOSE,
	TT_STANDALONE
};

inline bool is_letter(const wchar_t c) {
	return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z');
}

inline bool is_number(const wchar_t c) {
	return (c >= L'0' && c <= L'9');
}

inline bool is_space(const wchar_t c) {
	return (c == L' ' || c == L'\t' || c == '\n' || c == '\r');
}

// If end of text is reached, tag is canceled and ignored
#define NEXT { c = text[cursor]; if (c == L'\0') return; cursor++; }

ParsedText::ParsedText(core::stringw source, bool fragment_text) :
	m_fragment_text(fragment_text), m_root("%root%")
{
	m_stack.push_back(&m_root);
	parse(source.c_str());
}

void ParsedText::pushChar(const wchar_t c) {
	bool omissible = false;
	if (m_fragment_text && m_current_text != nullptr) {
		omissible = is_space(c);
		if (m_current_text->omissible != omissible)
			m_current_text = nullptr;
	}

	if (m_current_text == nullptr) {
		m_stack.back()->children.emplace_back(new TextFragment(omissible));
		m_current_text = (TextFragment *)m_stack.back()->children.back().get();
	}
	m_current_text->text += c;
}

void ParsedText::parseTag(const wchar_t *text, u32 &cursor)
{
	wchar_t c;
	TagType type = TT_OPEN;
	std::string name = "";
	std::string attr_name;
	core::stringw attr_val;
	AttrsList attrs;

	NEXT;

	if (c == L'/') {
		type = TT_CLOSE;
		NEXT;
	}

	while (is_letter(c) || is_number(c)) {
		name += c;
		NEXT;
	}

	while (true) {
		// Check for end tag marks
		if (c == L'/' && text[cursor] == L'>') {
			if (type == TT_OPEN)
				type = TT_STANDALONE;
			else
				{} // How to react to tag like </AA/> ?
				// Should it be considered as malformed ?
			NEXT;
		}

		if (c == L'>')
			break;

		// Skip text until next letter (attribute name)
		if (!is_letter(c)) {
			NEXT;
			continue;
		}

		attr_name = "";

		// Get an attr name
		while (is_letter(c)) {
			attr_name += (char)c;
			NEXT;
		}

		// If attr name is followed by a = sign, then it is actally an attribute
		if (c == L'=') {
			NEXT;
			attr_val = L"";

			if (c == L'\"') {
				NEXT;
				while (c !=  L'\"') {
					attr_val += c;
					NEXT;
				}
			} else {
				while (!is_space(c) && c != L'>' && c != L'/') { // TODO: Is it ok to end value on / ? or should we check for /> ?
					attr_val += c;
					NEXT;
				}
			}
			attrs[attr_name] = stringw_to_utf8(attr_val);
		}
	}

	if (type == TT_OPEN || type == TT_STANDALONE) {
		m_stack.back()->children.emplace_back(new Element{name, attrs});

		// If openning then add element to current stack
		if (type == TT_OPEN)
			m_stack.push_back((Element *)m_stack.back()->children.back().get());

		m_current_text = nullptr;
	}

	if (type == TT_CLOSE) {
		bool exists = false;
		for (auto element : m_stack)
			if (element->name == name) {
				exists = true;
				break;
			}

		if (exists) {
			std::deque<Element *> reopen;

			while (m_stack.back()->name != name) {
				reopen.push_front(m_stack.back());
				m_stack.pop_back();
			}

			// Actually close the tag (remove it from current stack)
			m_stack.pop_back();

			// Reopen all element that has to be closed to keep a tree structure
			for (auto element : reopen) {
				m_stack.back()->children.emplace_back(new Element{element->name, element->attrs});
				m_stack.push_back((Element *)m_stack.back()->children.back().get());
			}
			m_current_text = nullptr;
		}
	}
}

void ParsedText::parse(const wchar_t *text)
{
	m_current_text = nullptr;

	wchar_t c, c2;
	u32 cursor = 0;
	m_current_text = nullptr;

	while ((c = text[cursor]) != L'\0') {
		cursor++;
		c2 = text[cursor];

		// Tag check
		if (c == L'<' && (is_letter(c2) ||
			(c2 == L'/' && is_letter(text[cursor+1])))) {
			parseTag(text, cursor);
			continue;
		}

		// Default behavior
		pushChar(c);
	}
}
