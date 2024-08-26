#include "Economy.h"

#include <sc2api/sc2_agent.h>
#include <sc2api/sc2_interfaces.h>

#include "Collective.h"

scbot::Economy::Economy(std::shared_ptr<Collective> collective)
{
    m_Collective = collective;
}

scbot::Economy::~Economy()
{
}

const scdata::ResourcePair& scbot::Economy::GetResources() const
{
    return m_Resources;
}

void scbot::Economy::OnStep()
{
    const auto& observation = m_Collective->Observation();

    m_Resources.minerals = observation->GetMinerals();
    m_Resources.vespene = observation->GetVespene();
}
