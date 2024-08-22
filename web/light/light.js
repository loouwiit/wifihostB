let lightChannelElement = document.getElementById("lightChannelSelect");
let lightChannelLabelElement = document.getElementById("lightChannelLabel");

let lightLevelElement = document.getElementById("lightLevelRange");
let lightLevelLabelElement = document.getElementById("lightLevelLabel");

function sendLightLevel(lightChannel, lightLevel) {
	var request = new XMLHttpRequest();
	request.open("POST", "/api/setLightLevel");
	request.send(lightChannel.toString() + '\n' + lightLevel.toString());
}

function lightChannelChanged() {
	lightChannelLabelElement.innerHTML = "通道:" + lightChannelElement.valueAsNumber;
	lightLevelLabelElement.innerHTML = "等级:";
	lightLevelGet();
}

function lightLevelChanged() {
	lightLevelLabelElement.innerHTML = "等级:" + lightLevelElement.valueAsNumber;
}

function lightLevelSet() {
	sendLightLevel(lightChannelElement.valueAsNumber, lightLevelElement.valueAsNumber);
}

function getLevelCallback(level) {
	lightLevelElement.valueAsNumber = level;
	lightLevelLabelElement.innerHTML = "等级:" + lightLevelElement.valueAsNumber;
}

function lightLevelGet() {
	var request = new XMLHttpRequest();
	request.open("POST", "/api/getLightLevel");
	request.onload = function () { getLevelCallback(parseInt(this.responseText)); };
	request.send(lightChannelElement.valueAsNumber.toString());
}

lightChannelElement.addEventListener("input", lightChannelChanged);
lightLevelElement.addEventListener("input", lightLevelChanged);
lightLevelElement.addEventListener("change", lightLevelSet);
document.getElementById("lightLevelSet").addEventListener("click", lightLevelSet);
document.getElementById("lightLevelGet").addEventListener("click", lightLevelGet);
console.log("\"light.js\" loaded");
lightChannelChanged();
