#include <time.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_task.h>
#include <lwip/sockets.h>

#include "stringCompare.hpp"

#include "server.hpp"
#include "http.hpp"
#include "socketStreamWindow.hpp"
#include "wifi.hpp"
#include "wifi.inl"
#include "tempture.hpp"
#include "mDns.hpp"

#include "buildinHtml/file.hpp"
#include "fat.hpp"

static uint8_t autoRestartTimes = 0;
static uint8_t maxRestartTimes = 0;

constexpr char TAG[] = "server";

constexpr char ServerPath[] = "/server";
constexpr char MDnsName[] = "esp32s3";

constexpr size_t PutMaxSize = 1024 * 1024; //1M
constexpr size_t PutBufferSize = 512;

constexpr size_t socketStreamWindowNumber = 16;
constexpr size_t coworkerNumber = 5; //极限测试中刚刚跑不满的数量

bool serverRunning = false;
SocketStreamWindow socketStreamWindows[socketStreamWindowNumber];

void server(void*);
void serverCoworker(void*);
void recieve(IOSocketStream& socketStream);
void sendOk(OSocketStream& socketStream);
void httpGet(IOSocketStream& socketStream, HttpRequest& request);
void httpPost(IOSocketStream& socketStream, HttpRequest& request);
void httpPut(IOSocketStream& socketStream, HttpRequest& request);
void httpDelete(IOSocketStream& socketStream, HttpRequest& request);
void restart();

void startServer(uint8_t maxAutoRestartTimes)
{
	autoRestartTimes = 0;
	maxRestartTimes = maxAutoRestartTimes;
	xTaskCreate(server, "serverTask", 4096, nullptr, 10, NULL);
	for (size_t i = 0;i < coworkerNumber;i++)
	{
		xTaskCreate(serverCoworker, "serverCoTask", 4096, (void*)i, 11, NULL);
	}
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

		// 对访问网段进行限制
#if NetworkSegmentRestriction
		if (!(addr_str[0] == '1'
			&& addr_str[1] == '9'
			&& addr_str[2] == '2'
			&& addr_str[3] == '.'
			&& addr_str[4] == '1'
			&& addr_str[5] == '6'
			&& addr_str[6] == '8'
			&& addr_str[7] == '.'
			&& addr_str[8] == '4'
			&& addr_str[9] == '.'))
		{
			ISocketStream t{ sock };
			t.close();
			ESP_LOGI(TAG, "abandoned");
			continue;
		}
#endif

		size_t index = 0;
		while (!socketStreamWindows[index].setSocket(sock))
		{
			index++;
			if (index >= socketStreamWindowNumber)
			{
				index = 0;
				printf("server: all window was occupyed\n");
				vTaskDelay(100 / portTICK_PERIOD_MS);
			}
		}
		printf("SocketStreamWindow state: %d / %d\n", SocketStreamWindow::getEnabledCount(), socketStreamWindowNumber);
		vTaskDelay(1);
	}

	close(listen_sock);
	vTaskDelete(NULL);
}

