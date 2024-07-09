#include <esp_vfs.h>
#include <mutex>
#include "minmax.hpp"
#include "stringCompare.hpp"
#include "nonCopyAble.hpp"
#include "mem.hpp"

#define MemDebug true
#define MemUsageDebug true
#define MemProcessDebug false

class MemBlockLinker; //链表
class MemFileDataBlock; // 数据块
class MemDirent; // VFS兼容
class MemFileHead; // 文件头
class MemFloorHead; // 目录头
class MemFloorDir; // VFS兼容

// linker:链表
class MemBlockLinker : public NonCopyAble
{
public:
	//内部文件，直接全部public
	MemBlockLinker* last = nullptr;
	MemBlockLinker* next = nullptr;
};

// dataBlock:数据块
class MemFileDataBlock : public MemBlockLinker
{
public:
	//内部文件，直接全部public
	char data[MemFileBlockTotolSize - sizeof(MemBlockLinker)] = { '\0' };
};

// dirent VFS兼容文件
class MemDirent
{
public:
	//内部文件，直接全部public
	struct dirent dirent;
	char(&name)[256] = dirent.d_name;
	size_t nameLenght = 0;
};

// fileHead:文件头
class MemFileHead : public MemBlockLinker, public MemDirent
{
public:
	//内部文件，直接全部public
	struct stat st;
	off_t& size = st.st_size;
	off_t operatorPointer = 0;
	MemFileDataBlock* block = nullptr;
	MemFloorHead* parent = nullptr;

	MemFileHead();
	~MemFileHead();
	ssize_t write(const char* buffer, size_t size);
	ssize_t read(char* buffer, size_t size);
};

// floorHead:目录头
class MemFloorHead : public MemBlockLinker, public MemDirent
{
public:
	//内部文件，直接全部public
	MemFloorHead* parent = nullptr;
	MemFloorHead* child = nullptr; //first child
	MemFileHead* file = nullptr;

	MemFloorHead();
	~MemFloorHead();
	MemFloorHead* findChildFloor(const char* floorName, size_t floorLenght);
	MemFloorHead* findFloor(const char* path, size_t pathLenght);
	MemFileHead* findChildFile(const char* fileName, size_t fileNameLenght);
	MemFileHead* findFile(const char* path, size_t pathLenght);

	bool removeChildFloor(const char* floorName, size_t floorNameLenght);
	bool removeFloor(const char* path, size_t pathLenght);
	bool removeChildFile(const char* fileName, size_t fileNameLenght);
	bool removeFile(const char* path, size_t pathLenght);

	bool makeChildFloor(const char* floorName, size_t floorNameLenght);
	bool makeFloor(const char* path, size_t pathLenght);
	bool makeChildFile(const char* fileName, size_t fileNameLenght);
	bool makeFile(const char* path, size_t pathLenght);
};

// floorDir VFS兼容
class MemFloorDir
{
public:
	DIR _RamDefender;
	MemFloorHead* floor = nullptr;
	MemFloorHead* operatorChild = nullptr;
	MemFileHead* operatorFile = nullptr;
	size_t operatorCount = 0;
};

ssize_t memWrite(int fd, const void* data, size_t size);
int memOpen(const char* path, int flags, int mode);
int memFstat(int fd, struct stat* st);
int memClose(int fd);
ssize_t memRead(int fd, void* dst, size_t size);
off_t memSeek(int fd, off_t size, int mod);
int memRename(const char* src, const char* dst);
int memUnlink(const char* path);
DIR* memOpenDir(const char* name);
dirent* memReadDir(DIR* pdir);
long memTellDir(DIR* pdir);
void memSeekDir(DIR* pdir, long offset);
int memCloseDir(DIR* pdir);
int memMakeDir(const char* name, mode_t mode);
int memRemoveDir(const char* name);

size_t memTotolUsage = 0;
MemFloorHead memRoot;
MemFileHead* memFileDescriptions[MaxMemFileDescriptionCount] = { nullptr };
std::mutex mutex;

// class函数

