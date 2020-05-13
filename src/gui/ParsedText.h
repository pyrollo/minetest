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
#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include "irrlichttypes_bloated.h"
#include "irrString.h"

class ParsedText
{
public:
	typedef std::unordered_map<std::string, std::string> AttrsList;

	enum NodeType {
		NT_ELEMENT,
		NT_TEXT,
	};

	struct Node
	{
		Node(NodeType type) : type(type) {};
		virtual ~Node() = default;
		NodeType type;
	};

	struct TextFragment : Node {
		core::stringw text;
		bool omissible;
		TextFragment(bool omissible) : Node(NT_TEXT), text(L""), omissible(omissible) {};
	};

	struct Element : Node {
		std::string name;
		AttrsList attrs;
		std::vector<std::unique_ptr<Node>> children;

		Element(std::string name) : Node(NT_ELEMENT), name(name) {};
		Element(std::string name, AttrsList attrs) : Node(NT_ELEMENT), name(name), attrs(attrs) {};
	};

	ParsedText(core::stringw source, bool fragment_text);
	const Node *getNodes() { return &m_root; };

protected:
	void pushChar(const wchar_t c);
	void parseTag(const wchar_t *text, u32 &cursor);
	void parse(const wchar_t *text);

	bool m_fragment_text;

	Element m_root;
	std::vector<Element *> m_stack;
	TextFragment *m_current_text;
};
