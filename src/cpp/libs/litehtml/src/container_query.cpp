#include "html.h"
#include "container_query.h"
#include "css_parser.h"
#include "document.h"
#include "html_tag.h"
#include "render_item.h"

namespace litehtml
{

bool container_query::check(const container_features& features) const
{
	if (!m_condition) return true;
	
	// We need to map container_features to media_features to reuse media_condition::check
	media_features mf;
	mf.width = features.width;
	mf.height = features.height;
	// Other features don't apply to containers
	
	return m_condition->check(mf) == True;
}

bool container_query_list::check(const container_features& features) const
{
	if (empty()) return true;
	for (const auto& query : m_queries)
	{
		if (query.check(features)) return true;
	}
	return false;
}

bool container_query_list_list::check(const html_tag* el) const
{
	if (m_container_query_lists.empty()) return true;

	for (const auto& cq_list : m_container_query_lists)
	{
		bool list_match = false;
		for (const auto& query : cq_list.m_queries)
		{
			// Find matching container ancestor
			element::ptr parent = el->parent();
			bool found = false;
			while (parent)
			{
				const auto& css = parent->css();
				if (css.get_container_type() != container_type_none)
				{
					if (query.m_name.empty() || query.m_name == css.get_container_name())
					{
						container_features feat;
						auto ri = parent->get_render_item();
						if (ri)
						{
							// pos() is already the content box size in this engine
							// (flex_item.cpp reads it as the content size), so the
							// paddings and borders must not come off a second time:
							// a 448px-content container failed a (min-width: 448px)
							// query by exactly its horizontal padding.
							feat.width = ri->pos().width;
							feat.height = ri->pos().height;
						}
						else
						{
							feat.width = 0;
							feat.height = 0;
						}
						
						if (query.check(feat))
						{
							list_match = true;
						}
						found = true;
						break;
					}
				}
				parent = parent->parent();
			}
			if (list_match) break;
			
			if (!found && !query.m_condition) 
			{
				// Bare @container matches if found
				// (Shouldn't really happen with valid queries)
			}
		}
		
		if (!list_match) return false;
	}

	return true;
}

// These are defined in media_query.cpp but not exported. 
// For now, I will implement a simplified version or reuse what's possible.
// Ideally, we should refactor media_query.cpp to export parsing functions for conditions.

media_query_list parse_media_query_list(const css_token_vector& tokens, shared_ptr<document> doc);

// Container size features are the logical inline-size / block-size; the media feature parser only knows
// width / height, so rename them (horizontal writing mode: inline = width, block = height).
static void rename_logical_size_features(css_token_vector& tokens)
{
	for (auto& t : tokens)
	{
		if (t.type == IDENT)
		{
			const string id = t.ident();
			const string prefix = (id.compare(0, 4, "min-") == 0 || id.compare(0, 4, "max-") == 0) ? id.substr(0, 4) : "";
			const string base = id.substr(prefix.size());
			if (base == "inline-size") t.name = prefix + "width";
			else if (base == "block-size") t.name = prefix + "height";
		}
		else if (t.is_component_value())
		{
			rename_logical_size_features(t.value);
		}
	}
}

container_query_list parse_container_query_list(const css_token_vector& _tokens, shared_ptr<document> doc)
{
	css_token_vector tokens = normalize(_tokens, f_componentize | f_remove_whitespace);
	if (tokens.empty()) return {};
	rename_logical_size_features(tokens);

	container_query_list result;
	
	// @container <name>? <condition>, where the condition may itself start with
	// `not` (a media-not) -- the form Tailwind v4 compiles every @max-* container
	// variant to: `@container not (min-width: 64rem)`. The leading ident is the
	// container name only when it is not a query keyword: taking `not` as a name
	// made every negated query silently never match (no container is named "not").
	container_query query;
	int index = 0;
	if (tokens[index].type == IDENT && !is_one_of(tokens[index].ident(), "not", "only"))
	{
		query.m_name = tokens[index].ident();
		index++;
	}

	if (index < (int)tokens.size())
	{
		// Reuse the media query parser for the whole remainder, so a leading
		// `not`, `and` chains and range forms behave exactly as in @media.
		css_token_vector rest(tokens.begin() + index, tokens.end());
		media_query_list mql = parse_media_query_list(rest, doc);
		if (!mql.m_queries.empty() && !mql.m_queries[0].m_conditions.empty())
		{
			query.m_condition = make_shared<media_condition>(mql.m_queries[0].m_conditions[0]);
		}
	}
	
	result.m_queries.push_back(query);
	return result;
}

} // namespace litehtml