MemFileHead::MemFileHead()
{
	dirent.d_type = (unsigned char)FileType::File;
	dirent.d_name[0] = '\0';
	this->size = 0;
}

MemFileHead::~MemFileHead()
{
	this->parent = nullptr;
	MemFileDataBlock* data = this->block;
	this->block = nullptr;
	while (data->next != nullptr)
	{
		data = (MemFileDataBlock*)data->next;

#if MemDebug && MemUsageDebug
		memTotolUsage -= sizeof(MemFileDataBlock);
		printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif

		delete data->last;
	}

#if MemDebug && MemUsageDebug
	memTotolUsage -= sizeof(MemFileDataBlock);
	printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif

	delete data;
}

ssize_t MemFileHead::write(const char* buffer, size_t size)
{
	if (this->operatorPointer + size > MemFileMaxSize)
	{
		return MemFileSystemError::TooLongTheFile;
	}

	//确定起始块位置
	constexpr static size_t blockDataSize = sizeof(MemFileDataBlock::data);

	if (this->block == nullptr)
	{

#if MemDebug && MemUsageDebug
		memTotolUsage += sizeof(MemFileDataBlock);
		printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif

		this->block = new MemFileDataBlock;
	}
	MemFileDataBlock* block = this->block;
	size_t dataPointer = this->operatorPointer; //dataPointer可用%化简

	while (dataPointer > blockDataSize)
	{
		if (block->next == nullptr)
		{

#if MemDebug && MemUsageDebug
			memTotolUsage += sizeof(MemFileDataBlock);
			printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif

			block->next = new MemFileDataBlock;
			block->next->last = block;
		}
		block = (MemFileDataBlock*)block->next;
		dataPointer -= blockDataSize;
	}

	//写入起始块
	size_t writeSize = min(blockDataSize - dataPointer, size);
	memcpy(block->data + dataPointer, buffer, writeSize);

	//写入后续完整块
	size_t writePointer = writeSize;
	while (writePointer < size)
	{

#if MemDebug && MemUsageDebug
		memTotolUsage += sizeof(MemFileDataBlock);
		printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif

		block->next = new MemFileDataBlock;
		block->next->last = block;
		block = (MemFileDataBlock*)block->next;

		writeSize = min(blockDataSize, size - writePointer);
		memcpy(block->data, buffer + writePointer, writeSize);
		writePointer += writeSize;
	}

	this->operatorPointer += writePointer;
	if (this->operatorPointer > this->size)
		this->size = this->operatorPointer;
	return writePointer;
}
ssize_t MemFileHead::read(char* buffer, size_t size)
{
	//确定起始块位置
	constexpr static size_t blockDataSize = sizeof(MemFileDataBlock::data);

	MemFileDataBlock* block = this->block;
	size_t dataPointer = this->operatorPointer; //dataPointer可用%化简
	size_t fileLeaveDataSize = this->size;

	while (dataPointer > blockDataSize)
	{
		if (block->next == nullptr)
		{
			// out of file
			return 0;
		}
		block = (MemFileDataBlock*)block->next;
		dataPointer -= blockDataSize;
		fileLeaveDataSize -= blockDataSize;
	}

	// ... --- ................... --- ...
	// ... --- |      Block      | --- ...
	//                           ^ blockDataSize
	// ... --- DataData
	//           ^     ^ fileLeaveDataSize
	//           | operatorPointer
	//
	//           | buffer |

	//读取起始块
	size_t readSize = min(min(blockDataSize, fileLeaveDataSize) - dataPointer, size);
	memcpy(buffer, block->data + dataPointer, readSize);
	this->operatorPointer += readSize;

	//读取后续完整块
	size_t readPointer = readSize;
	block = (MemFileDataBlock*)block->next;
	while (readPointer < size && block != nullptr)
	{
		readSize = min(blockDataSize, size - readPointer);
		memcpy(buffer + readPointer, block->data, readSize);
		this->operatorPointer += readSize;
		readPointer += readSize;
	}

	return readPointer;
}

