#pragma once

#include <array>
#include <ranges>

#include <SDL3/SDL_scancode.h>

enum class KeyState
{
    Up, Pressed, Down, Released
};

class Keyboard
{ 
public:
    Keyboard() 
    {
        std::ranges::fill(m_Scancodes, KeyState::Up);
    }

    void Process()
    {
        for(auto& keyState : m_Scancodes)
        {
            if(keyState == KeyState::Pressed)
                keyState = KeyState::Down;
            else if(keyState == KeyState::Released)
                keyState = KeyState::Up;
        }
    }

    void UpdateKeyState(SDL_Scancode scancode, bool up)
    {
        if(scancode < 0 || scancode > ScancodesMax)
            return;
        
        const bool down = !up;   
        KeyState oldState = m_Scancodes[scancode];
        
        if(down)
        {
            m_Scancodes[scancode] = (oldState == KeyState::Up)
                ? KeyState::Pressed
                : KeyState::Down;
        }
        else
        {
            m_Scancodes[scancode] = (oldState == KeyState::Down)
                ? KeyState::Released
                : KeyState::Up;
        }
    }

    bool KeyUp(SDL_Scancode code)
    {
        return m_Scancodes[code] == KeyState::Up || m_Scancodes[code] == KeyState::Released; 
    }

    bool KeyDown(SDL_Scancode code)
    {
        return m_Scancodes[code] == KeyState::Down || m_Scancodes[code] == KeyState::Pressed;         
    }

    bool KeyPressed(SDL_Scancode code)
    {
        return m_Scancodes[code] == KeyState::Pressed;
    }

    bool KeyReleased(SDL_Scancode code)
    {
        return m_Scancodes[code]  == KeyState::Released; 
    }

private:
    constexpr static int ScancodesMax = 512;
    std::array<KeyState, ScancodesMax> m_Scancodes;
};