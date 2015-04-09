/*   Small Issue Tracker
 *   Copyright (C) 2015 Frederic Hoerni
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

#include <algorithm>

#include "ProjectConfig.h"
#include "global.h"
#include "logging.h"
#include "filesystem.h"
#include "parseConfig.h"

/** Convert a property type to a string
  *
  * @return
  *     empty string if unknown property type
  */
std::string propertyTypeToStr(PropertyType type)
{
    switch (type) {
    case F_TEXT: return "text";
    case F_SELECT: return "select";
    case F_MULTISELECT: return "multiselect";
    case F_SELECT_USER: return "selectUser";
    case F_TEXTAREA: return "textarea";
    case F_TEXTAREA2: return "textarea2";;
    case F_ASSOCIATION: return "association";
    default: return "";
    }
}


/** Convert a string to a PropertyType
  *
  * @return 0 on success, -1 on error
  */
int strToPropertyType(const std::string &s, PropertyType &out)
{
    if (s == "text") out = F_TEXT;
    else if (s == "selectUser") out = F_SELECT_USER;
    else if (s == "select") out = F_SELECT;
    else if (s == "multiselect") out = F_MULTISELECT;
    else if (s == "textarea") out = F_TEXTAREA;
    else if (s == "textarea2") out = F_TEXTAREA2;
    else if (s == "association") out = F_ASSOCIATION;

    else return -1; // error


    return 0; // ok
}


/** Get the specification of a given property
  *
  * @return 0 if not found.
  */
const PropertySpec *ProjectConfig::getPropertySpec(const std::string name) const
{
    std::list<PropertySpec>::const_iterator p;
    FOREACH(p, properties) {
        if (p->name == name) return &(*p);
    }
    return 0;
}

/** Get the list of all properties
  */
std::list<std::string> ProjectConfig::getPropertiesNames() const
{
    std::list<std::string> colspec;

    // get user defined properties
    std::list<PropertySpec>::const_iterator pspec;
    FOREACH(pspec, properties) {
        colspec.push_back(pspec->name);
    }

    // add mandatory properties that are not included in orderedProperties
    std::list<std::string> reserved = getReservedProperties();
    colspec.insert(colspec.begin(), reserved.begin(), reserved.end());
    return colspec;
}

std::list<std::string> ProjectConfig::getReservedProperties()
{
    std::list<std::string> reserved;
    reserved.push_back("id");
    reserved.push_back("ctime");
    reserved.push_back("mtime");
    reserved.push_back("summary");
    return reserved;
}

bool ProjectConfig::isReservedProperty(const std::string &name)
{
    std::list<std::string> reserved = getReservedProperties();
    std::list<std::string>::iterator i = std::find(reserved.begin(), reserved.end(), name);
    if (i == reserved.end()) return false;
    else return true;
}

std::string ProjectConfig::getLabelOfProperty(const std::string &propertyName) const
{
    std::string label;
    std::map<std::string, std::string>::const_iterator l;
    l = propertyLabels.find(propertyName);
    if (l != propertyLabels.end()) label = l->second;

    if (label.size()==0) label = propertyName;
    return label;
}

std::string ProjectConfig::getReverseLabelOfProperty(const std::string &propertyName) const
{
    std::string reverseLabel;
    std::map<std::string, std::string>::const_iterator rlabel;
    rlabel = propertyReverseLabels.find(propertyName);
    if (rlabel != propertyReverseLabels.end()) reverseLabel = rlabel->second;

    if (reverseLabel.size()==0) reverseLabel = propertyName;
    return reverseLabel;
}

bool ProjectConfig::isValidPropertyName(const std::string &name) const
{
    // get user defined properties
    std::list<PropertySpec>::const_iterator pspec;
    FOREACH(pspec, properties) {
        if (pspec->name == name) return true;
    }

    // look in reserved properties
    std::list<std::string> reserved = getReservedProperties();
    std::list<std::string>::const_iterator p;
    FOREACH(p, reserved) {
        if ((*p) == name) return true;
    }
    return false;

}
/** Check if a name is valid for a project
  *
  * Characters \r and \n are forbidden
  */
bool ProjectConfig::isValidProjectName(const std::string &name)
{
    std::size_t found = name.find_first_of("\r\n");
    if (found != std::string::npos) return false;
    return true;
}

ProjectConfig ProjectConfig::getDefaultConfig()
{
    ProjectConfig pconfig;
    pconfig.propertyLabels["id"] = "#";

    PropertySpec pspec;
    pspec.name = "status";
    pspec.type = F_SELECT;
    pspec.selectOptions.push_back("open");
    pspec.selectOptions.push_back("closed");
    pspec.selectOptions.push_back("deleted");
    pconfig.properties.push_back(pspec);

    pspec.name = "owner";
    pspec.type = F_SELECT_USER;
    pspec.selectOptions.clear();
    pconfig.properties.push_back(pspec);

    pconfig.author = "";
    pconfig.ctime = time(0);
    pconfig.parent = K_PARENT_NULL;

    return pconfig;
}