MemFloorHead::MemFloorHead()
{
	dirent.d_type = (unsigned char)FileType::Floor;
	dirent.d_name[0] = '\0';
}

MemFloorHead::~MemFloorHead()
{
	this->parent = nullptr;
	MemFileHead* file = this->file;
	if (file != nullptr)
	{
		while (file->next != nullptr)
		{
			file = (MemFileHead*)file->next;

#if MemDebug && MemUsageDebug
			memTotolUsage -= sizeof(MemFileHead);
			printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif

			delete file->last;
		}

#if MemDebug && MemUsageDebug
		memTotolUsage -= sizeof(MemFileHead);
		printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif

		delete file;
	}

	MemFloorHead* floor = this->child;
	if (floor != nullptr)
	{
		while (floor->next != nullptr)
		{
			floor = (MemFloorHead*)floor->next;

#if MemDebug && MemUsageDebug
			memTotolUsage -= sizeof(MemFloorHead);
			printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif

			delete floor->last;
		}
#if MemDebug && MemUsageDebug
		memTotolUsage -= sizeof(MemFloorHead);
		printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif

		delete floor;
	}
}

MemFloorHead* MemFloorHead::findChildFloor(const char* floorName, size_t floorNameLenght)
{
	MemFloorHead* floor = child;
	while (floor != nullptr)
	{
		if (stringCompare(floor->name, floor->nameLenght, floorName, floorNameLenght)) break;
		floor = (MemFloorHead*)floor->next;
	}
	return floor;
}

MemFloorHead* MemFloorHead::findFloor(const char* path, size_t pathLenght)
{
	if (path[pathLenght - 1] == '/')
	{
		pathLenght -= 1;
		if (pathLenght == 0) return this; // pathLenght == 1 -> '/'
	}
	if (path[0] == '/')
	{
		path++;
		pathLenght -= 1;
		if (pathLenght == 0) return nullptr; //路径无效
	}

	if (pathLenght == (size_t)-1) // '/'
	{
		printf("%s:%d I just want to figure out whether it matter, now it seem count\n", __FILE__, __LINE__);
		return this;
	}

	MemFloorHead* floor = this;
	size_t begin = 0;
	size_t end = 0;

	// root/path/floor
	// ^              ^
	// |              |
	//begin        Lenght
	while (begin < pathLenght)
	{
		// 找end
		while (path[end] != '/' && end < pathLenght) end++;
		// root/path/floor
		// ^   ^          ^
		// |   |          |
		//begin end    Lenght

		floor = floor->findChildFloor(path + begin, end - begin);
		if (floor == nullptr) return nullptr;

		if (end == pathLenght)
		{
			// root/path/floor
			//      ^    ^    ^
			//      |    |    |
			//   floor begin end
			return floor;
		}

		end++;
		begin = end;
		// root/path/floor
		//      ^         ^
		//      |         |
		//    begin    Lenght
	}

	printf("???How I get Here???\n\tpath=%s\n\tpathLenght=%u\n", path, pathLenght);
	//throw "???How I get Here???"
	return nullptr;
}

MemFileHead* MemFloorHead::findChildFile(const char* fileName, size_t fileNameLenght)
{
	MemFileHead* file = this->file;

	while (file != nullptr)
	{
		if (stringCompare(file->name, file->nameLenght, fileName, fileNameLenght)) break;
		file = (MemFileHead*)file->next;
	}
	return file;
}

MemFileHead* MemFloorHead::findFile(const char* path, size_t pathLenght)
{
	if (path[pathLenght - 1] == '/')
	{
		pathLenght -= 1;
		if (pathLenght == 0) return nullptr; //路径无效
	}
	if (path[0] == '/')
	{
		path++;
		pathLenght -= 1;
		if (pathLenght == 0) return nullptr; //路径无效
	}

	constexpr size_t negtive = (size_t)-1;
	size_t divide = pathLenght - 1;
	MemFloorHead* floor = &memRoot;

	// root/path/file
	//              ^
	while (divide != negtive)
	{
		if (path[divide] == '/')
		{
			// root/path/file
			//          ^
			floor = floor->findFloor(path, divide);
			if (floor == nullptr) return nullptr; //路径无效
			break;
		}
		divide--;
	}

	divide++;
	// root/path/file
	//           ^
	return floor->findChildFile(path + divide, pathLenght - divide);
}

