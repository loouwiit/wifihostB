#include <time.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_task.h>
#include <lwip/sockets.h>

#include "tempture.hpp"
#include "server.hpp"
#include "http.hpp"
#include "wifi.hpp"
#include "tempture.hpp"
#include "mDns.hpp"

#include "pwm.hpp"
#include "buildinHtml/file.hpp"
#include "fat.hpp"

static uint8_t autoRestartTimes = 0;
static uint8_t maxRestartTimes = 0;

constexpr char MDnsName[] = "esp32s3";
constexpr char formatingPassword[] = "I know exactly what I'm doing";
constexpr size_t PutMaxSize = 1024 * 1024;
constexpr size_t PutBufferSize = 64;

bool serverRunning = false;

void server(void*);
void recieve(IOSocketStream& socketStream);
void sendOk(OSocketStream& socketStream);
void httpGet(IOSocketStream& socketStream, HttpRequest& request);
void httpPost(IOSocketStream& socketStream, HttpRequest& request);
void httpPut(IOSocketStream& socketStream, HttpRequest& request);
void httpDelete(IOSocketStream& socketStream, HttpRequest& request);
void restart();
bool prefixCompare(const char* string, size_t stringLength, const char* prefix, size_t prefixLength);
bool stringCompare(const char* string, size_t stringLength, const char* standard, size_t standardLength);

void apiFloor(OSocketStream& socketStream, const char* path);

void startServer(uint8_t maxAutoRestartTimes)
{
	autoRestartTimes = 0;
	maxRestartTimes = maxAutoRestartTimes;
	xTaskCreate(server, "serverTask", 4096, nullptr, 10, NULL);
	mDnsStart(MDnsName);
}

void server(void*)
{
	int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	if (listen_sock < 0)
	{
		printf("server:socket creat failed: error %d\n", listen_sock);
		vTaskDelete(NULL);
		return;
	}

	int opt = 1;
	setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	printf("socket created\n");

	sockaddr_storage addr = {};
	{
		struct sockaddr_in* dest_addr_ip4 = (struct sockaddr_in*)&addr;
		dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
		dest_addr_ip4->sin_family = AF_INET;
		dest_addr_ip4->sin_port = htons(80);
	}

	int err;
	err = bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr));
	if (err != 0)
	{
		printf("server: socket bind failed: errno %d\n", errno);
		vTaskDelete(NULL);
		return;
	}
	printf("server:socket bound, port 80\n");

	err = listen(listen_sock, 1);
	if (err != 0)
	{
		printf("server: socket listen failed: errno %d\n", errno);
		vTaskDelete(NULL);
		return;
	}

	IOSocketStream socketStream;

	while (serverRunning)
	{
		printf("server: socket listening\n");

		struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
		socklen_t addr_len = sizeof(source_addr);
		int sock = accept(listen_sock, (struct sockaddr*)&source_addr, &addr_len);
		if (sock < 0)
		{
			printf("server: Unable to accept connection: errno %d\n", errno);
			restart();
			break;
		}

		// Set tcp keepalive option
		int keepAlive = 1;
		int keepIdle = 5;
		int keepInterval = 5;
		int keepCount = 3;
		setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
		setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
		setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
		setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));

		// Convert ip address to string
		char addr_str[128];
		if (source_addr.ss_family == PF_INET)
		{
			inet_ntoa_r(((sockaddr_in*)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
		}

		printf("server: socket accepted ip address: %s\n", addr_str);

		socketStream.setSocket(sock);
		recieve(socketStream);
		vTaskDelay(100 / portTICK_PERIOD_MS); // 确保完全发送

		shutdown(sock, 0);
		close(sock);
	}

	close(listen_sock);
	vTaskDelete(NULL);
}

void recieve(IOSocketStream& socketStream)
{
	HttpRequest request;
	request.receive(socketStream);

	// print request infomation
	{
		printf("server::recieve: method = %s\n", getMethodNameFromMethod(request.getMethod()));
		printf("server::recieve: path = %s\n", request.getPath());

		HttpStringPairSize_t headNumber = request.heads.getNumber();
		for (HttpStringPairSize_t i = 0; i < headNumber; i++)
		{
			const char* name = request.heads[i].getName();
			const char* value = request.heads[i].getValue();
			printf("server::recieve: head[%d] is %s : %s\n", i, name, value);
		}

		HttpStringPairSize_t cookieNumber = request.cookies.getNumber();
		for (HttpStringPairSize_t i = 0; i < cookieNumber; i++)
		{
			printf("server::recieve: cookie[%d] is %s = %s\n", i, request.cookies[i].getName(), request.cookies[i].getValue());
		}

		printf("server::recieve: body lenght = %u\n", request.getBodyLenght());
	}

	switch (request.getMethod())
	{
	default:
	case HttpMethod::Get:
		httpGet(socketStream, request);
		break;
	case HttpMethod::Post:
		httpPost(socketStream, request);
		break;
	case HttpMethod::Put:
		httpPut(socketStream, request);
		break;
	case HttpMethod::Delete:
		httpDelete(socketStream, request);
		break;
	}

	if (!socketStream.isGood())
		printf("server::recieve: socketStream error\n");
}

