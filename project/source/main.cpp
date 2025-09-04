#include <print>
#include <chrono>

#include <SDKDDKVer.h>
#include <asio.hpp>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#include <stb_image.h>
#include <yaml-cpp/yaml.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../include/CommandlineArguments.hpp"
#include "../include/Keyboard.h"
#include "../include/Networking.h"

SDL_Texture* LoadTexture(SDL_Renderer* renderer, const std::string& path)
{
    int width = 0, height = 0, channels = 0;
    auto pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);

    if(!pixels)
    {
        std::println("Failed to load \"{}\"", path);
        return nullptr;
    }

    SDL_Texture* texture = SDL_CreateTexture
    (
        renderer,
        SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STATIC,
        width,
        height
    );

    if(!texture)
    {
        std::println("SDL_CreateTexture failed when loading {}: {}", path, SDL_GetError());
        stbi_image_free(pixels);

        return nullptr;
    }

    if(!SDL_UpdateTexture(texture, nullptr, static_cast<void*>(pixels), 4 * width))
    {
        std::println("SDL_UpdateTexture failed for {}: {}", path, SDL_GetError());
    }

    if(!SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST))
    {
        std::println("SDL_SetTextureScaleMode failed for {}: {}", path, SDL_GetError());
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

    Frame3::CommandlineArguments argumenets(argc, argv);

    SDL_Window* window = SDL_CreateWindow("Some application", 1024, 768, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);

    ImGui::CreateContext();

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    ImGuiIO& imguiIO = ImGui::GetIO();

    glm::vec3 playerPosition(0, 0, 0);
    SDL_Texture* texture = LoadTexture(renderer, "resources/Bro-0001.png");
    
    glm::vec3 grassPosition(0, 0, 0);
    glm::vec3 grassPosition2(16, 0, 0);
    SDL_Texture* grassTexture = LoadTexture(renderer, "resources/Grass-0001.png");

    SDL_Texture* chainlinkFence = LoadTexture(renderer, "resources/Chainlink-0001.png");
    glm::vec3 fencePosition(0, 0, 0);

    SDL_Texture* house = LoadTexture(renderer, "resources/building-0001.png");
    glm::vec3 housePosition(40, 0, 0);

    SDL_Texture* building = LoadTexture(renderer, "resources/building-0002.png");
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

    std::unique_ptr<Networking::UDPServer> server;
    std::unique_ptr<Networking::UDPClient> client;

    enum class OnlineStatus { Offline, Client, Host };

    OnlineStatus onlineStatus = OnlineStatus::Offline;

    bool showImGuiDemo = false;
    int upscaleFactor = 6;

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
                    const char* items = "Linear\0Nearest\0PixelArt\0";
                    
                    if(ImGui::Combo("RenderTexture ScaleMod", &currentItem, items))
                    {
                        switch(currentItem)
                        {
                            case 0:
                                SDL_SetTextureScaleMode(renderTexture, SDL_SCALEMODE_LINEAR);
                                std::println("LINEAR");
                            break;
                            case 1:
                                SDL_SetTextureScaleMode(renderTexture, SDL_SCALEMODE_NEAREST);
                                std::println("Nearest");
                            break;
                            case 2:
                                SDL_SetTextureScaleMode(renderTexture, SDL_SCALEMODE_PIXELART);
                                std::println("Pixel art");
                            break;
                        }
                    }

                    ImGui::DragInt("Upscale factor", &upscaleFactor, 0.06f, 1, 10);

                    ImGui::Checkbox("Show ImGui demo", &showImGuiDemo);

                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Networking"))
                {
                    switch(onlineStatus)
                    {
                        case OnlineStatus::Offline:
                        {
                            ImGui::Text("Host");

                            static int hostPort = 13998; 
                            ImGui::InputInt("Port", &hostPort);
                            if(ImGui::Button("Start server"))
                            {
                                if(!server)
                                    server = std::make_unique<Networking::UDPServer>();

                                server->Start(hostPort);
                                onlineStatus = OnlineStatus::Host;
                            }

                            ImGui::Text("Join");
                            
                            static char buffer[32] {"127.0.0.1\0"};
                            ImGui::InputText("IP", buffer, sizeof(buffer));
                            static int clientPort = 13998;
                            ImGui::InputInt("Port#1", &clientPort);

                            if(ImGui::Button("Join server"))
                            {
                                if(!client)
                                    client = std::make_unique<Networking::UDPClient>();
                                    
                                try
                                {
                                    client->Connect(asio::ip::udp::endpoint(asio::ip::make_address(buffer), clientPort));
                                    onlineStatus = OnlineStatus::Client;
                                }
                                catch(std::exception& e)
                                {
                                    std::println("Error: {}", e.what());
                                }
                            }

                        } break;
                        case OnlineStatus::Client:
                        {
                            ImGui::Text("Client");
                            ImGui::Separator();

                            if(ImGui::Button("Disconnect"))
                            {
                                client->Disconnect();
                                onlineStatus = OnlineStatus::Offline;
                            }

                            if(ImGui::Button("Send data"))
                            {
                                client->Send("Hello?", false);
                            }
                        } break;
                        case OnlineStatus::Host:
                        {
                            ImGui::Text("Server");
                            ImGui::Separator();
                            
                            static std::array<char, 32> buffer {"Hello from server\0"};
                            ImGui::InputText("Broadcast message", buffer.data(), buffer.size());
                            
                            if(ImGui::Button("Send"))
                            {
                                server->Broadcast(buffer.data());
                            }

                            if(ImGui::Button("Stop server"))
                            {
                                server->Stop();
                                onlineStatus = OnlineStatus::Offline;
                            }
                        } break;
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