#include <stdlib.h>
#include <sstream>
#include <string.h>

#include "renderingHtml.h"
#include "db.h"
#include "parseConfig.h"
#include "logging.h"
#include "stringTools.h"


std::string epochToString(time_t t)
{
    struct tm *tmp;
    tmp = localtime(&t);
    char datetime[100+1]; // should be enough
    //strftime(datetime, sizeof(datetime)-1, "%Y-%m-%d %H:%M:%S", tmp);
    if (time(0) - t > 48*3600) {
        // date older than 2 days
        strftime(datetime, sizeof(datetime)-1, "%d %b %Y", tmp);
    } else {
        strftime(datetime, sizeof(datetime)-1, "%d %b %Y, %H:%M:%S", tmp);
    }
    return std::string(datetime);
}



ContextParameters::ContextParameters(std::string u, int n, const Project &p) : project(p)
{
    username = u;
    numberOfIssues = n;
}

void ContextParameters::printSmitData(struct mg_connection *conn)
{
    mg_printf(conn, "%s", "<script id=\"sm_data\" type=\"application/json\">\n{");
    mg_printf(conn, "\"sm_user\": \"%s\"", username.c_str());
    mg_printf(conn, ", \"sm_numberOfIssues\": \"%d\"", numberOfIssues);
    mg_printf(conn, "%s", "}\n</script>");
}


void RHtml::printHeader(struct mg_connection *conn, const std::string &projectPath)
{
    std::string path = projectPath + "/html/header.html";
    char *data;
    int r = loadFile(path.c_str(), &data);
    if (r >= 0) {
        mg_printf(conn, "%s", data);
        free(data);
    } else {
        LOG_ERROR("Could not load header.html for project %s", projectPath.c_str());
    }
}

void RHtml::printSigninPage(struct mg_connection *conn, const char *pathToRepository, const char *redirect)
{

    mg_printf(conn, "Content-Type: text/html\r\n\r\n");

    std::string path = pathToRepository;
    path += "/public/signin.html";
    char *data;
    int r = loadFile(path.c_str(), &data);
    if (r >= 0) {
        mg_printf(conn, "%s", data);
        // add javascript for updating the redirect URL
        mg_printf(conn, "<script>document.getElementById(\"redirect\").value = \"%s\"</script>", redirect);
        free(data);
    } else {
        LOG_ERROR("Could not load %s", path.c_str());
    }
}


void RHtml::printFooter(struct mg_connection *conn, const std::string &projectPath)
{
    std::string path = projectPath + "/html/footer.html";
    char *data;
    int r = loadFile(path.c_str(), &data);
    if (r >= 0) {
        mg_printf(conn, "%s", data);
        free(data);
    } else {
        LOG_ERROR("Could not load footer.html for project %s", projectPath.c_str());
    }
}


void RHtml::printProjectList(struct mg_connection *conn, const std::list<std::string> &pList)
{
    mg_printf(conn, "Content-Type: text/html\r\n\r\n");

    std::list<std::string>::const_iterator p;
    for (p=pList.begin(); p!=pList.end(); p++) {
        mg_printf(conn, "%s\n", p->c_str());

    }
}

/** Build a new query string based on the current one, and update the sorting part
  *
  *
  * Example:
  * In: current query string is sort=xx&filterin=...&whatever...
  *     property name is 'assignee'
  * Out when exclusive is true:
  *     sort=assignee&filterin=...&whatever...
  * Out when exclusive is false:
  *     sort=xx+assignee&filterin=...&whatever...
  *
  * When exclusive is false, the existing sorting is kept, except that:
  * - if the property is already present, its order is inverted (+/-)
  * - else, the property is added after the others
  *
  */
