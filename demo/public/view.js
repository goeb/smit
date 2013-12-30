/* Small Issue Tracker
 * Copyright (C) 2013 Frederic Hoerni
 * GNU General Public License v2
 */
function addFilter(divname, selected, value) {
    var div = document.getElementById(divname);
    var select = createSelect(Properties, selected, true);
    select.name = divname;
    div.appendChild(select);
    var i = document.createElement('input');
    i.type = "text";
    i.value = value;
    i.name = 'filter_value';
    div.appendChild(i);
    div.appendChild(document.createElement('br'));
}
function addColspec(selected) {
    var divname = 'colspec';
    var div = document.getElementById(divname);
    var select = createSelect(Properties, selected, true);
    select.name = 'colspec';
    div.appendChild(select);
}
function addSort(selectedDirection, selectedProperty) {
    var divname = 'sort';
    var div = document.getElementById(divname);
    var select = createSelect(['Ascending', 'Descending'], selectedDirection, true);
    select.name = 'sort_direction';
    div.appendChild(select);
    select = createSelect(Properties, selectedProperty, true);
    select.name = 'sort_property';
    div.appendChild(select);
    div.appendChild(document.createElement('br'));
}
function setSearch(value) {
    var iname = 'search';
    var input = document.getElementById(iname);
    input.value = value;
}
function setName(value) {
    var iname = 'name'; 
    var input = document.getElementById(iname);
    input.value = value;
}
function setUrl(value) {
    var aname = 'sm_view_current_url'; 
    var a = document.getElementById(aname);
    a.href = value;
    a.innerHTML = value;
}
function hideAdminZone() {
    var divs = document.getElementsByClassName('sm_view_admin_zone');
    for(var i=0; i<divs.length; i++) { 
          divs[i].style.display='none';
    }
}
function setDefaultCheckbox() {
    var input = document.getElementById('sm_default_view');
    input.checked = true;
}
