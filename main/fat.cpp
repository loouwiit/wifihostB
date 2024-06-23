#include <fat.hpp>
#include <string.h>

static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

bool mountFlash()
{
	printf("fat: mounting flash\n");
	const esp_vfs_fat_mount_config_t mount_config = {
	   .format_if_mount_failed = false,
	   .max_files = FlashMaxFileNumber,
	   .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
	   .disk_status_check_enable = false
	};

	esp_err_t err;
	err = esp_vfs_fat_spiflash_mount_rw_wl(FlashPath, FlashPartitionLabel, &mount_config, &s_wl_handle);

	if (err == ESP_OK)
	{
		printf("fat: flash mount success\n");
		return true;
	}
	else
	{
		printf("fat: Failed to mount flash (%s)\n", esp_err_to_name(err));
		return false;
	}
}

void unmountFlash()
{
	printf("unmounting flash\n");
	ESP_ERROR_CHECK(esp_vfs_fat_spiflash_unmount_rw_wl(FlashPath, s_wl_handle));
	printf("flash unmounted\n");
}

void formatFlash()
{
	printf("fat: formating flash\n");
	esp_err_t ret = esp_vfs_fat_spiflash_format_rw_wl(FlashPath, FlashPartitionLabel);
	if (ret == ESP_OK)
	{
		printf("fat: format success\n");
	}
	else
	{
		printf("fat:format failed: %s\n", esp_err_to_name(ret));
	}
}

void getSpace(uint64_t& free, uint64_t& totol, const char* path)
{
	esp_vfs_fat_info(path, &totol, &free);
}

uint64_t getFreeSpace(const char* path)
{
	uint64_t bytes_total = 0;
	uint64_t bytes_free = 0;
	esp_vfs_fat_info(path, &bytes_total, &bytes_free);
	return bytes_free;
}

uint64_t getTotolSpace(const char* path)
{
	uint64_t bytes_total = 0;
	uint64_t bytes_free = 0;
	esp_vfs_fat_info(path, &bytes_total, &bytes_free);
	return bytes_total;
}

bool tree(const char* path, unsigned char maxOffset, unsigned char offset)
{
	if (offset >= maxOffset)
	{
		printf("tree offset %d, which is touched the max, in %s\n", offset, path);
		return false;
	}

	if (offset == 0)
	{
		printf("tree at %s:\n", path);
	}
	//printf("[debug]tree at %s:\n", path);

	Floor floor;
	if (!floor.open(path))
	{
		putchar('|');
		for (unsigned char i = 0; i < offset;i++)
		{
			putchar('|');
		}
		printf("-tree: fail in open %s\n", path);
		// printf("-tree: formating flash\n");
		// unmountFlash();
		// formatFlash();
		return false;
	}

	const char* sub = nullptr;
	size_t subLenght = 0;
	char* buffer = nullptr;
	bool fine = true;

	while (true)
	{
		sub = floor.read(Floor::Type::Floor);
		if (sub == nullptr) break;

		subLenght = strlen(sub);
		putchar('|');
		for (unsigned char i = 0; i < offset;i++)
		{
			putchar('|');
		}
		printf("-dir: ");
		printf(sub);
		printf("\n");

		buffer = new char[subLenght + floor.getPathLenght() + 2];memcpy(buffer, floor.getPath(), floor.getPathLenght());
		buffer[floor.getPathLenght()] = '/';
		memcpy(buffer + floor.getPathLenght() + 1, sub, subLenght + 1);
		fine = tree(buffer, maxOffset, offset + 1);
		delete[] buffer;

		if (fine) continue;
		else return false;
	}

	floor.backToBegin();
	while (true)
	{
		sub = floor.read(Floor::Type::File);
		if (sub == nullptr) break;

		putchar('|');
		for (unsigned char i = 0; i < offset;i++)
		{
			putchar('|');
		}
		printf("-file:");
		printf(sub);
		printf("\n");
	}

	return true;
}

bool newFile(const char* path)
{
	if (testFile(path))
		return false;

	FILE* file;
	file = fopen(path, "w");
	if (file != nullptr)
	{
		fclose(file);
		return true;
	}
	else
	{
		return false;
	}
}

bool testFile(const char* path)
{
	FILE* file;
	file = fopen(path, "r");
	if (file != nullptr)
	{
		fclose(file);
		return true;
	}
	else
	{
		return false;
	}
}

bool moveFile(const char* oldPath, const char* newPath)
{
	return rename(oldPath, newPath) == 0;
}

bool removeFile(const char* path)
{
	return remove(path) == 0;
}

bool newFloor(const char* path)
{
	size_t i = 0;
	while (path[i] == FlashPath[i] && path[i] != '\0' && FlashPath[i] != '\0')
		i++;
	//直接调用FatFs库函数，跳过VFS前缀
	if (FlashPath[i] == '\0')
		return f_mkdir(path + i) == 0;
	else
		return false;
}

bool testFloor(const char* path)
{
	Floor floor;
	if (floor.open(path))
	{
		floor.close();
		return true;
	}
	{
		return false;
	}
}

bool removeFloor(const char* path)
{
	return remove(path);
}

