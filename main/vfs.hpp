#pragma once

constexpr char PerfixRoot[] = "/root";

constexpr char PerfixFlash[] = "/root";
constexpr char PerfixMem[] = "/root/mem";

using FileTypeBase = uint8_t;
enum class FileType : FileTypeBase
{
	File = DT_REG,
	Floor = DT_DIR,
	Both = File | Floor,
};
