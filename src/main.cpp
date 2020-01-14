#include "App.hpp"

int main(int argc, char* argv[]) 
{
    Window window{ "Mulen", glm::uvec2(1280, 720) };
    {
        Mulen::App mulen{ window };
        window.Run(mulen);
    }
    return 0;
}
