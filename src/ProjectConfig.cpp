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


PropertySpec parsePropertySpec(std::list<std::string> & tokens)
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
            if (tokens.front() == "-reverseLabel") {
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
    if (r != 0) return r;

    std::list<std::list<std::string> > lines = parseConfigTokens(data.c_str(), data.size());

    config = ProjectConfig::parseProjectConfig(lines);

    return 0;
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

        } else if (0 == token.compare("addProperty")) {
            PropertySpec property = parsePropertySpec(*line);
            if (property.name.size() > 0) {
                config.properties.push_back(property);
                LOG_DEBUG("properties: added %s", property.name.c_str());

                if (! property.label.empty()) config.propertyLabels[property.name] = property.label;
                if (! property.reverseLabel.empty()) config.propertyReverseLabels[property.name] = property.reverseLabel;

            } else {
                // parse error, ignore
                wellFormatedLines.pop_back(); // remove incorrect line
            }

        } else if (0 == token.compare("setPropertyLabel")) {
            if (line->size() != 2) {
                LOG_ERROR("Invalid setPropertyLabel");
                wellFormatedLines.pop_back(); // remove incorrect line
            } else {
                std::string propName = line->front();
                std::string propLabel = line->back();
                config.propertyLabels[propName] = propLabel;
            }

        } else if (token == "numberIssues") {
            std::string value = pop(*line);
            if (value == "global") config.numberIssueAcrossProjects = true;
            else LOG_ERROR("Invalid value '%s' for numberIssues.", value.c_str());

        } else if (token == "tag") {
            TagSpec tagspec;
            tagspec.id = pop(*line);
            tagspec.label = tagspec.id; // by default label = id

            if (tagspec.id.empty()) {
                LOG_ERROR("Invalid tag id");
                continue; // ignore current line and go to next one
            }
            while (line->size()) {
                token = pop(*line);
                if (token == "-label") {
                    std::string label = pop(*line);
                    if (label.empty()) LOG_ERROR("Invalid empty tag label");
                    else tagspec.label = label;
                } else if (token == "-display") {
                    tagspec.display = true;
                } else {
                    LOG_ERROR("Invalid token '%s' in tag specification", token.c_str());
                }
            }
            LOG_DEBUG("tag '%s' -label '%s' -display=%d", tagspec.id.c_str(), tagspec.label.c_str(),
                      tagspec.display);
            config.tags[tagspec.id] = tagspec;

        } else {
            LOG_DEBUG("Unknown function '%s'", token.c_str());
            wellFormatedLines.pop_back(); // remove incorrect line
        }
    }
    lines = wellFormatedLines;
    return config;
}


