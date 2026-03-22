#include <print>
#include <chrono>
#include <filesystem>

// #include <SDKDDKVer.h>
#include <asio.hpp>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#include <stb_image.h>
#include <yaml-cpp/yaml.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "CommandlineArguments.hpp"
#include "Keyboard.hpp"

#include "Client.hpp"
#include "Server.hpp"

#include "ImGui_Extension.hpp"

#include "Networking/NetworkSystem/NetworkSystem.hpp"
#include "Networking/NetworkSystem/Serialization/StreamWriter.hpp"
#include "Networking/NetworkSystem/Serialization/StreamReader.hpp"

#include "Handle.hpp"
#include "VariantUtilities.hpp"

namespace Paths
{
    // Path to the directory containing the executable
    static std::filesystem::path Root;

    static std::filesystem::path Resources() 
    {
        return Root / "resources";
    }
};

SDL_Texture* LoadTexture(SDL_Renderer* renderer, const std::filesystem::path& path)
{
    const SDL_PixelFormat format = SDL_PIXELFORMAT_RGBA32;
    const int desiredChannels = SDL_BYTESPERPIXEL(format);

    const auto pathString = path.generic_string();

    int width = 0, height = 0, channels = 0;
    auto pixels = stbi_load(pathString.c_str(), &width, &height, &channels, desiredChannels);
    
    if(!pixels)
    {
        std::println("Failed to load \"{}\"", pathString);
        return nullptr;
    }
    
    if(channels != desiredChannels)
    {
        std::println("Color channel count ({}) in {} doesn't match the expected value of {}", channels, pathString, desiredChannels);
        stbi_image_free(pixels);
        
        return nullptr;
    }
    
    SDL_Texture* texture = SDL_CreateTexture
    (
        renderer,
        format,
        SDL_TEXTUREACCESS_STATIC,
        width,
        height
    );

    if(!texture)
    {
        std::println("SDL_CreateTexture failed when loading {}: {}", pathString, SDL_GetError());
        stbi_image_free(pixels);

        return nullptr;
    }
    
    if(!SDL_UpdateTexture(texture, nullptr, static_cast<void*>(pixels), width * SDL_BYTESPERPIXEL(format)))
    {
        std::println("SDL_UpdateTexture failed for {}: {}", pathString, SDL_GetError());
    }

    if(!SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST))
    {
        std::println("SDL_SetTextureScaleMode failed for {}: {}", pathString, SDL_GetError());
    }

    stbi_image_free(pixels);

    return texture;
}

void Render(SDL_Renderer* renderer, SDL_Texture* texture, glm::vec3 position, const glm::mat4x4& viewMatrix, bool flip = false)
{
    // Use bottom left as the origin for drawing. SDL uses top left
    const glm::mat4 localOrigin = glm::translate(glm::mat4(1), glm::vec3(0, texture->h, 0));

    const glm::vec4 transformedCoordinate = viewMatrix * localOrigin * glm::vec4(position, 1);

    const SDL_FRect destination {transformedCoordinate.x, transformedCoordinate.y, static_cast<float>(texture->w), static_cast<float>(texture->h)};
    
    SDL_RenderTextureRotated(renderer, texture, nullptr, &destination, 0, nullptr, !flip ? SDL_FLIP_NONE : SDL_FLIP_HORIZONTAL);
}

