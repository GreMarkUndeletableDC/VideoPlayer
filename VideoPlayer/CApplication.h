#pragma once

class CApplication
{
public:
    static void Initialize() noexcept;
};

extern CApplication* App;