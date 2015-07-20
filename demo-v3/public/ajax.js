function ajaxSend(url, method) {
    var request = new XMLHttpRequest();
    request.open(method, url, false); // synchronous
    request.send(null);
    var status = request.status;
    if (status == 200) return ['ok', request.responseText];
    else return ['error', request.responseText];
}