PropertySpec PropertySpec::parsePropertySpec(std::list<std::string> & tokens)
{
    // Supported syntax:
    // name [label <label>] type params ...
    // type = text | select | multiselect | selectUser | ...
    PropertySpec pspec;
    if (tokens.size() < 2) {
        LOG_ERROR("Not enough tokens");
        return pspec; // error, indicated to caller by empty name of pspec
    }

    pspec.name = tokens.front();
    // check that property name contains only [a-zA-Z0-9-_]
    const char* allowedInPropertyName = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";
    if (pspec.name.find_first_not_of(allowedInPropertyName) != std::string::npos) {
        // invalid character
        LOG_ERROR("Invalid property name: %s", pspec.name.c_str());
        pspec.name = "";
        return pspec;
    }
    tokens.pop_front();

    if (tokens.empty()) {
        LOG_ERROR("Not enough tokens");
        pspec.name = "";
        return pspec; // error, indicated to caller by empty name of property
    }

    // look for optional -label parameter
    if (tokens.front() == "-label") {
        tokens.pop_front();
        if (tokens.empty()) {
            LOG_ERROR("Not enough tokens (-label)");
            pspec.name = "";
            return pspec; // error, indicated to caller by empty name of property
        }
        pspec.label = tokens.front();
        tokens.pop_front();
    }

    if (tokens.empty()) {
        LOG_ERROR("Not enough tokens");
        pspec.name = "";
        return pspec; // error, indicated to caller by empty name of property
    }
    std::string type = tokens.front();
    tokens.pop_front();
    int r = strToPropertyType(type, pspec.type);
    if (r < 0) { // error, unknown type
        LOG_ERROR("Unkown property type '%s'", type.c_str());
        pspec.name.clear();
        return pspec; // error, indicated to caller by empty name of property
    }

    if (F_SELECT == pspec.type || F_MULTISELECT == pspec.type) {
        // populate the allowed values
        while (tokens.size() > 0) {
            std::string value = tokens.front();
            tokens.pop_front();

            if (F_MULTISELECT == pspec.type && value.empty()) {
                // for multiselect, an empty value has no sense and are removed

            } else pspec.selectOptions.push_back(value);
        }
    } else if (F_ASSOCIATION == pspec.type) {
        if (tokens.size() > 1) {
            // expect -reverseLabel
            if (tokens.front() == OPT_REVERSE_LABEL) {
                tokens.pop_front();
                pspec.reverseLabel = tokens.front();
                tokens.pop_front();
            }
        }
    }
    return pspec;
}

/** Load a project configuration from a file
  *
  * @param[out] config
  */
int ProjectConfig::load(const std::string &path, ProjectConfig &config)
{
    std::string data;
    int r = loadFile(path, data);
    if (r != 0) {
        LOG_ERROR("Cannot load project config '%s': %s", path.c_str(), strerror(errno));
        return -1;
    }

    std::list<std::list<std::string> > lines = parseConfigTokens(data.c_str(), data.size());

    config = ProjectConfig::parseProjectConfig(lines);
    config.id = getBasename(path);

    return 0;
}

std::string ProjectConfig::serialize() const
{
    std::string result;

    // header
    result += serializeProperty(K_SMIT_VERSION, VERSION);

    // authors, parent, ctime
    result += K_PARENT " " + serializeSimpleToken(parent) + "\n";
    char timestamp[128];
    sprintf(timestamp, "%ld", ctime);
    result += K_CTIME " " + serializeSimpleToken(timestamp) + "\n";
    result += K_AUTHOR " " + serializeSimpleToken(author) + "\n";

    // setPropertyLabel
    // for reserved properties
    std::list<std::string> reserved = getReservedProperties();
    std::list<std::string>::iterator r;
    FOREACH(r, reserved) {
        std::map<std::string, std::string>::const_iterator label;
        label = propertyLabels.find(*r);
        if (label != propertyLabels.end() && label->second != *r) {
            result += KEY_SET_PROPERTY_LABEL " " + (*r) + " " + serializeSimpleToken(label->second) + "\n";
        }
    }

    // properties
    std::list<PropertySpec>::const_iterator p; // user defined properties
    FOREACH(p, properties) {
        PropertySpec pspec = *p;
        result += KEY_ADD_PROPERTY " " + serializeSimpleToken(pspec.name);
        if (! pspec.label.empty() && pspec.label != pspec.name) {
            result += " -label " + serializeSimpleToken(pspec.label);
        }
        result += " " + propertyTypeToStr(pspec.type);
        if (pspec.type == F_SELECT || pspec.type == F_MULTISELECT) {
            // add std::list<std::string> selectOptions
            std::list<std::string>::const_iterator opt;
            FOREACH(opt, pspec.selectOptions) {
                result += " " + serializeSimpleToken(*opt);
            }

        } else if (pspec.type == F_ASSOCIATION && !pspec.reverseLabel.empty()) {
            result += " " OPT_REVERSE_LABEL " " + serializeSimpleToken(pspec.reverseLabel);
        }

        result += "\n";
    }

    // tags
    std::map<std::string, TagSpec>::const_iterator tag;
    FOREACH(tag, tags) {
        TagSpec tspec = tag->second;
        result += KEY_TAG " " + serializeSimpleToken(tspec.id);
        if (!tspec.label.empty() && tspec.label != tspec.id) {
            result += " " OPT_LABEL " " + serializeSimpleToken(tspec.label);
        }
        if (tspec.display) result += " " OPT_DISPLAY;
        result += "\n";
    }

    // numbering policy
    if (numberIssueAcrossProjects) {
        result += serializeProperty(KEY_NUMBER_ISSUES, "global");
    }

    return result;
}