void sendOk(OSocketStream& socketStream)
{
	HttpRespond respond;
	respond.setBody((void*)HttpReason::Ok);
	respond.setBodyLenght(sizeof(HttpReason::Ok) - 1);
	respond.setStatus(HttpStatus::Ok);
	respond.setReason(HttpReason::Ok);
	respond.send(socketStream);
}

void httpGet(IOSocketStream& socketStream, HttpRequest& request)
{
	const char* uri = (!(request.getPath()[0] == '/' && request.getPath()[1] == '\0')) ? (request.getPath()) : ("/index.html");

	HttpContentType contentType = getContentTypeFromPath(uri);
	HttpRespond respond;

	{
		respond.cookies.clear();

		respond.heads.clear();
		respond.heads.add({ "Content-Type", getContentTypeNameFromContentType(contentType) });
	}

	if (prefixCompare(uri, strlen(uri), "/file", 5) || prefixCompare(uri, strlen(uri), "/File", 5) || prefixCompare(uri, strlen(uri), "/FILE", 5))
	{
		respond.setBody((void*)HtmlFile);
		respond.setBodyLenght(sizeof(HtmlFile) - 1);
		respond.send(socketStream);
	}
	else
	{
		IFile file;
		char* path = new char[sizeof(FlashPath) + strlen(uri)];
		sprintf(path, "%s%s", FlashPath, uri);
		file.open(path);
		delete[] path;
		if (file.isOpen())
		{
			respond.setBody(file);
			file.reGetSize();
			respond.setBodyLenght(file.getSize());

			respond.setStatus(HttpStatus::Ok);
			respond.setReason(HttpReason::Ok);
		}
		else
		{
			respond.setBody((void*)HttpReason::NotFound);
			respond.setBodyLenght(sizeof(HttpReason::NotFound) - 1);

			respond.setStatus(HttpStatus::NotFound);
			respond.setReason(HttpReason::NotFound);
		}
		respond.send(socketStream);
	}

	socketStream.sendNow();
}

void httpPost(IOSocketStream& socketStream, HttpRequest& request)
{
	const char* uri = request.getPath();

	if (stringCompare((char*)uri, strlen(uri), "/api/setLightLevel", 18))
	{
		HttpRespond respond;

		{
			respond.cookies.clear();

			respond.heads.clear();
			respond.heads.add({ "Content-Type", " text/plain; charset=utf-8" });
		}
		char body[10] = "";
		socketStream.read(body, sizeof(body));
		setPWMDuty(atoi((const char*)body), 100);

		sendOk(socketStream);
		return;
	}
	else if (stringCompare((char*)uri, strlen(uri), "/api/serverOff", 14))
	{
		HttpRespond respond;

		{
			respond.cookies.clear();

			respond.heads.clear();
			respond.heads.add({ "Content-Type", " text/plain; charset=utf-8" });
		}

		sendOk(socketStream);
		vTaskDelay(100 / portTICK_PERIOD_MS);

		wifiDisconnect();
		wifiStop();
		startTemperature();
		serverRunning = false;

		return;
	}
	else if (stringCompare((char*)uri, strlen(uri), "/api/formatFlash", 16))
	{
		HttpRespond respond;
		{
			respond.cookies.clear();

			respond.heads.clear();
			respond.heads.add({ "Content-Type", " text/plain; charset=utf-8" });
		}

		char body[request.bodyLenght + 1] = "";
		size_t bodyLength = socketStream.read(body, request.bodyLenght + 1);
		printf("formating password input: %s\n", body);
		if (stringCompare((char*)body, bodyLength, formatingPassword, sizeof(formatingPassword) - 1))
		{
			printf("formating flash\n");
			formatFlash();
			respond.setBody((void*)"format successfully");
			respond.setBodyLenght(19);
		}
		else
		{
			printf("formating password wrong\n");
			respond.setBody((void*)"wrong password");
			respond.setBodyLenght(14);
		}

		respond.send(socketStream);
		socketStream.sendNow();
	}
	else if (stringCompare((char*)uri, strlen(uri), "/api/floor", 10))
	{
		size_t pathSize = sizeof(FlashPath) + request.getBodyLenght();
		char* path = new char[pathSize];
		strcpy(path, FlashPath);
		socketStream.read(path + sizeof(FlashPath) - 1, pathSize - sizeof(FlashPath) + 1);
		apiFloor(socketStream, path);
		delete[] path;
		return;
	}
	else
	{
		HttpRespond respond;

		respond.cookies.clear();

		respond.heads.clear();
		respond.heads.add({ "Content-Type", " text/plain; charset=utf-8" });

		respond.setStatus(HttpStatus::BadRequest);
		respond.setReason(HttpReason::BadRequest);
		respond.setBody((void*)HttpReason::BadRequest);
		respond.setBodyLenght(sizeof(HttpReason::BadRequest));
		respond.send(socketStream);

		socketStream.sendNow();
		return;
	}
}