std::string getNewSortingSpec(struct mg_connection *conn, const std::string property, bool exclusive)
{
    const char *qstring = mg_get_request_info(conn)->query_string;
    std::string qs;
    if (qstring) qs = qstring;
    LOG_DEBUG("getNewSortingSpec: in=%s, exclusive=%d", qs.c_str(), exclusive);
    std::string result;

    const char *SORT_SPEC_HEADER = "sort=";

    while (qs.size() > 0) {
        std::string part = popToken(qs, '&');
        if (0 == strncmp(SORT_SPEC_HEADER, part.c_str(), strlen(SORT_SPEC_HEADER))) {
            // sorting spec, that we want to alter
            std::string newSortingSpec = "";
            popToken(part, '=');

            if (exclusive) {
                newSortingSpec = SORT_SPEC_HEADER;
                newSortingSpec += property;
            } else {
                size_t len = part.size();
                size_t currentOffset = 0;
                std::string currentPropertyName;
                char currentOrder = '+';
                bool propertyFound = false; // detect if specified property is already present
                while (currentOffset < len) {
                    char c = part[currentOffset];
                    if (c == '+' || c == ' ' || c == '-' ) {
                        if (currentPropertyName.size()>0) {
                            // store property name
                            if (currentPropertyName == property) {
                                if (currentOrder == '+') currentOrder = '-';
                                else currentOrder = '+';
                                propertyFound = true;
                            }
                            newSortingSpec += currentOrder;
                            newSortingSpec += currentPropertyName;
                            currentPropertyName = "";
                        }
                        currentOrder = c;
                    } else {
                        currentPropertyName += c;
                    }
                    currentOffset++;
                }
                // flush remaining
                if (currentPropertyName.size()>0) {
                    // store property name
                    if (currentPropertyName == property) {
                        if (currentOrder == '+') currentOrder = '-';
                        else currentOrder = '+';
                        propertyFound = true;
                    }
                    newSortingSpec += currentOrder;
                    newSortingSpec += currentPropertyName;

                }
                if (!propertyFound) {
                    // add it at the end
                    newSortingSpec += '+';
                    newSortingSpec += property;
                }

                newSortingSpec = newSortingSpec.insert(0, SORT_SPEC_HEADER); // add the "sort=" at the beginning
            }
            part = newSortingSpec;
        }

        // append to the result
        if (result == "") result = part;
        else result = result + '&' + part;
    }
    LOG_DEBUG("getNewSortingSpec: result=%s", result.c_str());


    return result;
}

void RHtml::printIssueList(struct mg_connection *conn, const ContextParameters &ctx,
                           std::list<struct Issue*> issueList, std::list<std::string> colspec)
{
    mg_printf(conn, "Content-Type: text/html\r\n\r\n");
    printHeader(conn, ctx.project.getPath());

    // TODO use colspec
    // TODO sorting
    mg_printf(conn, "<table class=\"table_issues\">\n");

    // print header of the table
    mg_printf(conn, "<tr class=\"tr_issues\">\n");
    std::list<std::string>::iterator colname;
    for (colname = colspec.begin(); colname != colspec.end(); colname++) {
        std::string label = ctx.project.getLabelOfProperty(*colname);
        std::string newQueryString = getNewSortingSpec(conn, *colname, true);
        mg_printf(conn, "<th class=\"th_issues\"><a class=\"sm_sort_exclusive\" href=\"?%s\" title=\"Sort ascending\">%s</a>\n",
                  newQueryString.c_str(), label.c_str());
        newQueryString = getNewSortingSpec(conn, *colname, false);
        mg_printf(conn, " <a href=\"?%s\" class=\"sm_sort_accumulate\" title=\"Sort and preserve other sorted columns\">&#10228;</a></th>\n", newQueryString.c_str());
    }
    mg_printf(conn, "</tr>\n");

    std::list<struct Issue*>::iterator i;
    for (i=issueList.begin(); i!=issueList.end(); i++) {

        mg_printf(conn, "<tr class=\"tr_issues\">\n");

        std::list<std::string>::iterator c;
        for (c = colspec.begin(); c != colspec.end(); c++) {
            std::ostringstream text;
            std::string column = *c;

            if (column == "id") text << (*i)->id.c_str();
            else if (column == "ctime") text << epochToString((*i)->ctime);
            else if (column == "mtime") text << epochToString((*i)->mtime);
            else {
                // look if it is a single property
                std::map<std::string, std::list<std::string> >::iterator p;
                std::map<std::string, std::list<std::string> > & properties = (*i)->properties;

                p = properties.find(column);
                if (p != properties.end()) text << toString(p->second);
            }
            // add href if column is 'id' or 'title'
            std::string href_lhs = "";
            std::string href_rhs = "";
            if ( (column == "id") || (column == "title") ) {
                href_lhs = "<a href=\"";
                href_lhs = href_lhs + "/" + ctx.project.getName() + "/issues/";
                href_lhs = href_lhs + (char*)(*i)->id.c_str() + "\">";
                href_rhs = "</a>";
            }

            mg_printf(conn, "<td class=\"td_issues\">%s%s%s</td>\n", href_lhs.c_str(), text.str().c_str(), href_rhs.c_str());


        }
        mg_printf(conn, "</tr>\n");
    }
    mg_printf(conn, "</table>\n");
    mg_printf(conn, "%d issues\n", issueList.size());
    printFooter(conn, ctx.project.getName().c_str());

}


