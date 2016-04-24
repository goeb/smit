/*   Small Issue Tracker
 *   Copyright (C) 2013 Frederic Hoerni
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License v2 as published by
 *   the Free Software Foundation.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */
#include "config.h"

#include "View.h"
#include "global.h"
#include "utils/logging.h"
#include "utils/parseConfig.h"
#include "utils/filesystem.h"

/** @param filter
  *     [ "version:v1.0", "version:1.0", "owner:John Doe" ]
  */
std::map<std::string, std::list<std::string> > parseFilter(const std::list<std::string> &filters)
{
    std::map<std::string, std::list<std::string> > result;
    std::list<std::string>::const_iterator i;
    for (i = filters.begin(); i != filters.end(); i++) {
        // split apart from the first colon
        size_t colon = (*i).find_first_of(":");
        std::string propertyName = (*i).substr(0, colon);
        std::string propertyValue = "";
        if (colon != std::string::npos && colon < (*i).size()-1) propertyValue = (*i).substr(colon+1);

        if (result.find(propertyName) == result.end()) result[propertyName] = std::list<std::string>();

        result[propertyName].push_back(propertyValue);
    }

    return result;
}

/** Parse tokens into a list of views
  *
  * @param[out] views
  */
void PredefinedView::parsePredefinedViews(std::list<std::list<std::string> > lines, std::map<std::string, PredefinedView> &views)
{
    std::list<std::list<std::string> >::iterator line;
    std::string token;
    FOREACH(line, lines) {
        token = pop(*line);
        if (token.empty()) continue;

        if (token == K_SMIT_VERSION) {
            std::string v = pop(*line);
            LOG_DEBUG("Smit version of view: %s", v.c_str());

        } else if (token == "addView") {
            PredefinedView pv;
            pv.name = pop(*line);
            if (pv.name.empty()) {
                LOG_ERROR("parsePredefinedViews: Empty view name. Skip.");
                continue;
            }

            while (! line->empty()) {
                token = pop(*line);
                if (token == "filterin" || token == "filterout") {
                    if (line->size() < 2) {
                        LOG_ERROR("parsePredefinedViews: Empty property or value for filterin/out");
                        pv.name.clear(); // invalidate this line
                        break;
                    }
                    std::string property = pop(*line);
                    std::string value = pop(*line);
                    if (token == "filterin") pv.filterin[property].push_back(value);
                    else pv.filterout[property].push_back(value);

                } else if (token == "default") {
                    pv.isDefault = true;

                } else if (line->size() == 0) {
                    LOG_ERROR("parsePredefinedViews: missing parameter after %s", token.c_str());
                    pv.name.clear(); // invalidate this line
                    break;

                } else if (token == "colspec") {
                    pv.colspec = pop(*line);

                } else if (token == "sort") {
                    pv.sort = pop(*line);

                } else if (token == "search") {
                    pv.search = pop(*line);

                } else {
                    LOG_ERROR("parsePredefinedViews: Unexpected token %s", token.c_str());
                    pv.name.clear();
                }
            }
            if (! pv.name.empty()) views[pv.name] = pv;

        } else {
            LOG_ERROR("parsePredefinedViews: Unexpected token %s", token.c_str());
        }
    }
}

std::string PredefinedView::getDirectionName(bool d)
{
    return d?_("Ascending"):_("Descending");
}

std::string PredefinedView::getDirectionSign(const std::string &text)
{
    if (text == _("Ascending")) return "+";
    else return "-";
}


/** Generate a query string
  *
  * Example: filterin=x:y&filterout=a:b&search=ss&sort=s22&colspec=a+b+c
  */
