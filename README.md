# ESP32S3 wifihostB

一个使用ESP32S3作为wifi小服务器的项目。

## 开发设备及环境
* ESP32S3-WROOM-1 N16R8  
  16MB flash 8MB RAM
* VScode 版本：1.91.0
* ESP-IDF vscode 扩展 版本：v1.8.0

不知道是不是必要项:
* ESP32S3 N16R8
* Ubuntu 22.04.4 LTS
* ESP-IDF 版本：v5.3-beta2

版本更新可能忘记修改。

## 项目配置

带“\*”标识的配置为必要配置

### wifi\*
请创建[./main/wifi.inl](main/wifi.inl)文件，内容如下：

```
constexpr char WIFISSID[32] = "your wifi ssid";
constexpr char WIFIPASSWORD[64] = "your wifi password";
```

请勿上传你的wifi名称及密码，避免泄露信息！

### 网页上传\*

编译烧录完成后请在浏览器打开`/file`管理页面。建立`serverPath`（默认值：`server`）目录。然后将[./main/server/web](main/server/web)下的文件和目录依次上传。

### LED

#### 修改LED引脚

修改[./main/main.cpp](main/main.cpp)里`app_main`中的`startPWM((gpio_num_t)1);`为LED正极GPIO的引脚编号。

碎碎念：记得加限流电阻，3.3v直连LED有可能将其烧毁！  

### 按钮——IO0
[./main/main.cpp](main/main.cpp)中有`void setIo0();`，可自定义。在本项目中，IO0用于启动并连接wifi、启动服务端。

### 分区表
文件位置: [./partitions.csv](partitions.csv)  
fat分区与[./main/fat.hpp](main/fat.hpp)中的`FlashPartitionLabel`相同，确保成功挂载。

### 存储
flash、mem挂载点均由[./main/vfs/hpp](main/vfs.hpp)中的对应变量指定。
| 设备 | 配置项 | 默认挂载点 |
| :-: | :-: | :-: |
| 内置fat分区<br/>(大小设定参照分区表一节) | PerfixFlash | /root |
| 内存 | PerfixMem | /root/mem |

服务器访问根目录由[./main/vfs/hpp](main/vfs.hpp)中的`PerfixRoot`决定。  
服务器默认访问目录由[./main/server/server.cpp](main/server/server.cpp)中的`ServerPath`决定。

#### 服务器访问根目录与服务器默认访问目录的区别
URI中直接就是路径的普通用户访问被限制在`serverPath`目录下  
URI中含file的管理用户访问提高到`PerfixRoot`目录下

管理用户有权访问普通目录，应此`serverPath`在`PerfixRoot`下

例如：  
http://esp32s3.local/testFloor/testFile 将访问PerfixRoot/ServerPath/testFloor/testFile  
http://esp32s3.local/file/testFloor/testFile 将访问PerfixRoot/testFloor/testFile

#### 管理用户的意义
为了权限分离，也为了方便普通用户，加入不同的用户级别。但这目前仅限于脑中假想，目前没有任何用户级的权限限制。给未来打地基了属于是。

