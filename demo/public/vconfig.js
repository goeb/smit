function addFilter(divname, selected, value) {
    var div = document.getElementById(divname);
    var select = createSelect(Properties, selected);
    div.appendChild(select);
    var i = document.createElement('input');
    i.type = "text";
    i.value = value;
    div.appendChild(i);
    div.appendChild(document.createElement('br'));
}
function createSelect(items, selected) {
    var select = document.createElement('select');
    var items2 = items.slice(0);
    items2.push(''); // add an empty value
    var length = items2.length;
    for (var i = 0; i < length; i++) {
        var opt = document.createElement('option');
        if (selected == items2[i]) {
            opt.selected = "selected";
        }
        opt.innerHTML = items2[i];
        select.appendChild(opt);
    }
    return select;
}
function addColspec(selected) {
    var divname = 'colspec';
    var div = document.getElementById(divname);
    var select = createSelect(Properties, selected);
    div.appendChild(select);
}
function addSort(selectedDirection, selectedProperty) {
    var divname = 'sort';
    var div = document.getElementById(divname);
    var select = createSelect(['Ascending', 'Descending'], selectedDirection);
    div.appendChild(select);
    select = createSelect(Properties, selectedProperty);
    div.appendChild(select);
    div.appendChild(document.createElement('br'));
}
function setSearch(value) {
    var iname = 'search';
    var input = document.getElementById(iname);
    input.value = value;
}
function setName(value) {
    var iname = 'sm_predefined_view_name'; 
    var input = document.getElementById(iname);
    input.value = value;
}
function setUrl(value) {
    var aname = 'sm_view_current_url'; 
    var a = document.getElementById(aname);
    a.href = value;
    a.innerHTML = value;
}
function init() {
    addSort();
}
function addAllColspec() {
    var length = Properties.length;
    for (var i = 0; i < length; i++) {
        addColspec(Properties[i]);
    }
}