//FileBase

FileBase::~FileBase()
{
	if (file != nullptr)
	{
		fclose(file);
		file = nullptr;
	}
}

void FileBase::close()
{
	if (file != nullptr)
	{
		fclose(file);
		file = nullptr;
	}
}

bool FileBase::isOpen()
{
	return file != nullptr;
}

FileBase::OffsetType FileBase::getOffset()
{
	return ftell(file);
}

void FileBase::setOffset(FileBase::OffsetType offset, OffsetMode mode)
{
	fseek(file, offset, (int)mode);
}

void FileBase::offset(FileBase::OffsetType offset)
{
	fsetpos(file, &offset);
}

void FileBase::reGetSize()
{
	OffsetType origin = getOffset();
	setOffset(0, OffsetMode::End);
	fileSize = getOffset();
	setOffset(origin);
}

FileBase::OffsetType FileBase::getSize()
{
	return fileSize;
}

FileBase::operator FILE*& ()
{
	return file;
}

//IFile

bool IFile::open(const char* path)
{
	if (file != nullptr)
		close();
	fileSize = 0;
	file = fopen(path, "rb");
	if (file != nullptr)
		return true;
	else
		return false;
}

char IFile::get()
{
	return fgetc(file);
}

size_t IFile::read(void* pointer, size_t size)
{
	return fread((char*)pointer, sizeof(char), size, file);
}

//Ofile

bool OFile::open(const char* path)
{
	if (file != nullptr)
		close();
	fileSize = 0;
	file = fopen(path, "r+b");
	if (file != nullptr)
		return true;
	else
		return false;
}

bool OFile::put(const char ch)
{
	return fputc(ch, file) == 0;
}

size_t OFile::write(const void* pointer, size_t size)
{
	return fwrite(pointer, sizeof(char), size, file);
}

Floor::~Floor()
{
	if (dir != nullptr)
	{
		closedir(dir);
		dir = nullptr;
	}

	if (path != nullptr)
	{
		delete[] path;
		path = nullptr;
	}
}

bool Floor::open(const char* path)
{
	if (this->path != nullptr)
		delete[] this->path;
	pathBufferLenght = strlen(path) + 1;
	this->path = new char[pathBufferLenght];
	memcpy(this->path, path, pathBufferLenght);

	if (dir != nullptr)
		closedir(dir);
	dir = opendir(path);
	count[0] = 0;
	count[1] = 0;
	return dir != nullptr;
}

void Floor::close()
{
	if (dir != nullptr)
	{
		closedir(dir);
		dir = nullptr;
	}

	if (path != nullptr)
	{
		delete[] path;
		path = nullptr;
	}
}

const char* Floor::getPath()
{
	return path;
}

size_t Floor::getPathLenght()
{
	return pathBufferLenght - 1;
}

bool Floor::openFloor(const char* path, size_t pathLenght)
{
	char* buffer = new char[pathLenght + this->pathBufferLenght + 1];
	memcpy(buffer, this->path, pathBufferLenght - 1); // \0 不需要复制
	buffer[pathBufferLenght - 1] = '/';
	memcpy(buffer + pathBufferLenght, path, pathLenght + 1); // with \0

	DIR* temp = opendir(buffer);
	delete[] buffer;
	if (temp != nullptr)
	{
		closedir(dir);
		dir = temp;
		temp = nullptr;
		return true;
	}
	else
	{
		return false;
	}
}

bool Floor::openFile(const char* path, size_t pathLenght, FileBase& file)
{
	char* buffer = new char[pathLenght + this->pathBufferLenght + 1];
	memcpy(buffer, this->path, pathBufferLenght - 1); // \0 不需要复制
	buffer[pathBufferLenght - 1] = '/';
	memcpy(buffer + pathBufferLenght, path, pathLenght + 1); // with \0

	bool ret = file.open(buffer);
	delete[] buffer;
	return ret;
}

const char* Floor::read(Type type)
{
	if (dir == nullptr) return nullptr;

	dirent* content;
	do
	{
		content = readdir(dir);
	} while (content != nullptr && (content->d_type & (TypeBase)type) == 0);
	if (content != nullptr) return content->d_name;
	else return nullptr;
}

void Floor::backToBegin()
{
	if (dir != nullptr)
	{
		rewinddir(dir);
	}
}

void Floor::reCount()
{
	if (dir == nullptr) return;
	backToBegin();
	count[0] = 0;
	count[1] = 0;

	dirent* buffer = nullptr;
	while ((buffer = readdir(dir)) != nullptr)
	{
		switch (buffer->d_type)
		{
		case (TypeBase)Type::File:
			count[0]++;
			break;
		case (TypeBase)Type::Floor:
			count[1]++;
			break;
		default:
			printf("Fat: Floor::reCount: error in %s 's %s", path, buffer->d_name);
			break;
		}
	}

	backToBegin();
}

size_t Floor::getCount(Type type)
{
	switch (type)
	{
	case Type::File:
		return count[0];
	case Type::Floor:
		return count[1];
	case Type::Both:
		return count[0] + count[1];
	default:
		return 0;
	}
}