void serverCoworker(void* coworkerIndex)
{
	static std::mutex scanMutex;
	static size_t scanPosition = 0;

	bool scanning = false;
	size_t index = 0;

	printf("Coworker %d started\n", (int)coworkerIndex);
	while (serverRunning)
	{
		//尝试获取扫描权
		//printf("coworker%d: waiting for scanning\n", (int)coworkerIndex);
		scanMutex.lock();

		//获取成功，加载起始点
		scanning = true;
		index = scanPosition;
		//printf("coworker%d: scanning from %d\n", (int)coworkerIndex, (int)scanPosition);

		while (scanning && serverRunning)
		{
			for (; index < socketStreamWindowNumber; index++)
			{

				//尝试激活每一个窗口
				if (!socketStreamWindows[index].enable()) continue;

				//已激活窗口，检查是否需要处理数据
				//printf("coworker%d: checking %d\n", (int)coworkerIndex, (int)index);
				if (socketStreamWindows[index].check())
				{
					//移交扫描权
					scanPosition = index + 1 % socketStreamWindowNumber;
					scanMutex.unlock();

					//处理数据
					//printf("coworker%d: working on window %d\n", (int)coworkerIndex, index);
					recieve(socketStreamWindows[index].getSocketStream());
					socketStreamWindows[index].disable(); //窗口已激活，现取消激活窗口

					//取消自身扫描
					index = socketStreamWindowNumber;
					scanning = false;
				}
				else
				{
					socketStreamWindows[index].disable(); //窗口已激活，现取消激活窗口
				}
			}
			index = 0;
			vTaskDelay(1); //一周下来都没有任务或刚处理完一个请求
		}

		// scanning && serverRunning不成立
		if (scanning)
		{
			// scanning == true, serverRunning == false
			// server关闭，移交扫描权，使其他线程发现关闭
			scanning = false;
			scanMutex.unlock();
		}
		// scanning == false，无需移交扫描权
	}
	printf("Coworker %d ended\n", (int)coworkerIndex);
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
		printf("\n");
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
	socketStream.sendNow();

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
		if (contentType != HttpContentType::Other)
			respond.heads.add({ "Content-Type", getContentTypeNameFromContentType(contentType) });
	}

	if (stringCompare(uri, strlen(uri), "/file", 5) || stringCompare(uri, strlen(uri), "/File", 5) || stringCompare(uri, strlen(uri), "/FILE", 5))
	{
		//管理页面
		respond.setBody((void*)HtmlFile);
		respond.setBodyLenght(sizeof(HtmlFile) - 1);
		respond.send(socketStream);
	}
	else
	{
		IFile file;
		char* path = nullptr;
		if (prefixCompare(uri, strlen(uri), "/file", 5) || prefixCompare(uri, strlen(uri), "/File", 5) || prefixCompare(uri, strlen(uri), "/FILE", 5))
		{
			//全部文件
			path = new char[sizeof(PerfixRoot) + strlen(uri) - 5];
			sprintf(path, "%s%s", PerfixRoot, uri + 5);
		}
		else
		{
			//转到serverPath下
			path = new char[sizeof(PerfixRoot) + sizeof(ServerPath) + strlen(uri)];
			sprintf(path, "%s%s%s", PerfixRoot, ServerPath, uri);
		}
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
		file.close();
	}
}

void httpPost(IOSocketStream& socketStream, HttpRequest& request)
{
	const char* uri = request.getPath();
	const size_t uriLenght = strlen(uri);

	if (prefixCompare((char*)uri, uriLenght, "/api", 4))
	{
		void api(IOSocketStream & socketStream, HttpRequest & request, size_t uriOffset, size_t uriRemain);
		api(socketStream, request, 4, uriLenght - 4);
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
		respond.setBodyLenght(sizeof(HttpReason::BadRequest) - 1);
		respond.send(socketStream);
		return;
	}
}

void httpPut(IOSocketStream& socketStream, HttpRequest& request)
{
	const char* uri = request.getPath();
	char* path = new char[sizeof(PerfixRoot) + strlen(uri)];
	size_t pathLength = sprintf(path, "%s%s", PerfixRoot, uri);

	if (path[pathLength - 1] == '/')
	{
		//floor
		newFloor(path);
		tree(PerfixRoot); //[debug]
		sendOk(socketStream);
	}
	else
	{
		//file
		if (request.getBodyLenght() > PutMaxSize || request.getBodyLenght() > getFreeSpace())
		{
			printf("server: put file is too large\n");

			HttpRespond respond;
			respond.setBody((void*)HttpReason::BadRequest);
			respond.setBodyLenght(sizeof(HttpReason::BadRequest) - 1);
			respond.setStatus(HttpStatus::BadRequest);
			respond.setReason(HttpReason::BadRequest);
			respond.send(socketStream);

			vTaskDelay(100 / portTICK_PERIOD_MS);
			socketStream.close();
			return;
		}

		newFile(path);
		OFile file;
		file.open(path);
		if (!file.isOpen())
		{
			printf("server: error in put file %s\n", path);

			HttpRespond respond;
			respond.setBody((void*)HttpReason::InternalServerError);
			respond.setBodyLenght(sizeof(HttpReason::InternalServerError) - 1);
			respond.setStatus(HttpStatus::InternalServerError);
			respond.setReason(HttpReason::InternalServerError);
			respond.send(socketStream);

			vTaskDelay(100 / portTICK_PERIOD_MS);
			socketStream.close();
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
			vTaskDelay(1);
		}

#if true
		tree(PerfixRoot); //[debug]
#endif

#if false
		printf("coworker:httpPut(): stack high water = %u\n", uxTaskGetStackHighWaterMark(nullptr));
#endif

		sendOk(socketStream);
	}
}

void httpDelete(IOSocketStream& socketStream, HttpRequest& request)
{
	const char* uri = request.getPath();
	char* path = new char[sizeof(PerfixRoot) + strlen(uri)];
	size_t pathLength = sprintf(path, "%s%s", PerfixRoot, uri);

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
	tree(PerfixRoot); //[debug]
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
		stopTemperature();
		wifiDisconnect();
		wifiStop();
		serverRunning = false;
		startTemperature();
	}
}