bool RHtml::inList(const std::list<std::string> &listOfValues, const std::string &value)
{
    std::list<std::string>::const_iterator v;
    for (v=listOfValues.begin(); v!=listOfValues.end(); v++) if (*v == value) return true;

    return false;

}


// Example: replaceAll(in, '"', "&quot;")
// Replace all " by &quot;
std::string replaceHtmlEntity(const std::string &in, char c, const char *replaceBy)
{
    std::string out;
	size_t len = in.size();
    size_t i = 0;
    size_t savedOffset = 0;
    while (i < len) {
		if (in[i] == c) {
			if (savedOffset < i) out += in.substr(savedOffset, i-savedOffset);
			out += replaceBy;
			savedOffset = i+1;
		}
		i++;
    }
	if (savedOffset < i) out += in.substr(savedOffset, i-savedOffset);
    return out;
}

std::string htmlEscape(const std::string &value)
{
    std::string result = replaceHtmlEntity(value, '&', "&amp;");
    result = replaceHtmlEntity(result, '"', "&quot;");
    result = replaceHtmlEntity(result, '<', "&lt;");
    result = replaceHtmlEntity(result, '>', "&gt;");
    result = replaceHtmlEntity(result, '\'', "&apos;");
    return result;
}


std::string convertToRichTextWholeline(const std::string &in, const char *start, const char *htmlTag, const char *htmlClass)
{
    std::string result;
    size_t i = 0;
    size_t block = 0; // beginning of block, relevant only when insideBlock == true
    size_t len = in.size();
    bool insideBlock = false;
    bool startOfLine = true;
    while (i<len) {
        char c = in[i];
        if (c == '\n') {
            startOfLine = true;

            if (insideBlock) {
                // end of line and end of block
                std::ostringstream currentBlock;

                currentBlock << "<" << htmlTag;
                currentBlock << " class=\"" << htmlClass << "\">";
                currentBlock << in.substr(block, i-block+1);
                currentBlock << "</" << htmlTag << ">";
                result += currentBlock.str();
                insideBlock = false;
            }
            result += c;

        } else if (startOfLine && (i+strlen(start)-1 < len) && (0 == strncmp(start, in.c_str()+i, strlen(start))) ) {
            // beginning of new block
            insideBlock = true;
            block = i;

        } else {
            startOfLine = false;
            if (!insideBlock) result += c;
        }

        i++;
    }

    if (insideBlock) {
        // flush pending block
        std::ostringstream currentBlock;

        currentBlock << "<" << htmlTag;
        currentBlock << " class=\"" << htmlClass << "\">";
        currentBlock << in.substr(block, i-block+1);
        currentBlock << "</" << htmlTag << ">";
        result += currentBlock.str();
    }
    return result;

}

