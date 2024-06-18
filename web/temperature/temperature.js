let temperatureElement = document.getElementById("temperature");

let temperatureNextUpdateTime = 0;
const temperatureUpdateTime = 1000;
const temperatureAutoUpdateTime = 10000;
const temperatureTimeOutTime = 2000;
const temperatureTimeOutAddTime = 28000;

function getTemperatureCallback(temperature) {
	var date = new Date();
	temperatureNextUpdateTime = date.getTime() + temperatureUpdateTime; //防止更新
	temperatureElement.innerHTML = "温度:" + temperature.toString();
}

function temperatureGet() {
	var date = new Date();
	if (date.getTime() > temperatureNextUpdateTime) {
		temperatureNextUpdateTime = date.getTime() + temperatureUpdateTime;

		var request = new XMLHttpRequest();
		request.open("POST", "/api/getTemperature");
		request.timeout = temperatureTimeOutTime;
		request.ontimeout = function () { console.log("getTemperature time out"); temperatureNextUpdateTime += temperatureTimeOutAddTime + temperatureTimeOutTime; temperatureElement.innerHTML = "获取温度时超时，请等待服务器恢复" };
		request.onload = function () { getTemperatureCallback(parseFloat(this.responseText)); };
		request.send("");
	}
}

document.getElementById("temperatureGet").addEventListener("click", temperatureGet);
console.log("\"temperature.js\" loaded");
temperatureGet();
let task = window.setInterval(temperatureGet, temperatureAutoUpdateTime);