bool MemFloorHead::removeChildFloor(const char* floorName, size_t floorNameLenght)
{
	MemFloorHead* floor = findChildFloor(floorName, floorNameLenght);
	if (floor == nullptr) return false;
	if (floor->child != nullptr || floor->file != nullptr) return false; //非空目录
	if (floor == this->child) this->child = (MemFloorHead*)floor->next;
	if (floor->last != nullptr) floor->last->next = floor->next;
	if (floor->next != nullptr) floor->next->last = floor->last;

#if MemDebug && MemUsageDebug
	memTotolUsage -= sizeof(MemFloorHead);
	printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif

	delete floor;
	return true;
}

bool MemFloorHead::removeFloor(const char* path, size_t pathLenght)
{
	if (path[pathLenght - 1] == '/')
	{
		pathLenght -= 1;
		if (pathLenght == 0) return false; //路径无效
	}
	if (path[0] == '/')
	{
		path++;
		pathLenght -= 1;
		if (pathLenght == 0) return false; //路径无效
	}

	constexpr size_t negtive = (size_t)-1;
	size_t divide = pathLenght - 1;
	MemFloorHead* floor = &memRoot;

	// root/path/floor
	//               ^
	while (divide != negtive)
	{
		if (path[divide] == '/')
		{
			// root/path/floor
			//          ^
			floor = floor->findFloor(path, divide);
			if (floor == nullptr) return false; //路径无效
			break;
		}
		divide--;
	}

	divide++;
	// root/path/floor
	//           ^
	return floor->removeChildFloor(path + divide, pathLenght - divide);
}

bool MemFloorHead::removeChildFile(const char* fileName, size_t fileNameLenght)
{
	MemFileHead* file = findChildFile(fileName, fileNameLenght);
	if (file == nullptr) return false;
	if (file == this->file) this->file = (MemFileHead*)file->next;
	if (file->last != nullptr) file->last->next = file->next;
	if (file->next != nullptr) file->next->last = file->last;

#if MemDebug && MemUsageDebug
	memTotolUsage -= sizeof(MemFileHead);
	printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif

	delete file;
	return true;
}

bool MemFloorHead::removeFile(const char* path, size_t pathLenght)
{
	if (path[pathLenght - 1] == '/')
	{
		pathLenght -= 1;
		if (pathLenght == 0) return false; //路径无效
	}
	if (path[0] == '/')
	{
		path++;
		pathLenght -= 1;
		if (pathLenght == 0) return false; //路径无效
	}

	constexpr size_t negtive = (size_t)-1;
	size_t divide = pathLenght - 1;
	MemFloorHead* floor = &memRoot;

	// root/path/floor
	//               ^
	while (divide != negtive)
	{
		if (path[divide] == '/')
		{
			// root/path/floor
			//          ^
			floor = floor->findFloor(path, divide);
			if (floor == nullptr) return false; //路径无效
			break;
		}
		divide--;
	}

	divide++;
	// root/path/floor
	//           ^
	return floor->removeChildFile(path + divide, pathLenght - divide);
}
bool MemFloorHead::makeChildFloor(const char* floorName, size_t floorNameLenght)
{
	if (this->child == nullptr)
	{

#if MemDebug && MemUsageDebug
		memTotolUsage += sizeof(MemFloorHead);
		printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif

		this->child = new MemFloorHead;
		this->child->parent = this;
		this->child->nameLenght = floorNameLenght;
		memcpy(this->child->name, floorName, floorNameLenght);
		this->child->name[floorNameLenght] = '\0';
		return true;
	}

	// 查重
	MemFloorHead* floor = this->child;
	while (floor->next != nullptr)
	{
		if (stringCompare(floor->name, floor->nameLenght, floorName, floorNameLenght)) return false; // 已经存在
		floor = (MemFloorHead*)floor->next;
	}
	if (stringCompare(floor->name, floor->nameLenght, floorName, floorNameLenght)) return false; // 已经存在

	// floor -> new floor
	// ^
#if MemDebug && MemUsageDebug
	memTotolUsage += sizeof(MemFloorHead);
	printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif
	floor->next = new MemFloorHead;
	floor->next->last = floor;
	floor = (MemFloorHead*)floor->next;

	// floor -> new floor
	//          ^
	floor->parent = this;
	floor->nameLenght = floorNameLenght;
	memcpy(floor->name, floorName, floorNameLenght);
	floor->name[floorNameLenght] = '\0';
	return true;
}

