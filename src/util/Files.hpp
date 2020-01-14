#pragma once
#include <fstream>

namespace Util {
	inline std::string GetFileContents(std::string filePath)
    {
        std::ifstream in(filePath, std::ios::binary);
        if (in.fail()) return "";
        return std::string(
            (std::istreambuf_iterator<char>(in)),
            (std::istreambuf_iterator<char>())
        );
    }
}
