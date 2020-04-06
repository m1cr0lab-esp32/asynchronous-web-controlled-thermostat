/**
 * ----------------------------------------------------------------------------
 * ESP32 Web Controlled Thermostat
 * ----------------------------------------------------------------------------
 * Author: Stéphane Calderoni
 * Date:   April 2020
 * ----------------------------------------------------------------------------
 * This project is a response to a request made on the RNT Lab forum:
 * https://rntlab.com/question/java-script-code-to-refresh-home-page-only-once/
 * ----------------------------------------------------------------------------
 */

// ----------------------------------------------------------------------------
// Global constants
// ----------------------------------------------------------------------------

// Periodic temperature reading delay
const temperatureCaptureTime = 10000; // 10 seconds (in milliseconds)

// ----------------------------------------------------------------------------
// Global variables
// ----------------------------------------------------------------------------

// Default asynchronous request manager (classical AJAX method)
var xhttp = new XMLHttpRequest();

/**
 * We will need to read some data or update some elements of the HTML page.
 * So we need to define variables to reference them more easily throughout 
 * the program.
 */

// Current temperature display screen
// ----------------------------------

var screen;      // Container
var temperature; // HTML element that incorporates the current temperature value
var unit;        // HTML element that incorporates the display of the temperature reading unit (°C)

// Input fields for temperature limits
// -----------------------------------

var upper;
var lower;

// Current time display
// --------------------

var time;

// ESP32 control buttons
// ---------------------

var btnDefault;
var btnReboot;

// ----------------------------------------------------------------------------
// Initialization on full loading of the HTML page
// ----------------------------------------------------------------------------

window.addEventListener('load', onLoad);

function onLoad(event) {
    initTemperatureDisplay();
    initTime();
    initThresholds();
    initButtons();
    initProbe();
    showPanel();
}

// ----------------------------------------------------------------------------
// Progressive appearance of the control panel when everything is ready
// ----------------------------------------------------------------------------

function showPanel() {
    let panel = document.getElementById('panel');
    panel.classList.add('showing');
    panel.addEventListener('animationend', () => { panel.style.opacity = 1; });
}

// ----------------------------------------------------------------------------
// Initialization of the current temperature display
// ----------------------------------------------------------------------------

function initTemperatureDisplay() {
    screen      = document.getElementById('screen');
    temperature = document.getElementById('temperature');
    unit        = document.getElementById('unit');
}

// ----------------------------------------------------------------------------
// Initialization of the current time display
// ----------------------------------------------------------------------------

function initTime() {
    time = document.getElementById('time');
    updateTime();
    // a timing event manager is initialized
    // which must be triggered every second to refresh
    // the display of the current time
    setInterval(updateTime, 1000);
}

function updateTime() {
    let now    = new Date();
    let h      = now.getHours();
    let m      = now.getMinutes();
    let s      = now.getSeconds();
    time.value = `${normalize(h)}:${normalize(m)}:${normalize(s)}`;
}

function normalize(digit) {
    return (digit < 10 ? '0' : '') + digit;
}

// ----------------------------------------------------------------------------
// Initialization and control of the input fields for temperature thresholds
// ----------------------------------------------------------------------------

function initThresholds() {
    lower = document.getElementById('lower');
    upper = document.getElementById('upper');
}

// Event managers related to the entry of new temperature thresholds
// -----------------------------------------------------------------

// Once the entry is complete
// --------------------------

function validate() {
    let low = Number.parseInt(lower.value);
    let upp = Number.parseInt(upper.value);
    let min = Number.parseInt(lower.dataset.min);
    let max = Number.parseInt(upper.dataset.max);
    lower.value = Math.max(Math.min(low, upp), min);
    upper.value = Math.min(Math.max(low, upp), max);
    // color may change...
    setTemperature(temperature.innerText);
}

// While the user is entering a value
// ----------------------------------

function digitOnly(event) {
    if (event.keyCode == 13) {
        validate();
        return false;
    }
    return /[\d-]/.test(event.key);
}

// ----------------------------------------------------------------------------
// Initialization and handling of the ESP32 control buttons
// ----------------------------------------------------------------------------

function initButtons() {
    btnDefault = document.getElementById('default');
    btnReboot  = document.getElementById('reboot');
    btnDefault.addEventListener('click', onDefault);
    btnReboot.addEventListener('click', onReboot);
}

// Event manager for sending temperature thresholds
// ------------------------------------------------

function onDefault(event) {
    let low = Number.parseInt(lower.value);
    let upp = Number.parseInt(upper.value);

    // asynchronous call of the remote routine with the classical method
    xhrRequest(`/setdefaults?lower=${low}&upper=${upp}`);

    // for a more modern method, you can instead call this manager:
    // asyncAwaitRequest(`/setdefaults?lower=${low}&upper=${upp}`);
}

// Event manager for restarting ESP32
// ----------------------------------

function onReboot(event) {
    xhrRequest('/boot');
}

// ----------------------------------------------------------------------------
// Initialization and handling of the temperature sensor
// ----------------------------------------------------------------------------

function initProbe() {
    setTemperature(temperature.innerText);
    setInterval(getTemperature, temperatureCaptureTime);
}

// Sending the current temperature reading request
// -----------------------------------------------

function getTemperature() {
    /**
     * A `setTemperature()` callback function is designated in the following
     * to update the temperature display when the ESP32 has transmitted the response.
     */

    // asynchronous call of the remote routine with the classical method
    xhrRequest('/temp', (temp) => { setTemperature(temp); });

    // for a more modern method, you can instead call this manager:
    // asyncAwaitRequest('/temp', (temp) => { setTemperature(temp); });
}

// Updating the display when the value read on the sensor is received
// ------------------------------------------------------------------

function setTemperature(temp) {
    
    if (temp == 'Error') {
        temperature.innerText = temp;
        unit.style.display = 'none';
        screen.className = 'error';
    } else {

        let low = Number.parseInt(lower.value);
        let upp = Number.parseInt(upper.value);
        let t   = Number.parseFloat(temp).toFixed(1);

        if (t < low) {
            screen.className = 'cold';
        } else if (t > upp) {
            screen.className = 'hot';
        } else {
            screen.className = '';
        }

        temperature.innerText = t;
        unit.style.display = 'inline';
    }
}

// -------------------------------------------------------
// AJAX requests
// -------------------------------------------------------

// Using standard vanilla XHR (XMLHttpRequest method)
// @see https://www.w3schools.com/xml/ajax_xmlhttprequest_send.asp

function xhrRequest(path, callback) {
    
    xhttp.onreadystatechange = function() {
        if (this.readyState == 4 && this.status == 200) {
            // callback is optional!
            typeof callback === 'function' && callback(this.responseText);
        }
    };

    xhttp.open('GET', path, true);
    xhttp.send();
}

// Using Async/Await Promises
// @see: https://medium.com/@mattburgess/how-to-get-data-with-javascript-in-2018-f30ba04ad0da

function asyncAwaitRequest(path, callback) {
    (async () => {
        let response = await fetch(path);
        let temp     = await response.text();
        // callback is optional!
        typeof callback === 'function' && callback(temp);
    })();
}