#pragma once

#include <memory>

namespace scbot
{

class Collective;

/**
 * @brief Manager for the army units.
 */
class Liberation
{
public:
    /**
     * @brief Construct a new Liberation object
     */
    Liberation(std::shared_ptr<Collective> collective);

    /**
     * @brief Method that is called every frame. Updates the army units.
     */
    void OnStep();

    /**
     * @brief Destroy the Liberation object
     */
    ~Liberation();

private:
    std::shared_ptr<Collective> m_Collective;
};

}