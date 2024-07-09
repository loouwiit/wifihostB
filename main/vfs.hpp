#pragma once

constexpr char PerfixRoot[] = "/root";

constexpr char PerfixFlash[] = "/root";

using FileTypeBase = uint8_t;
enum class FileType : FileTypeBase
{
	File = DT_REG,
	Floor = DT_DIR,
	Both = File | Floor,
};
