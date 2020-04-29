#include "Screenshotter.hpp"
#include "util/Window.hpp"
#include "util/lodepng.h"
#include <iostream>
#include <iomanip>
#include <ctime>
#include <sstream>
#include <filesystem>

namespace Mulen {
    static std::string DetermineFileName()
    {
        const auto dir = "screenshots", id = "_Mulen_", extension = ".png";
        std::filesystem::create_directories(dir);

        // Find maximum image index in use.
        int newIndex = 0u;
        for (auto& p : std::filesystem::directory_iterator(dir))
        {
            const auto filename = p.path().string();
            const auto idx = filename.find(id);
            if (idx != std::string::npos)
            {
                newIndex = std::max(newIndex, std::stoi(filename.substr(idx + strlen(id))));
            }
        }
        ++newIndex; // one higher than the highest already in use

        auto t = std::time(nullptr);
        auto tm = *std::localtime(&t);
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y%m%d");
        auto dateStr = oss.str();
        // - maybe to do: configurable format/order
        return std::string(dir) + '/' + dateStr + id + std::to_string(newIndex) + extension;
    }

    void Screenshotter::TakeScreenshot(Window& window, Camera& camera)
    {
        const auto filename = DetermineFileName(); // (maybe do this in the other thread instead?)

        // - to do: encode camera parameters

        Util::Screenshotter::KeyValuePairs keyValuePairs;
        keyValuePairs["mulen_test2"] = "Here we go again. You ready?";
        screenshotter.TakeScreenshot(filename, window.GetSize(), std::move(keyValuePairs));
    }

    void Screenshotter::ReceiveScreenshot(std::string filename, Camera& camera)
    {
        // Try to decode as PNG (maybe in another thread, eventually?) and retrieve Mulen-specific data if there:
        std::vector<unsigned char> png;
        std::vector<unsigned char> image;
        unsigned width, height;
        lodepng::State state;

        unsigned error = lodepng::load_file(png, filename);
        if (!error) error = lodepng::decode(image, width, height, state, png);
        if (error)
        {
            if (error == 28) return; // not a PNG - move on
            std::cout << "PNG decoder error " << error << ": " << lodepng_error_text(error) << std::endl;
            return;
        }

        if (state.info_png.text_num)
        {
            for (auto i = 0u; i < state.info_png.text_num; ++i)
            {
                std::string key = state.info_png.text_keys[i];
                std::string str = state.info_png.text_strings[i];
                const std::string prefix = "mulen_";
                if (key.find(prefix) == 0)
                {
                    std::cout << "\"" << key << "\": \"" << str << "\"" << std::endl;
                    // - to do: decode Mulen data
                }
            }

            // - to do: set camera according to screenshot values, if present
        }
    }
}