bool isRichTextBlockSeparator(char c)
{
    if (isspace(c) || isblank(c)) return true;
    //if (c == '.'  || c == ';' || c == ':' || c == ',') return true;
    return false;
}

/** Convert text to HTML rich text according to 1 rich text pattern
  *
  * Basic syntax rules:
  * - rich text separators must be surrounded by blanks outside the rich text block, and non blank on the inside.
  * Examples:
  *    _underline_ => ok
  *    aa_underline_ => nope (because a 'a' before the first _
  *    _ underline_ => nope (because a space after the first _
  *
  *
  * @param dropBlockSeparators
  *    If true, the begin and end separators a removed from the final HTML
  *
  */
std::string convertToRichTextInline(const std::string &in, char begin, char end,
                                   bool dropBlockSeparators, const char *htmlTag, const char *htmlClass)
{
    std::string result;
    size_t i = 0;
    size_t block = 0; // beginning of block, relevant only when insideBlock == true
    size_t len = in.size();
    bool insideBlock = false;
    while (i<len) {
        char c = in[i];

        if (insideBlock) {
            // look if we are at the end of a block
            if (c == end &&
                    (end == '\n' || i == len-1 || isRichTextBlockSeparator(in[i+1]) ) &&
                    (i == 0 || !isRichTextBlockSeparator(in[i-1])) ) {
                // end of block detected
                size_t L;
                if (dropBlockSeparators) {
                    block++;
                    L = i-block;
                } else {
                    L = i-block+1;
                }
                std::ostringstream currentBlock;


                if (0 == strcmp("a", htmlTag)) {
                    // for "a" tags, add "href=..."
                    std::string hyperlink = in.substr(block+1, L-2);
                    currentBlock << in[block] << "<" << htmlTag;
                    currentBlock << " href=\"" << hyperlink << "\">" << hyperlink;
                    currentBlock << "</" << htmlTag << ">" << c;

                } else {
                    currentBlock << "<" << htmlTag;
                    currentBlock << " class=\"" << htmlClass << "\">";
                    currentBlock << in.substr(block, L);
                    currentBlock << "</" << htmlTag << ">";
                }
                result += currentBlock.str();
                insideBlock = false;

            } else if (c == '\n') {
                // end of line cancels the pending block
                result += in.substr(block, i-block+1);
                insideBlock = false;
            }
        } else if ( (begin == c) &&
                    (i==0 || isRichTextBlockSeparator(in[i-1]) ) &&
                    (i == len-1 || !isRichTextBlockSeparator(in[i+1])) ) {
            // beginning of new block
            insideBlock = true;
            block = i;

        } else result += c;
        i++;
    }
    if (insideBlock) {
        // cancel pending block
        result += in.substr(block);
    }
    return result;

}

/** Convert text to HTML rich text
  *
  *    *a b c* => <span class="sm_bold">a b c</span>
  *    _a b c_ => <span class="sm_underline">a b c</span>
  *    /a b c/ => <span class="sm_highlight">a b c</span>
  *    [a b c] => <a href="a b c" class="sm_hyperlink">a b c</a>
  *    > a b c =>  <span class="sm_quote">a b c</span> (> must be at the beginning of the line)
  *
  * (optional) Characters before and after block must be [\t \n.;:]
  * A line break in the middle prevents the pattern from being recognized.
  */
std::string convertToRichText(const std::string &raw)
{
    std::string result = convertToRichTextInline(raw, '*', '*', true, "strong", "");
    result = convertToRichTextInline(result, '_', '_', true, "span", "sm_underline");
    result = convertToRichTextInline(result, '/', '/', true, "em", "");
    result = convertToRichTextInline(result, '[', ']', false, "a", "sm_hyperlink");
    result = convertToRichTextWholeline(result, "&gt;", "blockquote", "");
    return result;

}

