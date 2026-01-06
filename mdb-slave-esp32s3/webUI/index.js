function saveClicked(evt) {
    console.log("save clicked")
    // Eine Instanz des FormDate-Objekts anlegen
    var ssid = document.getElementById("wifi_ssid").value
    var password = document.getElementById("wifi_password").value
    var mqttServer = document.getElementById("mqtt_server").value

    const formObj = {
        ssid,
        password,
        mqttServer
    }

    console.log(formObj)

    const settings = {
        'url': '/api/v1/settings/set',
        'method': 'POST',
        'timeout': 0,
        'headers': {
            'Content-Type': 'application/json'
        },
        'data': JSON.stringify({
            'ssid': document.getElementById('wifi_ssid').value,
            'password':  document.getElementById('wifi_password').value,
            'mqtt_server':  document.getElementById('mqtt_server').value,
        })
    }

    $.ajax(settings).done(function (response) {
        console.log(response)
    })
}

window.addEventListener('DOMContentLoaded',function () {
    $.ajax
        .get('/api/v1/system/info')
        .then(response => {
            const data = response.data
            document.getElementById("wifi_ssid").value = data.wifi_ssid
            document.getElementById("wifi_password").value = data.wifi_password
            document.getElementById("mqtt_server").value = data.mqtt_server
        })
        .catch(error => {
            console.error(error)
        })
});
