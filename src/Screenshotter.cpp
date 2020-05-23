#include "Screenshotter.hpp"
#include "util/Window.hpp"
#include "util/lodepng.h"
#include "Camera.hpp"
#include "atmosphere/Atmosphere.hpp"
#include <iostream>
#include <iomanip>
#include <ctime>
#include <sstream>
#include <filesystem>

namespace Mulen {
    static const auto keyPrefix = std::string("mulen_");

    static std::string DetermineFileName()
    {
        const auto dir = u8"screenshots", id = u8"_Mulen_", extension = u8".png";
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

    void Screenshotter::TakeScreenshot(Window& window, const glm::ivec2& resolution, const Camera& camera, const Atmosphere::Atmosphere& atmosphere)
    {
        const auto filename = DetermineFileName(); // (maybe do this in the other thread instead?)

        // Encode camera parameters as strings in the PNG.
        const auto p = camera.GetPosition();
        const auto o = camera.GetOrientation();
        auto positionStr = std::to_string(p.x) + ' ' + std::to_string(p.y) + ' ' + std::to_string(p.z);
        auto orientationStr = std::to_string(o.x) + ' ' + std::to_string(o.y) + ' ' + std::to_string(o.z) + ' ' + std::to_string(o.w);
        // - to do: add more important values (max allowed depth among them)

        Util::Screenshotter::KeyValuePairs keyValuePairs;
        keyValuePairs[keyPrefix + "camera_position"] = positionStr;
        keyValuePairs[keyPrefix + "camera_orientation"] = orientationStr;
        keyValuePairs[keyPrefix + "camera_upright"] = std::to_string(camera.upright);
        keyValuePairs[keyPrefix + "atmosphere_animation_time"] = std::to_string(atmosphere.GetAnimationTime());
        keyValuePairs[keyPrefix + "atmosphere_light_time"] = std::to_string(atmosphere.GetLightTime());
        screenshotter.TakeScreenshot(filename, resolution, std::move(keyValuePairs));
    }

    void Screenshotter::ReceiveScreenshot(std::string filename, Camera& camera, Atmosphere::Atmosphere& atmosphere)
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
            bool cameraWasUpdated = false;
            for (auto i = 0u; i < state.info_png.text_num; ++i)
            {
                std::string key = state.info_png.text_keys[i];
                std::string str = state.info_png.text_strings[i];
                if (key.find(keyPrefix) == 0)
                {
                    const auto k = key.substr(keyPrefix.length());
                    std::stringstream ss(str);
                    if (k == "camera_position")
                    {
                        Object::Position p;
                        ss >> p.x >> p.y >> p.z;
                        camera.SetPosition(p);
                        cameraWasUpdated = true;
                    }
                    else if (k == "camera_orientation")
                    {
                        Object::Orientation o;
                        ss >> o.x >> o.y >> o.z >> o.w;
                        camera.SetOrientation(o);
                        cameraWasUpdated = true;
                    }
                    else if (k == "camera_upright")
                    {
                        ss >> camera.upright;
                        cameraWasUpdated = true;
                    }
                    else if (k == "atmosphere_animation_time")
                    {
                        double t;
                        ss >> t;
                        atmosphere.SetAnimationTime(t);
                    }
                    else if (k == "atmosphere_light_time")
                    {
                        double t;
                        ss >> t;
                        atmosphere.SetLightTime(t);
                    }
                    else
                    {
                        std::cout << "Unknown key-value pair in image: \"" << key << "\" = \"" << str << "\"" << std::endl;
                    }
                }
            }

            camera.FlagForUpdate();
            // - to do: maybe check if any values were missing
        }
    }
}
