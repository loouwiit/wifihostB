#include <mdns.h>
#include "mDns.hpp"

void mDnsAdd();

void mDnsStart(const char name[])
{
    // 初始化 mDNS 服务
    esp_err_t err = mdns_init();
    if (err) {
        printf("MDNS Init failed: %d\n", err);
        return;
    }

    // 设置 hostname
    mdns_hostname_set(name);
    // 设置默认实例
    mdns_instance_name_set("ESP32S3 server");

	mDnsAdd();

	printf("mDns added\n");
}

void mDnsAdd()
{
    // 添加服务
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);

    // 注意：必须先添加服务，然后才能设置其属性
    // web 服务器使用自定义的实例名
    mdns_service_instance_name_set("_http", "_tcp", "ESP32S3 server");

    mdns_txt_item_t serviceTxtData[1] = {
        {"board","esp32s3"},
    };
    // 设置服务的文本数据（会释放并替换当前数据）
    mdns_service_txt_set("_http", "_tcp", serviceTxtData, 1);

    // 修改服务端口号
    mdns_service_port_set("_myservice", "_udp", 4321);
}