function serverOff() {
	var request = new XMLHttpRequest();
	request.open("POST", "/api/serverOff");
	request.send("");
}

document.getElementById("serverOff").addEventListener("click", serverOff);
console.log("\"server.js\" loaded");
