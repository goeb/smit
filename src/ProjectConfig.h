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

#ifndef _ProjectConfig_h
#define _ProjectConfig_h

#include <string>
#include <list>

#include "Issue.h"

struct TagSpec {
    TagSpec(): display(false) {}
    std::string id;
    std::string label; // UTF-8 text
    bool display; // status should be displayed in issue header
};

std::string propertyTypeToStr(PropertyType type);
int strToPropertyType(const std::string &s, PropertyType &out);
std::list<std::pair<bool, std::string> > parseSortingSpec(const char *sortingSpec);

struct PropertySpec {
    std::string name;
    std::string label;
    enum PropertyType type;
    std::list<std::string> selectOptions; // for F_SELECT and F_MULTISELECT only
    std::string reverseLabel; // for F_RELATIONSHIP

    static PropertySpec parsePropertySpec(std::list<std::string> & tokens);
};

#define KEY_ADD_PROPERTY "addProperty"
#define KEY_SET_PROPERTY_LABEL "setPropertyLabel"
#define KEY_NUMBER_ISSUES "numberIssues"
#define KEY_TAG "tag"
#define OPT_LABEL "-label"
#define OPT_REVERSE_LABEL "-reverseLabel"
#define OPT_DISPLAY "-display"

struct ProjectConfig {
    ProjectConfig() : numberIssueAcrossProjects(false) {}

    // user defined configuration
    std::list<PropertySpec> properties; // user defined properties
    std::map<std::string, std::string> propertyLabels;
    std::map<std::string, std::string> propertyReverseLabels;
    std::map<std::string, TagSpec> tags;
    bool numberIssueAcrossProjects; // accross project

    // internal properties
    std::string author; // author of a modification
    std::string parent; // id of previous known config
    std::string id; // id of this config
    time_t ctime;

    // methods
    static int load(const std::string &path, ProjectConfig &config);
    std::string serialize() const;
    static ProjectConfig parseProjectConfig(std::list<std::list<std::string> > &lines);
    int addProperty(std::list<std::string> &tokens);
    const PropertySpec *getPropertySpec(const std::string name) const;
    std::list<std::string> getPropertiesNames() const;
    static std::list<std::string> getReservedProperties();
    static bool isReservedProperty(const std::string &name);
    std::string getLabelOfProperty(const std::string &propertyName) const;
    std::string getReverseLabelOfProperty(const std::string &propertyName) const;
    bool isValidPropertyName(const std::string &name) const;
    static bool isValidProjectName(const std::string &name);
    static std::string getDefaultConfig();
};


#endif
