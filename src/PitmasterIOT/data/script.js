var gateway = `ws://${window.location.hostname}/ws`;
var websocket;

// Init web socket when the page loads
window.addEventListener('load', onload);

document.getElementById('customSlider').addEventListener('input', function() {
    const values = [0, 30, 50, 70, 100];
    const selectedValue = values[this.value];
    console.log(`Selected Value: ${selectedValue}`);
    setFanSpeed(selectedValue);
});

function setFanSpeed(speed) {
    // Send a message to the server to set the fan speed
    websocket.send(JSON.stringify({ event: "setFanSpeed", speed: speed }));
}

document.getElementById('hamburgerIcon').addEventListener('click', toggleConfig);

function onload(event) {
    initWebSocket();
}

function getReading(){
    websocket.send("getReading");
}

function initWebSocket() {
    console.log('Trying to open a WebSocket connection...');
    websocket = new WebSocket(gateway);
    websocket.onopen = onOpen;
    websocket.onclose = onClose;
    websocket.onmessage = onMessage;
}

function onOpen(event) {
    console.log('Connection opened');
    getReading();
}

function onClose(event) {
    console.log('Connection closed');
    setTimeout(initWebSocket, 2000);
}

function onMessage(event) {
    console.log(event.data);
    var myObj = JSON.parse(event.data);

    if(myObj.hasOwnProperty('tempAdjust_0')){
        const thermoConfigVal = myObj.tempAdjust_0;
        console.log(`Configuration Value: ${thermoConfigVal}`);
        document.getElementById('tempAdjust_0').value = thermoConfigVal;
    }

    if(myObj.hasOwnProperty('temperature_0')){
        const temperature_0 = myObj?.temperature_0 ?? 0;
        const temperature_1 = myObj?.temperature_1 ?? 0;
        document.getElementById("temperature_0").innerHTML = temperature_0.toFixed(1);
        document.getElementById("temperature_1").innerHTML = temperature_1.toFixed(1);
    }
   
    // avoid race condition when config is passed in, but user is modifying it first.
    if(myObj.thermoKey != null && document.getElementById("configModal").style.display == "none"){
        // "{"thermoKey":-151}"
        if(document.getElementById('tempAdjust_0').value != myObj.thermoKey){
            document.getElementById('tempAdjust_0').value = myObj.thermoKey;
        }
    }
}

function toggleConfig() {
    var modal = document.getElementById('configModal');
    var modalContent = modal.querySelector('.modal-content');
  
    if (modal.style.display === "none" || modal.style.display === "") {
        modal.style.display = "block";
        modalContent.classList.add('slide-in'); 
        modalContent.classList.remove('slide-out');
    } else {
        modalContent.classList.add('slide-out'); 
        modalContent.classList.remove('slide-in'); 
        setTimeout(function() {
          modal.style.display = "none";
      }, 500); // This should match the duration of the slide-out animation
    }
}

function SaveConfiguration() {
    // get values and create an event to send to backend...
    var tempAdjust_0 = document.getElementById('tempAdjust_0').value ?? 0;
    var tempPolyCorrection_0 = document.getElementById('tempPolyCorrection_0').value ?? 0;
    var tempAdjust_1 = document.getElementById('tempAdjust_1').value ?? 0;
    var tempPolyCorrection_1 = document.getElementById('tempPolyCorrection_1').value ?? 0;

    console.log(tempAdjust_0);

    //let configObj = { "tempAdjust_0":  thermo };
    let configObj = { 
        "tempAdjust_0":  tempAdjust_0,
        "tempPolyCorrection_0":  tempPolyCorrection_0,
        "tempAdjust_1":  tempAdjust_1,
        "tempPolyCorrection_1":  tempPolyCorrection_1,
    };

    websocket.send(JSON.stringify({ event: "updateConfiguration", configuration: configObj })); 
    alert("Configuration Saved!");
    toggleConfig();
}

