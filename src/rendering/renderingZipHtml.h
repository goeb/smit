#ifndef _renderingZipHtml_h
#define _renderingZipHtml_h

#define HTML_STYLES \
    "body {" \
    "    color: black;" \
    "    margin-top: 0;" \
    "    font-family: Verdana,sans-serif;" \
    "    font-size: small;" \
    "    vertical-align: top;" \
    "}" \
    "a[href], a[href]:link {" \
    "  color:blue;" \
    "  text-decoration: none;" \
    "}" \
    "a[href]:hover {" \
    "  color:blue;" \
    "  text-decoration: underline;" \
    "}" \
    "  td {" \
    "      vertical-align: top;" \
    "  }" \
    "  textarea {" \
    "      vertical-align: top;" \
    "  }" \
    "  .sm_issue_properties {" \
    "      margin-top: 1em;" \
    "      width: 50em;" \
    "      margin-bottom: 2em;" \
    "      font-size: small;" \
    "      border-collapse:collapse;" \
    "  }" \
    "  tr.sm_issue_asso td.sm_issue_asso {" \
    "      border: solid 1px #bbb;" \
    "  }" \
    "  .sm_issue_asso_id {" \
    "      font-weight: bold;" \
    "  }" \
    "  .sm_issue_asso_summary {" \
    "  }" \
    "  .sm_entry {" \
    "      clear: both;" \
    "      border: 0;" \
    "      margin: 0;" \
    "      padding: 5px;" \
    "  }" \
    "  .sm_entry_header {" \
    "      margin: 0px;" \
    "      padding: 0px;" \
    "      font-weight: bold;" \
    "      font-size: 80%;" \
    "      padding-left: 0.5em;" \
    "  }" \
    "  .sm_entry_message {" \
    "      background-color: #DFDFDF;" \
    "      border: 1px solid #BBB;" \
    "      white-space: pre;" \
    "      font-family: monospace;" \
    "      font-size: 100%;" \
    "      padding: 0.5em;" \
    "      padding-bottom: 0.5em;" \
    "      margin-top: 0.5em;" \
    "      margin-bottom: 0.5em;" \
    "  }" \
    "  .sm_entry_other_properties {" \
    "      clear: both;" \
    "      color: #777;" \
    "      padding-top: 7px;" \
    "      width: 60em;" \
    "      font-size: 80%;" \
    "  }" \
    "  a.sm_entry_tag, a.sm_entry_tag:link, a.sm_entry_tag:hover {" \
    "      cursor: pointer;" \
    "      text-decoration: none;" \
    "  }" \
    "      span.sm_entry_tagged {" \
    "          font-weight: bold;" \
    "          color: green;" \
    "          font-size: 150%;" \
    "      }" \
    "      div.sm_entry_notag {" \
    "      }" \
    "      span.sm_entry_notag {" \
    "          font-weight: bold;" \
    "          color: #ccc;" \
    "      }" \
    "      .sm_issue_plabel {" \
    "          font-weight: bold;" \
    "          white-space: nowrap;" \
    "          font-size: small;" \
    "          text-align: right;" \
    "      }" \
    "      .sm_issue_pvalue_ta, .sm_issue_pvalue_ta2 {" \
    "          white-space: pre-wrap;" \
    "      }" \
    "      .sm_issue_pvalue {" \
    "          white-space: nowrap;" \
    "          padding-right: 6em;" \
    "      }" \
    "      .sm_issue {" \
    "          vertical-align: top;" \
    "          padding: 10px;" \
    "          font-size: 10pt;" \
    "          border-radius: 5px 5px 5px 5px;" \
    "      }" \
    "      .sm_issue_header {" \
    "          clear: both;" \
    "          width: 60em;" \
    "          padding: 0.3em;" \
    "          background-color: #E0E0E0;" \
    "          font-size: 125%;" \
    "          font-weight: bold;" \
    "          border: solid 1px;" \
    "      }" \
    "      .sm_issue_id {" \
    "          color: blue;" \
    "          padding-left: 1em;" \
    "          padding-right: 1em;" \
    "          border-right: 1px solid black;" \
    "          font-family: monospace;" \
    "          font-size: 125%;" \
    "      }" \
    ".sm_issue_tags {" \
    "    padding: 1em;" \
    "}" \
    ".sm_issue_notag {" \
    "    font-weight: bold;" \
    "    color: #BBB;" \
    "    border: solid black 1px;" \
    "    padding: 0.5em;" \
    "}" \
    ".sm_issue_tagged {" \
    "    font-weight: bold;" \
    "    color: green;" \
    "    border: solid black 1px;" \
    "    padding: 0.5em;" \
    "}" \
    "img.sm_entry_file {" \
    "    height: 50px;" \
    "    border: 0;" \
    "}" \
    ".sm_entry_file:hover {" \
    "    background-color: #d0d0d0;" \
    "}" \
    "div.sm_entry_file {" \
    "    float: left;" \
    "    background-color: #e0e0e0;" \
    "    padding: 6px;" \
    "    margin: 3px;" \
    "    text-align: center;" \
    "}" \
    "div.sm_entry_files {" \
    "    padding: 7px;" \
    "    width: 60em;" \
    "    font-size: 80%;" \
    "    font-weight: bold;" \
    "    margin-bottom: 10px;" \
    "}" \
    ".sm_entry_pname {" \
    "}" \
    ".sm_entry_pvalue {" \
    "    font-weight: bold;" \
    "}" \
    ".sm_underline { text-decoration: underline; }" \
    ".sm_double_quote {" \
    "    color: blue;" \
    "}" \
    ".sm_quote { color: green; }" \
    ".sm_block { color: blue; }" \
    ".sm_hyperlink { }" \
    "strong { font-weight: bold; }" \
    "em {" \
    "    background-color: yellow;" \
    "    font-style:normal;" \
    "}" \
    ""


#define HTML_HEADER \
    "<!DOCTYPE HTML>" \
    "<html>" \
    "<head>" \
    "<title>Issue %s</title>" \
    "<meta http-equiv=\"Content-Type\" content=\"text/html;charset=UTF-8\">" \
    "<link rel=\"stylesheet\" type=\"text/css\" href=\"../style.css\">" \
    "</head>" \
    "<body>"

#define HTML_FOOTER "</body></html>"


#endif
