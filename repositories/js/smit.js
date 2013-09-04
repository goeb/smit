
// The HTML page must include the smit_data
// <script id="smit_data" type="application/json">
//      {"smit_user": "Mr. John", "smit_issueNumber": 34}
// </script>
// 
// and where to display this information:
// Logged in as <span class="smit_user">nobody</span>

function update(classname, value)
{
    var els = document.getElementsByClassName(classname);
    for(var i=0; i<els.length; i++) els[i].innerHTML = value;
}

function updateFields()
{
    var data = JSON.parse(document.getElementById("smit_data").innerHTML);
    for(var key in data) update(key, data[key]);
}

window.onload = updateFields;