void RHtml::printIssue(struct mg_connection *conn, const ContextParameters &ctx, const Issue &issue, const std::list<Entry*> &entries)
{
    LOG_DEBUG("printIssue...");

    mg_printf(conn, "Content-Type: text/html\r\n\r\n");
    printHeader(conn, ctx.project.getPath().c_str());

    mg_printf(conn, "<div class=\"sm_issue\">");

    // issue header
    // print id and title
    mg_printf(conn, "<div class=\"sm_issue_header\">\n");
    mg_printf(conn, "<span class=\"sm_issue_id\">%s</span>\n", issue.id.c_str());
    mg_printf(conn, "<span class=\"sm_issue_title\">%s</span>\n", htmlEscape(issue.getTitle()).c_str());
    mg_printf(conn, "</div>\n");

    // issue summary
    // print the fields of the issue in a two-column table
    mg_printf(conn, "<table class=\"sm_fields_summary\">");
    int workingColumn = 1;
    const uint8_t MAX_COLUMNS = 2;

    std::list<std::string> orderedFields = ctx.project.getConfig().orderedFields;

    std::list<std::string>::const_iterator f;
    for (f=orderedFields.begin(); f!=orderedFields.end(); f++) {
        std::string fname = *f;
        std::string label = ctx.project.getLabelOfProperty(fname);

        std::string value;
        std::map<std::string, std::list<std::string> >::const_iterator p = issue.properties.find(fname);

        if (p != issue.properties.end()) value = toString(p->second);

        if (workingColumn == 1) {
            mg_printf(conn, "<tr>\n");
        }
        mg_printf(conn, "<td class=\"sm_flabel sm_flabel_%s\">%s: </td>\n", fname.c_str(), label.c_str());
        mg_printf(conn, "<td class=\"sm_fvalue sm_fvalue_%s\">%s</td>\n", fname.c_str(), htmlEscape(value).c_str());

        workingColumn += 1;
        if (workingColumn > MAX_COLUMNS) {
            mg_printf(conn, "</tr>\n");
            workingColumn = 1;
        }
    }
    mg_printf(conn, "</table>\n");


    // print entries
    std::list<Entry*>::const_iterator e;
    for (e = entries.begin(); e != entries.end(); e++) {
        Entry ee = *(*e);
        mg_printf(conn, "<div class=\"sm_entry\">\n");

        mg_printf(conn, "<div class=\"sm_entry_header\">\n");
        mg_printf(conn, "Author: <span class=\"sm_entry_author\">%s</span>", htmlEscape(ee.author).c_str());
        mg_printf(conn, " / <span class=\"sm_entry_ctime\">%s</span>\n", epochToString(ee.ctime).c_str());
        // conversion date en javascript
        // document.write(new Date(%d)).toString());
        mg_printf(conn, "</div>\n"); // end header

        mg_printf(conn, "<div class=\"sm_entry_message\">");
        std::map<std::string, std::list<std::string> >::iterator m = ee.properties.find(K_MESSAGE);
        if (m != ee.properties.end()) {
            if (m->second.size() != 0) {
                mg_printf(conn, "%s\n", convertToRichText(htmlEscape(m->second.front())).c_str());
            }
        }
        mg_printf(conn, "</div>\n"); // end message


        // print other modified properties

        std::ostringstream otherFields;
        bool firstInList = true;

        // process title first as it is not part of orderedFields
        std::map<std::string, std::list<std::string> >::const_iterator p = ee.properties.find(K_TITLE);
        std::string value;
        if (p != ee.properties.end()) {
            value = toString(p->second);
            otherFields << "<span class=\"sm_entry_pname\">" << ctx.project.getLabelOfProperty(K_TITLE) << ": </span>";
            otherFields << "<span class=\"sm_entry_pvalue\">" << htmlEscape(value) << "</span>";
            firstInList = false;
        }

        for (f=orderedFields.begin(); f!=orderedFields.end(); f++) {
            std::string pname = *f;
            if (pname == K_MESSAGE) continue; // already processed

            std::map<std::string, std::list<std::string> >::const_iterator p = ee.properties.find(pname);
            if (p != ee.properties.end()) {
                // the entry has this property
                value = toString(p->second);

                if (!firstInList) otherFields << ", "; // separate properties by a comma
                otherFields << "<span class=\"sm_entry_pname\">" << ctx.project.getLabelOfProperty(pname) << ": </span>";
                otherFields << "<span class=\"sm_entry_pvalue\">" << htmlEscape(value) << "</span>";
                firstInList = false;
            }
        }

        if (otherFields.str().size() > 0) {
            mg_printf(conn, "<div class=\"sm_other_fields\">\n");
            mg_printf(conn, "%s", otherFields.str().c_str());
            mg_printf(conn, "</div>\n");
        }

        mg_printf(conn, "</div>\n"); // end entry

    } // end of entries


    printIssueForm(conn, ctx, issue);
    printFooter(conn, ctx.project.getPath().c_str());
}


