#pragma once
#include <glm/glm.hpp>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>

namespace Util {
    class Screenshotter 
    {
    public:
        typedef std::unordered_map<std::string, std::string> KeyValuePairs;

        Screenshotter();
        ~Screenshotter();

        // - to do: allow screenshots of textures (off-screen screenshot)
        void TakeScreenshot(const std::string& filename, glm::uvec2 size, KeyValuePairs && = {});

        void Thread();

    private:

        std::mutex m;
        std::condition_variable cv;
        std::thread thread;
        bool done = false;
        struct Job
        {
            std::vector<unsigned char> image;
            glm::uvec2 size;
            unsigned channels;
            std::string filename;
            KeyValuePairs keyValuePairs;
        };
        std::queue<std::unique_ptr<Job>> jobs;

        void Save(Job& job);
    };
}