bool MemFloorHead::makeFloor(const char* path, size_t pathLenght)
{
	if (path[pathLenght - 1] == '/')
	{
		pathLenght -= 1;
		if (pathLenght == 0) return false; //路径无效
	}
	if (path[0] == '/')
	{
		path++;
		pathLenght -= 1;
		if (pathLenght == 0) return false; //路径无效
	}

	constexpr size_t negtive = (size_t)-1;
	size_t divide = pathLenght - 1;
	MemFloorHead* floor = &memRoot;

	// root/path/floor
	//               ^
	while (divide != negtive)
	{
		if (path[divide] == '/')
		{
			// root/path/floor
			//          ^
			floor = floor->findFloor(path, divide);
			if (floor == nullptr) return false; //路径无效
			break;
		}
		divide--;
	}

	divide++;
	// root/path/floor
	//           ^
	return floor->makeChildFloor(path + divide, pathLenght - divide);
}

bool MemFloorHead::makeChildFile(const char* fileName, size_t fileNameLenght)
{
	if (this->file == nullptr)
	{

#if MemDebug && MemUsageDebug
		memTotolUsage += sizeof(MemFileHead);
		printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif

		this->file = new MemFileHead;
		MemFileHead& file = *this->file;
		file.parent = this;
		file.nameLenght = fileNameLenght;
		memcpy(file.name, fileName, fileNameLenght);
		file.name[fileNameLenght] = '\0';
		return true;
	}

	// 查重
	MemFileHead* file = this->file;
	while (file->next != nullptr)
	{
		if (stringCompare(file->name, file->nameLenght, fileName, fileNameLenght)) return false; // 已经存在
		file = (MemFileHead*)file->next;
	}
	if (stringCompare(file->name, file->nameLenght, fileName, fileNameLenght)) return false; // 已经存在

	// file -> new file
	// ^
#if MemDebug && MemUsageDebug
		memTotolUsage += sizeof(MemFileHead);
		printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif
	file->next = new MemFileHead;
	file->next->last = file;
	file = (MemFileHead*)file->next;

	// file -> new file
	//         ^

	file->parent = this;
	file->nameLenght = fileNameLenght;
	memcpy(file->name, fileName, fileNameLenght);
	file->name[fileNameLenght] = '\0';
	return true;
}

bool MemFloorHead::makeFile(const char* path, size_t pathLenght)
{
	if (path[pathLenght - 1] == '/')
	{
		pathLenght -= 1;
		if (pathLenght == 0) return false; //路径无效
	}
	if (path[0] == '/')
	{
		path++;
		pathLenght -= 1;
		if (pathLenght == 0) return false; //路径无效
	}

	constexpr size_t negtive = (size_t)-1;
	size_t divide = pathLenght - 1;
	MemFloorHead* floor = &memRoot;

	// root/path/floor
	//               ^
	while (divide != negtive)
	{
		if (path[divide] == '/')
		{
			// root/path/floor
			//          ^
			floor = floor->findFloor(path, divide);
			if (floor == nullptr) return false; //路径无效
			break;
		}
		divide--;
	}

	divide++;
	// root/path/floor
	//           ^
	return floor->makeChildFile(path + divide, pathLenght - divide);
}

// vfs file函数

