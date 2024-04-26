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

#include "pwm.hpp"
#include "web.hpp"

static uint8_t autoRestartTimes = 0;
static uint8_t maxRestartTimes = 0;

bool serverRunning = false;

void server(void*);
void recieve(IOSocketStream& socketStream);
void get(IOSocketStream& socketStream, const char* uri, const void* body, size_t bodyLength);
void post(IOSocketStream& socketStream, const char* uri, const void* body, size_t bodyLength);
void restart();

void startServer(uint8_t maxAutoRestartTimes)
{
	autoRestartTimes = 0;
	maxRestartTimes = maxAutoRestartTimes;
	xTaskCreate(server, "serverTask", 4096, nullptr, 10, NULL);
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
		get(socketStream, request.getPath(), request.getBody(), request.getBodyLenght());
		break;
	case HttpMethod::Post:
		post(socketStream, request.getPath(), request.getBody(), request.getBodyLenght());
		break;
	}

	if (!socketStream.isGood())
		printf("server::recieve: socketStream error\n");
}

void get(IOSocketStream& socketStream, const char* uri, const void* body, size_t bodyLength)
{
	HttpRespond respond;

	{
		respond.cookies.clear();

		respond.heads.clear();
		respond.heads.add({ "Content-Type", " text/html; charset=utf-8" });
	}

	respond.body = (void*)LightLevelHTML;
	respond.setBodyLenght(sizeof(LightLevelHTML) - 1);
	respond.send(socketStream);

	socketStream.sendNow();
}

void post(IOSocketStream& socketStream, const char* uri, const void* body, size_t bodyLength)
{
	if (strcmp((char*)uri, "/api/setLightLevel") == 0)
	{
		HttpRespond respond;

		{
			respond.cookies.clear();

			respond.heads.clear();
			respond.heads.add({ "Content-Type", " text/plain; charset=utf-8" });
		}

		setPWMDuty(atoi((const char*)body), 100);

		respond.body = (char*)HttpReason::Ok;
		respond.setBodyLenght(sizeof(HttpReason::Ok) - 1);

		respond.send(socketStream);
		socketStream.sendNow();
		return;
	}
	else if (strcmp((char*)uri, "/api/serverOff") == 0)
	{
		HttpRespond respond;

		{
			respond.cookies.clear();

			respond.heads.clear();
			respond.heads.add({ "Content-Type", " text/plain; charset=utf-8" });
		}

		respond.body = (char*)HttpReason::Ok;
		respond.setBodyLenght(sizeof(HttpReason::Ok) - 1);

		respond.send(socketStream);
		socketStream.sendNow();

		wifiDisconnect();
		wifiStop();
		startTemperature();
		serverRunning = false;

		return;
	}
	else
	{
		HttpRespond respond;

		{
			respond.cookies.clear();

			respond.heads.clear();
			respond.heads.add({ "Content-Type", " text/plain; charset=utf-8" });
		}

		char buffer[10] = "";
		size_t bufferCount = sprintf(buffer, "%0ld", getPWMDuty());

		respond.body = buffer;
		respond.setBodyLenght(bufferCount);
		respond.send(socketStream);

		socketStream.sendNow();
		return;
	}
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
