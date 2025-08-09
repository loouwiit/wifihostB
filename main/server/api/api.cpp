#include "stringCompare.hpp"
#include "http.hpp"
#include "pwm.hpp"
#include "tempture.hpp"
#include "wifi.hpp"
#include "wifi.inl"

constexpr char TAG[] = "api";

constexpr size_t wifiApRecordSize = 50;
constexpr char formatingPassword[] = "I know exactly what I'm doing";

extern bool serverRunning;

void restart();
void sendOk(OSocketStream& socketStream);
void api(IOSocketStream& socketStream, HttpRequest& request, size_t uriOffset, size_t uriRemain);

static void apiFloor(OSocketStream& socketStream, const char* path);

void api(IOSocketStream& socketStream, HttpRequest& request, size_t uriOffset, size_t uriRemain)
{
	const char* uri = request.getPath() + uriOffset;
	const size_t uriLenght = uriRemain;

	if (stringCompare((char*)uri, uriLenght, "/setLightLevel", 14))
	{
		char channel[10] = "";
		char duty[10] = "";
		socketStream.getline(channel, sizeof(channel));
		socketStream.getline(duty, sizeof(duty));

		setPWMDuty((ledc_channel_t)atoi((const char*)channel), atoi((const char*)duty), 100);

		sendOk(socketStream);
		return;
	}
	else if (stringCompare((char*)uri, uriLenght, "/getLightLevel", 14))
	{
		char channel[10] = "";
		socketStream.getline(channel, sizeof(channel));

		HttpRespond respond;
		{
			respond.cookies.clear();

			respond.heads.clear();
			respond.heads.add({ "Content-Type", " text/plain; charset=utf-8" });
		}
		char string[35] = "";
		size_t bodyLenght = sprintf(string, "%ld", getPWMDuty((ledc_channel_t)atoi(channel)));
		respond.setBody(string);
		respond.setBodyLenght(bodyLenght);

		respond.send(socketStream);
	}
	else if (stringCompare((char*)uri, uriLenght, "/getTemperature", 15))
	{
		float temp = getTemperature();
		HttpRespond respond;
		{
			respond.cookies.clear();

			respond.heads.clear();
			respond.heads.add({ "Content-Type", " text/plain; charset=utf-8" });
		}
		if (temp != FLT_MAX)
		{
			char string[20] = "";
			size_t bodyLenght = sprintf(string, "%f", temp);
			respond.setBody(string);
			respond.setBodyLenght(bodyLenght);
		}
		else
		{
			respond.setStatus(HttpStatus::InternalServerError);
			respond.setReason(HttpReason::InternalServerError);
			respond.setBody((void*)HttpReason::InternalServerError);
			respond.setBodyLenght(sizeof(HttpReason::InternalServerError) - 1);
		}

		respond.send(socketStream);
	}
	else if (stringCompare((char*)uri, uriLenght, "/floor", 6))
	{
		size_t pathSize = sizeof(PerfixRoot) + request.getBodyLenght();
		char* path = new char[pathSize];
		strcpy(path, PerfixRoot);
		socketStream.read(path + sizeof(PerfixRoot) - 1, pathSize - sizeof(PerfixRoot) + 1);
		apiFloor(socketStream, path);
		delete[] path;
	}
	else if (stringCompare((char*)uri, uriLenght, "/getSpace", 9))
	{
		uint64_t free = 0;
		uint64_t totol = 0;

		char buffer[41] = "";
		size_t count = 0;

		getSpace(free, totol);

		count = sprintf(buffer, "%llu,%llu", free, totol);

		HttpRespond respond;
		respond.setBody(buffer);
		respond.setBodyLenght(count);

		respond.send(socketStream);
	}
	else if (stringCompare((char*)uri, uriLenght, "/wifiScan", 9))
	{
		if (!wifiStationIsStarted())
		{
			// 发送错误
			HttpRespond respond;
			respond.setBody((void*)"ERROR:wifi not started");
			respond.setBodyLenght(22);
			respond.send(socketStream);
			return;
		}

		// 扫描
		wifi_ap_record_t* wifi = new wifi_ap_record_t[wifiApRecordSize]; // 96 bytes per record
		size_t count = wifiStationScan(wifi, wifiApRecordSize);

		// 统计长度
		// unsigned char wifiRssiLenght[wifiApRecordSize] = {0};
		// rssi假定为3位，“-85”
		size_t bodyLenght = 0;
		for (size_t i = 0;i < count;i++)
		{
			bodyLenght += strlen((char*)wifi[i].ssid) + 5;//rssi的3位，':'的1位，'\n'的1位
		}

		// 构建body
		char* body = new char[bodyLenght + 1];
		size_t pointer = 0;
		for (size_t i = 0;i < count;i++)
		{
			pointer += sprintf(body + pointer, "%d:%s\n", (int)wifi[i].rssi, (char*)wifi[i].ssid);
		}

		// 发送数据
		HttpRespond respond;
		respond.setBody(body);
		respond.setBodyLenght(pointer);
		respond.send(socketStream);

		// 清理内存
		delete[] body;
		body = nullptr;

		delete[] wifi;
		wifi = nullptr;
	}
	else if (stringCompare((char*)uri, uriLenght, "/wifiStart", 10))
	{
		if (!wifiStationIsStarted()) wifiStationStart();

		if (request.bodyLenght != 0)
		{
			char ssid[33];
			char password[64];
			socketStream.getline(ssid, sizeof(ssid));
			socketStream.getline(password, sizeof(password));
			wifiConnect(ssid, password);
		}
		else
		{
			wifiConnect(WIFISSID, WIFIPASSWORD);
		}
		sendOk(socketStream);
	}
	else if (stringCompare((char*)uri, uriLenght, "/wifiStop", 9))
	{
		sendOk(socketStream);
		socketStream.sendNow();
		if (wifiStationIsStarted()) wifiStationStop();
	}
	else if (stringCompare((char*)uri, uriLenght, "/apStart", 8))
	{
		if (!wifiApIsStarted()) wifiApStart();

		if (request.bodyLenght != 0)
		{
			char ssid[33];
			char password[64];
			socketStream.getline(ssid, sizeof(ssid));
			socketStream.getline(password, sizeof(password));
			wifiApSet(ssid, password, APAUTHENTICATEMODE);
		}
		else
		{
			wifiApSet(APSSID, APPASSWORD, APAUTHENTICATEMODE);
		}
		sendOk(socketStream);
	}
	else if (stringCompare((char*)uri, uriLenght, "/apStop", 7))
	{
		socketStream.sendNow();
		sendOk(socketStream);
		if (wifiApIsStarted()) wifiApStop();
	}
	else if (stringCompare((char*)uri, uriLenght, "/serverOff", 10))
	{
		sendOk(socketStream);
		socketStream.sendNow();
		vTaskDelay(100 / portTICK_PERIOD_MS);
		socketStream.close();

		stopTemperature();
		wifiStop();
		serverRunning = false;
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		startTemperature();
	}
	else if (stringCompare((char*)uri, uriLenght, "/formatFlash", 12))
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

static void apiFloor(OSocketStream& socketStream, const char* path)
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
		if (buffer == nullptr)
		{
			printf("server: apiFloor: read null in floor %s", path);
			totolSize = 0;
			contentCount[0] = 0;
			contentCount[1] = 0;
			continue;
		}
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
		if (buffer == nullptr)
		{
			printf("server: apiFloor: read null in floor %s", path);
			totolSize = 0;
			contentCount[0] = 0;
			contentCount[1] = 0;
			continue;
		}
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
}