void mountMem()
{
#if MemDebug && MemProcessDebug
	printf("mount Mem\n");
#endif
	esp_vfs_t memFs;

	memFs.flags = ESP_VFS_FLAG_DEFAULT;
	memFs.open = memOpen;
	memFs.write = memWrite;
	memFs.read = memRead;
	memFs.fstat = memFstat;
	memFs.close = memClose;
	memFs.lseek = memSeek;
	memFs.rename = memRename;
	memFs.unlink = memUnlink;
	memFs.mkdir = memMakeDir;
	memFs.rmdir = memRemoveDir;
	memFs.opendir = memOpenDir;
	memFs.readdir = memReadDir;
	memFs.telldir = memTellDir;
	memFs.seekdir = memSeekDir;
	memFs.closedir = memCloseDir;

	ESP_ERROR_CHECK(esp_vfs_register(PerfixMem, &memFs, NULL));
}

// void testMem()
// {
// 	printf("test Mem\n");
// 	vTaskDelay(100);
// 	char buffer[10] = "";
// 	auto file = fopen("/mem/test", "wb+");
// 	if (file != 0)
// 	{
// 		fwrite("pointer", sizeof(char), 7, file);
// 		fread(buffer, sizeof(char), 7, file);
// 		fclose(file);
// 	}
// 	else
// 	{
// 		printf("file == 0!\n");
// 	}
// }

int memOpen(const char* path, int flags, int mode)
{
#if MemDebug && MemProcessDebug
	printf("memOpen\n");
#endif

	size_t pathLenght = strlen(path);
	MemFileHead* file = memRoot.findFile(path, pathLenght);
	if (file == nullptr)
	{
		if (!memRoot.makeFile(path, pathLenght))
			return MemFileSystemError::Error;
		file = memRoot.findFile(path, pathLenght);
	}

	//operatorPointer归位
	file->operatorPointer = 0;

	//找fd
	int fd = 0;
	std::lock_guard<std::mutex>guard{ mutex };
	while (memFileDescriptions[fd] != nullptr && fd < MaxMemFileDescriptionCount) fd++;
	//没有文件描述符
	if (fd == MaxMemFileDescriptionCount) return MemFileSystemError::NoAviliableDescription;

#if MemDebug && MemProcessDebug
	printf("memOpend with fd = %d\n", fd);
#endif
	memFileDescriptions[fd] = file;
	return fd;
}

int memFstat(int fd, struct stat* st)
{
#if MemDebug && MemProcessDebug
	printf("memFstat\n");
#endif
	if (memFileDescriptions[fd] == nullptr) return MemFileSystemError::NotOpenedFileDescription;
	*st = memFileDescriptions[fd]->st;
	return 0;
}

ssize_t memWrite(int fd, const void* data, size_t size)
{
#if MemDebug && MemProcessDebug
	printf("memWrite\n");
#endif
	if (memFileDescriptions[fd] == nullptr) return MemFileSystemError::NotOpenedFileDescription;
	return (memFileDescriptions[fd]->write((const char*)data, size));
}

ssize_t memRead(int fd, void* dst, size_t size)
{
#if MemDebug && MemProcessDebug
	printf("memRead\n");
#endif
	if (memFileDescriptions[fd] == nullptr) return MemFileSystemError::NotOpenedFileDescription;
	return (memFileDescriptions[fd]->read((char*)dst, size));
}

int memClose(int fd)
{
#if MemDebug && MemProcessDebug
	printf("memClose\n");
#endif
	if (memFileDescriptions[fd] == nullptr) return MemFileSystemError::NotOpenedFileDescription;
	memFileDescriptions[fd] = nullptr;
	return 0;
}

off_t memSeek(int fd, off_t size, int mod)
{
#if MemDebug && MemProcessDebug
	printf("memSeek\n");
#endif
	if (memFileDescriptions[fd] == nullptr) return MemFileSystemError::NotOpenedFileDescription;

	off_t& offset = memFileDescriptions[fd]->operatorPointer;
	switch (mod)
	{
	case SEEK_SET:
		// beg
		offset = size;
		break;
	default:
	case SEEK_CUR:
		// cur
		offset += size;
		break;
	case SEEK_END:
		// end
		offset = size + memFileDescriptions[fd]->size;
		break;
	}
	if (offset < 0) offset = 0;
	if (offset > memFileDescriptions[fd]->size) offset = memFileDescriptions[fd]->size;

	return memFileDescriptions[fd]->operatorPointer;
}