std::string PredefinedView::generateQueryString() const
{
    std::string qs = "";
    if (!search.empty()) qs += "search=" + urlEncode(search) + '&';
    if (!sort.empty()) qs += "sort=" + sort + '&';
    if (!colspec.empty()) qs += "colspec=" + colspec + '&';
    std::map<std::string, std::list<std::string> >::const_iterator f;
    FOREACH(f, filterin) {
        std::list<std::string>::const_iterator v;
        FOREACH(v, f->second) {
            qs += "filterin=" + urlEncode(f->first) + ':' + urlEncode(*v) + '&';
        }
    }
    FOREACH(f, filterout) {
        std::list<std::string>::const_iterator v;
        FOREACH(v, f->second) {
            qs += "filterout=" + urlEncode(f->first) + ':' + urlEncode(*v) + '&';
        }
    }

    // remove latest &
    if (!qs.empty() && qs[qs.size()-1] == '&') qs = qs.substr(0, qs.size()-1);
    return qs;
}


/** Load parameters related to a view
  *
  * filterin/out, sort, search, colspec
  */
PredefinedView PredefinedView::loadFromQueryString(const std::string &q)
{
    std::list<std::string> filterinRaw = getParamListFromQueryString(q, "filterin");
    std::list<std::string> filteroutRaw = getParamListFromQueryString(q, "filterout");
    PredefinedView v; // unamed view, used as handler on the viewing parameters
    v.filterin = parseFilter(filterinRaw);
    v.filterout = parseFilter(filteroutRaw);
    v.search = getFirstParamFromQueryString(q, "search");
    v.sort = getFirstParamFromQueryString(q, "sort");
    v.colspec = getFirstParamFromQueryString(q, "colspec");
    std::string limit = getFirstParamFromQueryString(q, "limit");
    if (!limit.empty()) v.limit = atoi(limit.c_str());
    else v.limit = -1; // no limit
    return v;
}

std::string PredefinedView::serialize() const
{
    std::string out;

    out += "addView " + serializeSimpleToken(name) + " \\\n";
    if (isDefault) out += "    default \\\n";

    std::map<std::string, std::list<std::string> >::const_iterator f;
    FOREACH(f, filterin) {
        std::list<std::string>::const_iterator i;
        FOREACH(i, f->second) {
            out += "    filterin " + serializeSimpleToken(f->first) + " ";
            out += serializeSimpleToken(*i) + " \\\n";
        }
    }

    FOREACH(f, filterout) {
        std::list<std::string>::const_iterator i;
        FOREACH(i, f->second) {
            out += "    filterout " + serializeSimpleToken(f->first) + " ";
            out += serializeSimpleToken(*i) + " \\\n";
        }
    }

    if (!sort.empty()) out += "    sort " + serializeSimpleToken(sort) + " \\\n";
    if (!colspec.empty()) out += "    colspec " + serializeSimpleToken(colspec) + " \\\n";
    if (!search.empty()) out += "    search " + serializeSimpleToken(search) + "\n";

    return out;
}

std::string PredefinedView::serializeViews(const std::map<std::string, PredefinedView> &views)
{
    std::string out;
    std::map<std::string, PredefinedView>::const_iterator v;
    FOREACH(v, views) {
        out += v->second.serialize();
        out += "\n";
    }
    return out;
}

/** Load views from a file
  *
  * @param[out] views
  */
int PredefinedView::loadViews(const std::string &path, std::map<std::string, PredefinedView> &views)
{
    std::string contents;
    int n = loadFile(path.c_str(), contents);
    if (n != 0) {
        LOG_ERROR("Cannot load views '%s': %s", path.c_str(), strerror(errno));
        return -1;
    }

    std::list<std::list<std::string> > lines = parseConfigTokens(contents.c_str(), contents.size());
    PredefinedView::parsePredefinedViews(lines, views);

    return 0;
}

std::map<std::string, PredefinedView> PredefinedView::getDefaultViews()
{
    std::map<std::string, PredefinedView> defaultViews;
    PredefinedView v;
    // view "All issues"
    v.name = _("All Issues");
    v.sort = "status-mtime+id";
    defaultViews[v.name] = v;

    // view "My issues"
    v.name = _("My Issues");
    v.sort = "status-mtime+id";
    v.filterin["owner"].push_back("me");
    defaultViews[v.name] = v;

    return defaultViews;
}