int main(int argc, char* argv[])
{
    using Clock = std::chrono::steady_clock;
    using Timepoint = Clock::time_point;

    Frame3::CommandlineArguments arguments(argc, argv);

    // +Resource path
    auto rootDir = arguments.get(1);
    if(!rootDir)
    {
        std::println("1.Pass the full path to the folder containing the executable as the first argument to the program.");
        return 0;
    }

    Paths::Root = std::filesystem::path(*rootDir);
    std::println("Project root: {}", Paths::Root.string());
    
    if(!std::filesystem::exists(Paths::Root) || !std::filesystem::is_directory(Paths::Root))
    {
        std::println("Path provided doesn't exist");
        return 0;
    }
    
    std::println("Resource path: {}", Paths::Resources().string());
    // -Resource path

    Networking::NetworkSystem netsystem;
    auto listener = netsystem.Listen(81337);
    auto connection = netsystem.Connect("127.0.0.1", 81337);

    SDL_Window* window = SDL_CreateWindow("Some application", 1024, 768, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);

    ImGui::CreateContext();

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    ImGuiIO& imguiIO = ImGui::GetIO();

    glm::vec3 playerPosition(0, 0, 0);
    SDL_Texture* texture = LoadTexture(renderer, Paths::Resources()/"Bro-0001.png");
    
    glm::vec3 grassPosition(0, 0, 0);
    glm::vec3 grassPosition2(16, 0, 0);
    SDL_Texture* grassTexture = LoadTexture(renderer, Paths::Resources()/"Grass-0001.png");

    SDL_Texture* chainlinkFence = LoadTexture(renderer, Paths::Resources()/"Chainlink-0001.png");
    glm::vec3 fencePosition(0, 0, 0);

    SDL_Texture* house = LoadTexture(renderer, Paths::Resources()/"building-0001.png");
    glm::vec3 housePosition(40, 0, 0);

    SDL_Texture* building = LoadTexture(renderer, Paths::Resources()/"building-0002.png");
    glm::vec3 buildingPosition(128, 0, 0); 

    glm::vec3 nativeResolution(320, 180, 0);
    // nativeResolution *= 4;

    glm::mat4x4 viewMatrix(1);
    viewMatrix = glm::translate(viewMatrix, glm::vec3(0, nativeResolution.y, 0));
    viewMatrix = glm::scale(viewMatrix, glm::vec3(1, -1, 1));

    SDL_Texture* renderTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, nativeResolution.x, nativeResolution.y);
    if(!SDL_SetTextureScaleMode(renderTexture, SDL_SCALEMODE_NEAREST))
    {
        std::println("SDL_SetTextureScaleMode failed for renderTexture: {}", SDL_GetError());
    }

    bool showImGuiDemo = false;
    int upscaleFactor = 4;

    float walkingSpeed = 25.0f;

    float jumpForce = 98.1f;
    float dashForce = 98.1f;

    glm::vec3 playerVelocity(0, 0, 0);

    bool playerLookingRight = true;

    Keyboard keyboard;

    Timepoint previousFrameTime = Clock::now();
    bool open = true;

    while(open)
    {
        Timepoint now = Clock::now();
        float deltaTime = std::chrono::duration<float>(now - previousFrameTime).count();
        previousFrameTime = now;

        keyboard.Process();

        SDL_Event event;
        while(SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            switch(event.type)
            {
                case SDL_EVENT_QUIT:
                    open = false;
                break;
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    if(event.window.windowID == SDL_GetWindowID(window))
                        open = false;
                break;
                case SDL_EVENT_KEY_DOWN:
                case SDL_EVENT_KEY_UP:
                {
                    keyboard.UpdateKeyState(event.key.scancode, !event.key.down);
                } break;
            }
        }

        netsystem.Update();

        for(auto& networkEvent : netsystem.Events())
        {
            using namespace asd::Variant;
            DispatchLenient
            (
                networkEvent,
                [](Networking::Event::ListenerStarted const& e)
                {
                    std::println("Netsystem: Listener with handle: {} is ready", e.handle.Index());
                },
                [](Networking::Event::ListenerClosed const& e)
                {
                    std::println("Netsystem: Listener with handle {} closed with reason: {}", e.handle.Index(), static_cast<int>(e.listenerError));
                },
                [](Networking::Event::IncomingConnectionAccepted const& e)
                {
                    std::println("Netsystem: New connection accepted! Listener: {} Connection handle: {}", e.listener.Index(), e.connection.Index());
                },
                [](Networking::Event::OutgoingConnectionEstablished const& e)
                {
                    std::println("Netsystem: Outgoing connection established! Handle: {}", e.handle.Index());
                },
                [](Networking::Event::DataReceived const& e)
                {
                    std::print("Netsystem: Data received from handle {}: ", e.handle.Index());
                    for(auto byte : e.data)
                        std::print("{} ", static_cast<int>(byte));
                    std::println("");
                },
                [](Networking::Event::ConnectionClosed const& e)
                {
                    std::println("Netsystem: Connection with handle {} closed with reason: {}", e.handle.Index(), static_cast<int>(e.connectionError));
                }
            );
        }

// Input
        if(keyboard.KeyPressed(SDL_SCANCODE_ESCAPE))
            open = false;

        if(keyboard.KeyDown(SDL_SCANCODE_A))
        {
            playerPosition.x += -walkingSpeed * deltaTime;
            playerLookingRight = false;
        }
            
        if(keyboard.KeyDown(SDL_SCANCODE_D))
        {
            playerPosition.x += walkingSpeed * deltaTime;
            playerLookingRight = true;
        }
        
        if(keyboard.KeyPressed(SDL_SCANCODE_SPACE))
        {
            playerVelocity.y = jumpForce;
        }

        if(keyboard.KeyPressed(SDL_SCANCODE_LSHIFT))
        {
            playerVelocity.x = dashForce  * (playerLookingRight ? 1 : -1);
            playerVelocity.y = 0;
        }

// Logics 
        playerPosition += playerVelocity * deltaTime;
        
        if(playerPosition.y < 0)
        {
            playerVelocity.y = 0;
            playerPosition.y = 0;
        }

        if(playerPosition.y > 0)
        {
            playerVelocity.y -= 9.81f * deltaTime * 15;
        }
        
        if(playerVelocity.x != 0)
        {
            playerVelocity.x += 9.81 * deltaTime * 15 * (playerVelocity.x > 0 ? -1 : 1);
            playerVelocity.y = 0;

            if(glm::abs(playerVelocity.x) <= 0.1f)
            {
                playerVelocity.x = 0;
            }
        }

// Begin new UI Frame
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        if(showImGuiDemo)
            ImGui::ShowDemoWindow();

        if(ImGui::Begin("Controls"))
        {
            if (ImGui::BeginTabBar("MyTabBar"))
            {
                if (ImGui::BeginTabItem("Misc"))
                {
                    static int currentItem = 1;
                    const char* const items[] = {"Linear", "Nearest", "PixelArt"};
                    
                    if(ImGui::Combo("RenderTexture ScaleMod", &currentItem, items, 3))
                    {
                        switch(currentItem)
                        {
                            case 0: SDL_SetTextureScaleMode(renderTexture, SDL_SCALEMODE_LINEAR); break;
                            case 1: SDL_SetTextureScaleMode(renderTexture, SDL_SCALEMODE_NEAREST); break;
                            case 2: SDL_SetTextureScaleMode(renderTexture, SDL_SCALEMODE_PIXELART); break;
                        }
                    }

                    ImGui::DragInt("Upscale factor", &upscaleFactor, 0.06f, 1, 10);

                    ImGui::Checkbox("Show ImGui demo", &showImGuiDemo);

                    ImGui::EndTabItem();
                }
                if(ImGui::BeginTabItem("Network System"))
                {
                    if(ImGui::Button("Send Hello to clients"))
                    {
                        Networking::ByteVector buff(sizeof(Networking::Packet::Header) + 32);
                        
                        Networking::Packet::Header header{};
                        header.protocol = Networking::sProtocol2;

                        Serialization::StreamWriter writer(buff);
                        writer.Write(header);
                        writer.Write(std::string("Hello"));
                        
                        if(!writer.Ok())
                            std::println("Problem writing to buffer");

                        netsystem.Send(connection, std::span{buff.data(), writer.StreamPosition()});
                    }

                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
        }
        ImGui::End();

        // End UI Frame
        ImGui::Render();

        //RenderTexture
        SDL_SetRenderTarget(renderer, renderTexture);
        SDL_SetRenderDrawColor(renderer, 135, 206, 235, 255);
        
        SDL_RenderClear(renderer);
        
        Render(renderer, building, buildingPosition, viewMatrix);
        Render(renderer, house, housePosition, viewMatrix);
        Render(renderer, grassTexture, grassPosition, viewMatrix);
            Render(renderer, texture, playerPosition, viewMatrix, !playerLookingRight);
        Render(renderer, chainlinkFence, fencePosition, viewMatrix);
        Render(renderer, grassTexture, grassPosition2, viewMatrix);
        
        // Framebuffer
        SDL_SetRenderTarget(renderer, nullptr);
        
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderClear(renderer);
        SDL_FRect destination {0, 0, static_cast<float>(renderTexture->w * upscaleFactor), static_cast<float>(renderTexture->h * upscaleFactor)};
        SDL_RenderTexture(renderer, renderTexture, nullptr, &destination);

        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}