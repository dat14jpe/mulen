#include "Screenshotter.hpp"
#include "GLObject.hpp"
#include "lodepng.h"
#include <iostream>

namespace Util {
    Screenshotter::Screenshotter()
        : thread{ &Screenshotter::Thread, this }
    {

    }

    Screenshotter::~Screenshotter()
    {
        {
            std::lock_guard<std::mutex> lk(m);
            done = true;
        }
        cv.notify_one();
        thread.join();
    }
    
    void Screenshotter::Thread()
    {
        while (true)
        {
            std::unique_ptr<Job> job;
            {
                std::unique_lock<std::mutex> lk(m);
                cv.wait(lk, [&] { return done || jobs.size(); });
                if (done) break;
                job = std::move(jobs.front());
                jobs.pop();
            }

            Save(*job);
        }
    }

    void Screenshotter::TakeScreenshot(const std::string& filename, glm::uvec2 size, KeyValuePairs&& keyValuePairs)
    {
        // - to do: reuse job objects (there's little need for dynamic allocation all the time)
        std::unique_ptr<Job> job(new Job{});
        job->keyValuePairs = keyValuePairs;
        job->size = size;
        auto& image = job->image;
        auto& channels = job->channels = 3u;
        image.resize(size.x * size.y * channels);

        // Retrieve from OpenGL buffer.
        glBindFramebuffer(GL_FRAMEBUFFER, 0u);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, size.x, size.y, channels == 4u ? GL_RGBA : GL_RGB, GL_UNSIGNED_BYTE, image.data());

        {
            std::lock_guard<std::mutex> lk(m);
            jobs.push(std::move(job));
            auto& job = jobs.back();
            job->filename = filename;
        }
        cv.notify_one();
    }

    void Screenshotter::Save(Job& job)
    {
        auto& image = job.image;
        auto& size = job.size;
        auto& channels = job.channels;

        // Reverse vertically.
        for (auto y = 0u; y < size.y / 2u; ++y)
        {
            for (auto x = 0u; x < size.x; ++x)
            {
                for (auto c = 0u; c < channels; ++c)
                {
                    auto i0 = (y * size.x + x) * channels + c;
                    auto i1 = ((size.y - 1u - y) * size.x + x) * channels + c;
                    auto temp = image[i0];
                    image[i0] = image[i1];
                    image[i1] = temp;
                }
            }
        }

        // Encode and save PNG.
        // - to do: possibly do this in another thread instead
        std::vector<unsigned char> png;
        lodepng::State state;

        if (false) // - to do: make this configurable
        {
            // Aim for best compression.
            state.encoder.filter_palette_zero = 0; //We try several filter types, including zero, allow trying them all on palette images too.
            state.encoder.add_id = false; //Don't add LodePNG version chunk to save more bytes
            state.encoder.text_compression = 1; //Not needed because we don't add text chunks, but this demonstrates another optimization setting
            state.encoder.zlibsettings.nicematch = 258; //Set this to the max possible, otherwise it can hurt compression
            state.encoder.zlibsettings.lazymatching = 1; //Definitely use lazy matching for better compression
            state.encoder.zlibsettings.windowsize = 32768; //Use maximum possible window size for best compression
        }

        // Set up colour types.
        state.info_png.color.colortype = LCT_RGB;
        state.info_png.color.bitdepth = 8;
        state.info_raw.colortype = LCT_RGB;
        state.info_raw.bitdepth = 8;
        state.encoder.auto_convert = 0;

        // Add custom data (key/value strings).
        for (auto entry : job.keyValuePairs)
        {
            lodepng_add_text(&state.info_png, entry.first.c_str(), entry.second.c_str());
        }

        unsigned error = lodepng::encode(png, image, size.x, size.y, state);
        if (!error) lodepng::save_file(png, job.filename.c_str());
        if (error) std::cout << "PNG encoder error " << error << ": " << lodepng_error_text(error) << std::endl;
    }
}
