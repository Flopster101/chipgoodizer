#pragma once

namespace chipgoodizer
{

class SlewLimiter
{
public:
    void init(float maxSlewRate)
    {
        maxSlew = maxSlewRate;
        reset();
    }

    inline float process(float x)
    {
        float delta = x - prev;
        if (delta > maxSlew)
            prev += maxSlew;
        else if (delta < -maxSlew)
            prev -= maxSlew;
        else
            prev = x;
        return prev;
    }

    void reset()
    {
        prev = 0.0f;
    }

private:
    float maxSlew{0.14f};
    float prev{0.0f};
};

} // namespace chipgoodizer