void RHtml::printNewIssuePage(struct mg_connection *conn, const ContextParameters &ctx)
{
    LOG_DEBUG("printNewPage...");

    mg_printf(conn, "Content-Type: text/html\r\n\r\n");
    printHeader(conn, ctx.project.getPath().c_str());

    mg_printf(conn, "<div class=\"sm_issue\">");

    Issue issue;
    printIssueForm(conn, ctx, issue);
    printFooter(conn, ctx.project.getPath().c_str());
}


/** print form for adding a message / modifying the issue
  */
void RHtml::printIssueForm(struct mg_connection *conn, const ContextParameters &ctx, const Issue &issue)
{
    // TODO if access rights granted

    // enctype=\"multipart/form-data\"
    mg_printf(conn, "<form method=\"post\"  class=\"sm_issue_form\">");
    // print the fields of the issue in a two-column table

    // The form is made over a table with 4 columns.
    // each row is made of 1 label, 1 input, 1 label, 1 input (4 columns)
    // except for the title.
    // title
    mg_printf(conn, "<table class=\"sm_fields_summary\">");
    mg_printf(conn, "<tr>\n");
    mg_printf(conn, "<td class=\"sm_flabel sm_flabel_title\">%s: </td>\n", ctx.project.getLabelOfProperty("title").c_str());
    mg_printf(conn, "<td class=\"sm_finput\" colspan=\"3\">");
    mg_printf(conn, "<input class=\"sm_finput_title\" required=\"required\" type=\"text\" name=\"title\" value=\"%s\">", htmlEscape(issue.getTitle()).c_str());
    mg_printf(conn, "</td>\n");
    mg_printf(conn, "</tr>\n");

    int workingColumn = 1;
    const uint8_t MAX_COLUMNS = 2;
    std::list<std::string>::const_iterator f;

    std::list<std::string> orderedFields = ctx.project.getConfig().orderedFields;

    std::map<std::string, FieldSpec> fields = ctx.project.getConfig().fields;


    for (f=orderedFields.begin(); f!=orderedFields.end(); f++) {
        std::string fname = *f;
        std::string label = ctx.project.getLabelOfProperty(fname);

        std::map<std::string, FieldSpec>::const_iterator fieldSpec = fields.find(fname);
        if (fieldSpec == fields.end()) {
            LOG_ERROR("Field '%s' (of setHtmlFieldDisplay) not found in addField options", fname.c_str());
            continue;
        }

        FieldSpec fspec = fieldSpec->second;

        std::map<std::string, std::list<std::string> >::const_iterator p = issue.properties.find(fname);
        std::list<std::string> propertyValues;
        if (p!=issue.properties.end()) propertyValues = p->second;

        std::ostringstream input;
        std::string value;

        if (fspec.type == F_TEXT) {
            if (propertyValues.size()>0) value = propertyValues.front();
            input << "<input class=\"sm_finput_" << fname << "\" type=\"text\" name=\""
                  << fname << "\" value=\"" << htmlEscape(value) << "\">\n";

        } else if (fspec.type == F_SELECT) {
            if (propertyValues.size()>0) value = propertyValues.front();
            std::list<std::string>::iterator so;
            input << "<select class=\"sm_finput_" << fname << "\" name=\"" << fname << "\">";

            for (so = fspec.selectOptions.begin(); so != fspec.selectOptions.end(); so++) {
                input << "<option" ;
                if (value == *so) input << " selected=\"selected\"";
                input << ">" << htmlEscape(*so) << "</option>";
            }

            input << "</select>";

        } else if (fspec.type == F_MULTISELECT) {
            std::list<std::string>::iterator so;
            input << "<select class=\"sm_finput_" << fname << "\" name=\"" << fname << "\"";
            if (fspec.type == F_MULTISELECT) input << " multiple=\"multiple\"";
            input << ">";

            for (so = fspec.selectOptions.begin(); so != fspec.selectOptions.end(); so++) {
                input << "<option" ;
                if (inList(propertyValues, *so)) input << " selected=\"selected\"";
                input << ">" << htmlEscape(*so) << "</option>";
            }

            input << "</select>";

        } else if (fspec.type == F_SELECT_USER) {
            if (propertyValues.size()>0) value = propertyValues.front();
            std::list<std::string>::iterator u;
            input << "<select class=\"sm_finput_" << fname << "\" name=\"" << fname << "\">";

            // TODO
            std::list<std::string> users;
            users.push_back("John");
            users.push_back("Fred");
            users.push_back("Alice");
            users.push_back("David G. Smith");
            for (u = users.begin(); u != users.end(); u++) {
                input << "<option" ;
                if (value == *u) input << " selected=\"selected\"";
                input << ">" << htmlEscape(*u) << "</option>";
            }

            input << "</select>";


        } else {
            LOG_ERROR("invalid fieldSpec->type=%d", fspec.type);
            continue;
        }

        if (workingColumn == 1) {
            mg_printf(conn, "<tr>\n");
        }
        mg_printf(conn, "<td class=\"sm_flabel sm_flabel_%s\">%s: </td>\n", fname.c_str(), label.c_str());
        mg_printf(conn, "<td class=\"sm_finput\">%s</td>\n", input.str().c_str());

        workingColumn += 1;
        if (workingColumn > MAX_COLUMNS) {
            mg_printf(conn, "</tr>\n");
            workingColumn = 1;
        }
    }

    if (workingColumn != 1) {
        // add an empty cell
        mg_printf(conn, "<td></td></tr>\n");
    }
    mg_printf(conn, "<tr>\n");
    mg_printf(conn, "<td class=\"sm_flabel sm_flabel_message\" >%s: </td>\n", ctx.project.getLabelOfProperty("message").c_str());
    mg_printf(conn, "<td colspan=\"3\">\n");
    mg_printf(conn, "<textarea class=\"sm_finput sm_finput_message\" placeholder=\"%s\" name=\"%s\" wrap=\"hard\" cols=\"80\">\n",
              "Enter a message", K_MESSAGE);
    mg_printf(conn, "</textarea>\n");
    mg_printf(conn, "</td></tr>\n");

    // check box "enable long lines"
    mg_printf(conn, "<tr><td></td>\n");
    mg_printf(conn, "<td class=\"sm_longlines\" colspan=\"3\">\n");
    mg_printf(conn, "<input type=\"checkbox\" onclick=\"changeWrapping();\">\n");
    mg_printf(conn, "%s\n", ctx.project.getLabelOfProperty("long-line-break-message").c_str());
    mg_printf(conn, "</td></tr>\n");

    mg_printf(conn, "<tr><td></td>\n");
    mg_printf(conn, "<td colspan=\"3\">\n");
    mg_printf(conn, "<input type=\"submit\" value=\"%s\">\n", ctx.project.getLabelOfProperty("Add-Message").c_str());
    mg_printf(conn, "</td></tr>\n");

    mg_printf(conn, "</table>\n");

    mg_printf(conn, "</form>");

    mg_printf(conn, "</div>");
}
