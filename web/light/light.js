	let lightLevelRangeElement = document.getElementById("lightLevelRange");
	let lightLevelLabelElement = document.getElementById("lightLevelLabel");
	let lightLevelNextUpdateTime = 0;
	const lightLevelUpdateTime = 200;
	
	function sendLightLevel(lightLevel) {
		var request = new XMLHttpRequest();
		request.open("POST", "/api/setLightLevel");
		request.send(lightLevel.toString());
	}

	function lightLevelChangedForce() {
		lightLevelLabelElement.innerHTML = "光亮等级:" + lightLevelRangeElement.valueAsNumber;

		sendLightLevel(lightLevelRangeElement.valueAsNumber);
	}

	function lightLevelChanged() {
		lightLevelLabelElement.innerHTML = "光亮等级:" + lightLevelRangeElement.valueAsNumber;

		var date = new Date();
		if (date.getTime() > lightLevelNextUpdateTime) {
			lightLevelNextUpdateTime = date.getTime() + lightLevelUpdateTime;
			sendLightLevel(lightLevelRangeElement.valueAsNumber);
		}
	}

	function lightLevelAdd() {
		lightLevelRangeElement.valueAsNumber += 1;
		lightLevelChangedForce();
	}

	function lightLevelSub() {
		lightLevelRangeElement.valueAsNumber -= 1;
		lightLevelChangedForce();
	}

	function serverOff() {
		var request = new XMLHttpRequest();
		request.open("POST", "/api/serverOff");
		request.send("");
	}

	lightLevelRangeElement.addEventListener("input", lightLevelChanged);
	lightLevelRangeElement.addEventListener("change", lightLevelChangedForce);
	document.getElementById("lightLevelMore").addEventListener("click", lightLevelAdd);
	document.getElementById("lightLevelLess").addEventListener("click", lightLevelSub);
	document.getElementById("serverOff").addEventListener("click", serverOff);
	console.log("\"light.js\" loaded");