void httpPut(IOSocketStream& socketStream, HttpRequest& request)
{
	const char* uri = request.getPath();
	char* path = new char[sizeof(FlashPath) + strlen(uri)];
	size_t pathLength = sprintf(path, "%s%s", FlashPath, uri);

	if (path[pathLength - 1] == '/')
	{
		//floor
		newFloor(path);
		tree(FlashPath); //[debug]
		sendOk(socketStream);
		return;
	}
	else
	{
		//file
		if (request.getBodyLenght() > PutMaxSize)
		{
			printf("server: put file is too large\n");
			sendOk(socketStream);
			return;
		}

		newFile(path);
		OFile file;
		file.open(path);
		if (!file.isOpen())
		{
			printf("server: error in put file %s\n", path);
			sendOk(socketStream);
			return;
		}

		size_t left = request.getBodyLenght();
		size_t readCount = 0;
		char buffer[PutBufferSize] = "";

		while (left > 0)
		{
			readCount = socketStream.readByte(buffer, PutBufferSize);
			file.write(buffer, readCount);
			left -= readCount;
		}

		tree(FlashPath); //[debug]
		sendOk(socketStream);
	}
}

void httpDelete(IOSocketStream& socketStream, HttpRequest& request)
{
	const char* uri = request.getPath();
	char* path = new char[sizeof(FlashPath) + strlen(uri)];
	size_t pathLength = sprintf(path, "%s%s", FlashPath, uri);

	if (path[pathLength - 1] == '/')
	{
		//floor
		removeFloor(path);
	}
	else
	{
		//file
		removeFile(path);
	}
	tree(FlashPath); //[debug]
	sendOk(socketStream);
}

void restart()
{
	autoRestartTimes++;
	if (autoRestartTimes < maxRestartTimes)
	{
		printf("server: restart at %d times\n", autoRestartTimes);
		xTaskCreate(server, "serverTask", 4096, nullptr, 10, NULL);
	}
	else
	{
		printf("restart %u times, which is the max times\n", autoRestartTimes);
	}
}

bool prefixCompare(const char* string, size_t stringLength, const char* prefix, size_t prefixLength)
{
	if (stringLength < prefixLength) return false; // 确保string的长度小等于prefix的长度
	size_t i = 0;
	while (i < prefixLength)
	{
		if (string[i] == prefix[i]) i++;
		else return false;
	}
	return true;
}

bool stringCompare(const char* string, size_t stringLength, const char* standard, size_t standardLength)
{
	if (stringLength != standardLength) return false;
	size_t i = 0;
	while (i < stringLength)
	{
		if (string[i] == standard[i]) i++;
		else return false;
	}
	return true;
}

void apiFloor(OSocketStream& socketStream, const char* path)
{
	HttpRespond respond;
	respond.cookies.clear();
	respond.heads.clear();
	respond.heads.add({ "Content-Type", " text/plain; charset=utf-8" });

	Floor floor;
	floor.open(path);
	floor.reCount();

	size_t contentCount[2] = { floor.getCount(Floor::Type::Floor), floor.getCount(Floor::Type::File) };
	size_t totolSize = 0;
	const char* buffer = nullptr; //no need to free
	char* body = nullptr; //new later
	size_t bodyOffset = 0;

	//统计总字符数
	for (size_t i = 0; i < contentCount[0]; i++)
	{
		buffer = floor.read(Floor::Type::Floor);
		totolSize += strlen(buffer) + 2; // one for '/' and another for '\n'
	}

	floor.backToBegin();
	for (size_t i = 0; i < contentCount[1]; i++)
	{
		buffer = floor.read(Floor::Type::File);
		totolSize += strlen(buffer) + 1; // just one for '\n'
	}

	//写入内存
	floor.backToBegin();
	body = new char[totolSize]; //body is new here
	for (size_t i = 0;i < contentCount[0];i++)
	{
		buffer = floor.read(Floor::Type::Floor);
		for (size_t j = 0; buffer[j] != '\0'; j++)
		{
			body[bodyOffset] = buffer[j];
			bodyOffset++;
		}
		body[bodyOffset] = '/';
		bodyOffset++;
		body[bodyOffset] = '\n';
		bodyOffset++;
	}
	floor.backToBegin();
	for (size_t i = 0;i < contentCount[1];i++)
	{
		buffer = floor.read(Floor::Type::File);
		for (size_t j = 0; buffer[j] != '\0'; j++)
		{
			body[bodyOffset] = buffer[j];
			bodyOffset++;
		}
		body[bodyOffset] = '\n';
		bodyOffset++;
	}

	respond.setBody((void*)body);
	if (totolSize > 1) respond.setBodyLenght(totolSize - 1); //去除最后一个'\n'
	else respond.setBodyLenght(0);
	respond.send(socketStream);

	delete[] body; //body is deleted here

	socketStream.sendNow();
}
