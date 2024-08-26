#pragma once

#include <memory>

#include "Data.h"

namespace scbot
{

class Collective;

/**
 * @brief Manager for the economy of the bot.
 */
class Economy
{
public:
    /**
     * @brief Construct a new Economy object
     */
    Economy(std::shared_ptr<Collective> collective);

    /**
     * @brief Destroy the Economy object
     */
    ~Economy();

    /**
     * @brief Get the current mineral and gas count.
     * 
     * @return The current mineral and gas count
     */
    const scdata::ResourcePair& GetResources() const;

    /**
     * @brief Method that is called every frame. Updates the resources.
     */
    void OnStep();

private:
    std::shared_ptr<Collective> m_Collective;
    scdata::ResourcePair m_Resources;
};

}