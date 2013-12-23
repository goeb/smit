/* Small Issue Tracker
 * Copyright (C) 2013 Frederic Hoerni
 * GNU General Public License v2
 */
function addProject(divname, selectedProject, selectedRole) {
    var div = document.getElementById(divname);

    var select = createSelect(Projects, selectedProject, true);
    select.name = 'project';
    div.appendChild(select);
    var i = createSelect(Roles, selectedRole, true);
    i.name = 'role';
    div.appendChild(i);
    div.appendChild(document.createElement('br'));
}
function setName(value) {
    var iname = 'name';
    var input = document.getElementById(iname);
    input.value = value;
}
function setSuperadminCheckbox() {
    var input = document.getElementById('sm_superadmin');
    input.checked = true;
}