#### 为什么有PerfixRoot这种“根”目录设定而不是直接用“/”？
[ESP-IDF限制](https://docs.espressif.com/projects/esp-idf/zh_CN/v5.2.2/esp32s3/api-reference/storage/vfs.html#id5)。ESP-IDF要求挂载点以“/”开头，至少两个字符。意味着必须有一个目录（`PerfixRoot`）作为虚拟“根”目录。  
对于文档中所述例外情况，限于水平问题我没弄成功。所以退而求其次加入虚拟的根目录。

### FAT分区格式化密码

[./main/server/server.cpp](main/server/server.cpp)中的`formatingPassword`

### mDNS

设置地址：[./main/server/server.cpp](main/server/server.cpp)中的`MDnsName`  
设置服务：[./main/server/mDns.cpp](main/server/mDns.cpp)里`mDnsStart`、`mDnsAdd`中的对于函数参数  
该部分代码大部分来源于IDF文档，建议结合[文档](https://docs.espressif.com/projects/esp-protocols/mdns/docs/latest/zh_CN/index.html)修改。  
[mDNS项目](https://github.com/espressif/esp-protocols/tree/master/components/mdns)

### Http并发与缓存

#### 网络架构
接入TCP并放入window中，等待`coWorker`的处理  
`coWorker`同一时间只能处理一个请求。大型请求（GET和PUT）内部会有delay函数避免挤占资源导致其他请求无法响应。

#### 并发设置
最大连接数：[./main/server/server.cpp](main/server/server.cpp)中的`socketStreamWindowNumber`。  
`coWorker`数：[./main/server/server.cpp](main/server/server.cpp)中的`coworkerNumber`。  
请注意ESP32S3为双核SoC，增加的并发数不一定能提高性能。但能缓解大型请求下的资源挤占问题。

#### 缓存设置

socketStream缓存：[./main/server/head/socketStream.hpp](main/server/head/socketStream.hpp)中的`SocketStreamBufferSize`。决定`socketStream`的缓存数量。`socketStream`是本项目中所有网络相关的基础，提供最基本的`read`、`write`函数。提高缓存有利于增加性能。（虽然我估计影响不大）

URI解析缓存：[./main/server/head/http.hpp](main/server/head/http.hpp)中的`HttpUrlBufferLenght`。决定URI的最大长度。  
Reason缓存：[./main/server/head/http.hpp](main/server/head/http.hpp)中的`HttpReasonBufferLenght`。决定解析响应时的最大长度  
Head缓存：[./main/server/head/http.hpp](main/server/head/http.hpp)中的`HttpHeadsBufferLenght`。决定解析Http Head时每行（没个Http Head）的最大长度。
发送文件缓存：[./main/server/head/http.hpp](main/server/head/http.hpp)中的`HttpBodySendingBufferLenght`。决定发送Http请求时文件的读取缓存。更大的数字可使`coWorker`一次发送更多内容，减少发送次数与delay时间，有利于提高整体性能。

PUT缓存：[./main/server/server.cpp](main/server/server.cpp)中的`PutBufferSize`。决定PUT时服务器的读取缓存。更大的数字可使`coWorker`一次读取更多内容，减少接收次数与delay时间，有利于提高整体性能。

请注意：缓存由于速度、管理方便大部分都在线程的栈上。过大的缓存可能导致栈溢出错误和小型处理时空间的浪费。

### 文件系统相关

#### 文件大小控制

PUT的最大限制：[./main/server/server.cpp](main/server/server.cpp)中的`PutMaxSize`。
Mem文件系统中的最大限制：[./main/mem.hpp](main/mem.hpp)中的`MemFileMaxSize`。

### 其它
想不起来了，到时候再说

## 服务器功能

由[./main/server/server.cpp](main/server/server.cpp)中的对应函数处理。除GET外基本目录都对应`PerfixRoot`
* GET请求  
  由`httpGet`处理。解析URI，若URI为`/file`、`/File`、`/FILE`则返回管理页面；若URI以`/file/`、`/File/`、`/FILE/`开头则返回`PerfixRoot`下的对应文件；其余情况返回`PerfixRoot/ServerPath`下的对应文件。
* PUT请求  
  由`httpPut`处理。解析URI，若URI以'/'结尾则建立目录；否则打开并写入文件。
* DELETE请求  
  由`httpDelete`处理。解析URI，尝试删除对应文件或目录。
* POST请求
* * `/api/setLightLevel`  
    设定PWM占比从而设置LED亮度。  
    请求的body为PWM占比。
* * `/api/getLightLevel`  
    获取PWM占比。  
    响应的body为PWM占比。
* * `/api/getTemperature`  
    获取内部温度。  
    响应的body为内部温度。
* * `/api/floor`  
    获取目录信息。  
    请求的body为目录，如“/server”。  
    响应的body为目录信息，每行一个内容，以“/”结尾的为目录，其余为文件。
* * `/api/getSpaece`  
    获取Fat文件系统的空间信息。  
    响应的body为空间信息，两个数字由“,”分割，前一个为可用空间，后一个为总空间。
* * `/api/serverOff`  
    关闭wifi、服务器功能  
    再次开启请使用IO0按钮（见上文）
* * `/api/formatFlash`
    请求的body为格式化密码（设置见上文）  
    响应的body为格式化情况

## 杂谈

### LICENSE

[`ESP-IDF`](https://github.com/espressif/esp-idf)使用`Apache License 2.0`协议。未对其进行任何修改。项目连接：[https://github.com/espressif/esp-idf](https://github.com/espressif/esp-idf)  
本项目`wifihostB`使用`GPL-3.0 license`协议

新人一个，协议方面不熟。讲究一个无脑GPL3做最开源的一次。  

### wifihostB那么A在那里？

A是废弃项目，还没来得及开源。它和esp-led都是B的前身。只不过A的代码太烂了，LED的代码太过于偏向测试了，所以重新开了B的项目。将来的某个时间B过于零乱的时候或许C就会出现。

### start VS code.sh

纯纯懒的，对项目没有影响。

## 未来开发

* 一个管理密码的页面
* 一个小型数据库的页面
* 权限分离与账户登录
* 聊天页面
* 小游戏服务端（可能是单独的项目，而且客户端还遥遥无期）
* ...