int memRename(const char* src, const char* dst)
{
#if MemDebug && MemProcessDebug
	printf("memRename\n");
#endif
	return MemFileSystemError::UnSupportOpreation;
}

int memUnlink(const char* path)
{
#if MemDebug && MemProcessDebug
	printf("memUnlink\n");
#endif
	size_t pathLenght = strlen(path);
	if (path[pathLenght - 1] == '/')
		return memRoot.removeFloor(path, pathLenght) ? 0 : MemFileSystemError::Error;
	else
		return memRoot.removeFile(path, pathLenght) ? 0 : MemFileSystemError::Error;
}

// vfs floor函数

DIR* memOpenDir(const char* name)
{
#if MemDebug && MemProcessDebug
	printf("memOpenDir\n");
#endif

#if MemDebug && MemUsageDebug
	memTotolUsage += sizeof(MemFloorDir);
	printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif

	auto ret = new MemFloorDir;
	ret->floor = memRoot.findFloor(name, strlen(name));
	if (ret->floor == nullptr)
	{

#if MemDebug && MemUsageDebug
		memTotolUsage -= sizeof(MemFloorDir);
		printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif

		delete ret;
		return nullptr;
	}

	ret->operatorChild = ret->floor->child;
	ret->operatorFile = ret->floor->file;
	return (DIR*)ret;
}

dirent* memReadDir(DIR* pdir)
{
#if MemDebug && MemProcessDebug
	printf("memReadDir\n");
#endif
	MemFloorDir& dir = *(MemFloorDir*)pdir;
	MemFloorHead*& floor = dir.floor;
	MemFloorHead*& child = dir.operatorChild;
	MemFileHead*& file = dir.operatorFile;

	if (child != nullptr && child->parent == floor) // 防止child被delete
	{
		dirent* ret = &child->dirent;
		child = (MemFloorHead*)child->next;
		dir.operatorCount++;
		return ret;
	}

	if (file != nullptr && file->parent == floor) // 防止file被delete
	{
		dirent* ret = &file->dirent;
		file = (MemFileHead*)file->next;
		dir.operatorCount++;
		return ret;
	}
	return nullptr;
}

long memTellDir(DIR* pdir)
{
#if MemDebug && MemProcessDebug
	printf("memTellDir\n");
#endif
	MemFloorDir& dir = *(MemFloorDir*)pdir;
	return dir.operatorCount;
}

void memSeekDir(DIR* pdir, long offset)
{
#if MemDebug && MemProcessDebug
	printf("memSeekDir\n");
#endif
	MemFloorDir& dir = *(MemFloorDir*)pdir;
	MemFloorHead*& child = dir.operatorChild;
	MemFileHead*& file = dir.operatorFile;
	child = dir.floor->child;
	file = dir.floor->file;
	for (size_t i = 0;i < offset;i++)
		memReadDir(pdir);
}

int memCloseDir(DIR* pdir)
{
#if MemDebug && MemProcessDebug
	printf("memCloseDir\n");
#endif

#if MemDebug && MemUsageDebug
	memTotolUsage -= sizeof(MemFloorDir);
	printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif

	delete (MemFloorDir*)pdir;
	return 0;
}

int memMakeDir(const char* name, mode_t mode)
{
#if MemDebug && MemProcessDebug
	printf("memMakeDir\n");
#endif
	return memRoot.makeFloor(name, strlen(name));
}

int memRemoveDir(const char* name)
{
#if MemDebug && MemProcessDebug
	printf("memRemoveDir\n");
#endif
	if (memRoot.removeFloor(name, strlen(name))) return 0;
	else return MemFileSystemError::Error;
}
