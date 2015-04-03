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
};


struct ProjectConfig {
    ProjectConfig() : numberIssueAcrossProjects(false) {}

    // properties
    std::list<PropertySpec> properties; // user defined properties
    std::map<std::string, std::string> propertyLabels;
    std::map<std::string, std::string> propertyReverseLabels;
    std::map<std::string, TagSpec> tags;
    bool numberIssueAcrossProjects; // accross project

    std::string id;
    std::string parent;

    // methods
    static int load(const std::string &path, ProjectConfig &config);
    std::string serialize() const;
    static ProjectConfig parseProjectConfig(std::list<std::list<std::string> > &lines);
    const PropertySpec *getPropertySpec(const std::string name) const;
    std::list<std::string> getPropertiesNames() const;
    static std::list<std::string> getReservedProperties();
    static bool isReservedProperty(const std::string &name);
    std::string getLabelOfProperty(const std::string &propertyName) const;
    std::string getReverseLabelOfProperty(const std::string &propertyName) const;
    bool isValidPropertyName(const std::string &name) const;
    static bool isValidProjectName(const std::string &name);
};


#endif