/** Add a property
  *
  * @param[in/out] tokens
  *    The tokens are consumed.
  */
int ProjectConfig::addProperty(std::list<std::string> &tokens)
{
    PropertySpec property = PropertySpec::parsePropertySpec(tokens);
    if (property.name.size() > 0) {
        properties.push_back(property);
        LOG_DEBUG("properties: added %s", property.name.c_str());

        if (! property.label.empty()) propertyLabels[property.name] = property.label;
        if (! property.reverseLabel.empty()) propertyReverseLabels[property.name] = property.reverseLabel;

        return 0;
    } else {
        // syntax error or other error
        return -1;
    }
}

/** Return a configuration object from a list of lines of tokens
  * The 'lines' parameter is modified and cleaned up of incorrect lines
  */
ProjectConfig ProjectConfig::parseProjectConfig(std::list<std::list<std::string> > &lines)
{
    ProjectConfig config;

    std::list<std::list<std::string> >::iterator line;
    std::list<std::list<std::string> > wellFormatedLines;

    FOREACH (line, lines) {
        wellFormatedLines.push_back(*line);

        std::string token = pop(*line);
        if (token == K_SMIT_VERSION) {
            // not used in this version
            std::string v = pop(*line);
            LOG_DEBUG("Smit version of project: %s", v.c_str());

        } else if (token == KEY_ADD_PROPERTY) {
            int r = config.addProperty(*line);
            if (r != 0) {
                // parse error, ignore
                wellFormatedLines.pop_back(); // remove incorrect line
            }

        } else if (token == KEY_SET_PROPERTY_LABEL) {
            if (line->size() != 2) {
                LOG_ERROR("Invalid %s", KEY_SET_PROPERTY_LABEL);
                wellFormatedLines.pop_back(); // remove incorrect line
            } else {
                std::string propName = line->front();
                std::string propLabel = line->back();
                config.propertyLabels[propName] = propLabel;
            }

        } else if (token == KEY_NUMBER_ISSUES) {
            std::string value = pop(*line);
            if (value == "global") config.numberIssueAcrossProjects = true;
            else LOG_ERROR("Invalid value '%s' for %s.", value.c_str(), KEY_NUMBER_ISSUES);

        } else if (token == KEY_TAG) {
            TagSpec tagspec;
            tagspec.id = pop(*line);
            tagspec.label = tagspec.id; // by default label = id

            if (tagspec.id.empty()) {
                LOG_ERROR("Invalid %s id", KEY_TAG);
                continue; // ignore current line and go to next one
            }
            while (line->size()) {
                token = pop(*line);
                if (token == "-label") {
                    std::string label = pop(*line);
                    if (label.empty()) LOG_ERROR("Invalid empty %s label", KEY_TAG);
                    else tagspec.label = label;
                } else if (token == "-display") {
                    tagspec.display = true;
                } else {
                    LOG_ERROR("Invalid token '%s' in tag specification", token.c_str());
                }
            }
            LOG_DEBUG("%s '%s' -label '%s' -display=%d", KEY_TAG, tagspec.id.c_str(),
                      tagspec.label.c_str(), tagspec.display);
            config.tags[tagspec.id] = tagspec;

        } else if (token == K_PARENT) {
            token = pop(*line);
            config.parent = token;

        } else if (token == K_CTIME) {
            token = pop(*line);
            config.ctime = atoi(token.c_str());

        } else if (token == K_AUTHOR) {
            token = pop(*line);
            config.author = token;

        } else {
            LOG_DEBUG("Unknown function '%s'", token.c_str());
            wellFormatedLines.pop_back(); // remove incorrect line
        }
    }
    lines = wellFormatedLines;
    return config;
}


