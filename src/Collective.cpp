#include "Collective.h"

#include <sc2api/sc2_unit.h>
#include <sc2api/sc2_agent.h>
#include <sc2api/sc2_interfaces.h>
#include <sc2lib/sc2_search.h>

#include "Map.h"

sc2::Units scbot::Collective::s_EmptyUnits {};

scbot::Collective::Collective(sc2::Agent* bot)
{
    NON_NULL(bot);

    this->bot = bot;
    
    m_Ramps = Map::FindRamps(Query(), Observation());
    m_Expansions = sc2::search::CalculateExpansionLocations(Observation(), Query());
}

scbot::Collective::~Collective()
{
}

sc2::Agent* scbot::Collective::GetBot()
{
    return bot;
}

const sc2::Agent* scbot::Collective::GetBot() const
{
    return bot;
}

const sc2::Units& scbot::Collective::GetAlliedUnitsOfType(sc2::UNIT_TYPEID type) const
{
    auto it = m_AlliedUnitsByType.find(type);
    if (it != m_AlliedUnitsByType.end()) {
        return it->second;
    }
    return s_EmptyUnits;
}

const sc2::Units& scbot::Collective::GetEnemyUnitsOfType(sc2::UNIT_TYPEID type) const
{
    auto it = m_EnemyUnitsByType.find(type);
    if (it != m_EnemyUnitsByType.end()) {
        return it->second;
    }
    return s_EmptyUnits;
}

const sc2::Units& scbot::Collective::GetNeutralUnitsOfType(sc2::UNIT_TYPEID type) const
{
    auto it = m_NeutralUnitsByType.find(type);
    if (it != m_NeutralUnitsByType.end()) {
        return it->second;
    }
    return s_EmptyUnits;
}

const sc2::Units& scbot::Collective::GetAlliedUnits() const
{
    return m_AlliedUnits;
}

const sc2::Units& scbot::Collective::GetEnemyUnits() const
{
    return m_EnemyUnits;
}

const sc2::Units& scbot::Collective::GetNeutralUnits() const
{
    return m_NeutralUnits;
}

const sc2::Units& scbot::Collective::GetAllUnits() const
{
    return m_AllUnits;
}

void scbot::Collective::OnStep()
{
    UpdateUnits();
}

sc2::ActionInterface* scbot::Collective::Actions()
{
    return bot->Actions();
}

sc2::ActionFeatureLayerInterface* scbot::Collective::ActionsFeatureLayer()
{
    return bot->ActionsFeatureLayer();
}

sc2::AgentControlInterface* scbot::Collective::AgentControl()
{
    return bot->AgentControl();
}

const sc2::ObservationInterface* scbot::Collective::Observation() const
{
    return bot->Observation();
}

sc2::QueryInterface* scbot::Collective::Query() const
{
    return bot->Query();
}

sc2::DebugInterface* scbot::Collective::Debug()
{
    return bot->Debug();
}

const std::vector<scdata::Ramp>& scbot::Collective::GetRamps() const
{
    return m_Ramps;
}

const std::vector<sc2::Point3D>& scbot::Collective::GetExpansions() const
{
    return m_Expansions;
}

sc2::Point2D scbot::Collective::GetClosestRamp(const sc2::Point2D& position) const
{
    float distance = std::numeric_limits<float>::max();
    sc2::Point2D closest_ramp;

    for (const scdata::Ramp& ramp : m_Ramps)
    {
        float d = sc2::DistanceSquared2D(position, ramp.point);
        if (d < distance)
        {
            distance = d;
            closest_ramp = ramp.point;
        }
    }

    return closest_ramp;
}

void scbot::Collective::UpdateUnits()
{
    const sc2::ObservationInterface* observation = bot->Observation();
    m_AllUnits = observation->GetUnits();

    m_AlliedUnits.clear();
    m_EnemyUnits.clear();
    m_NeutralUnits.clear();
    m_AlliedUnitsByType.clear();
    m_EnemyUnitsByType.clear();
    m_NeutralUnitsByType.clear();

    for (const sc2::Unit* unit : m_AllUnits)
    {
        switch (unit->alliance)
        {
        case sc2::Unit::Alliance::Self:
            m_AlliedUnits.push_back(unit);
            m_AlliedUnitsByType[unit->unit_type].push_back(unit);
            break;
        case sc2::Unit::Alliance::Enemy:
            m_EnemyUnits.push_back(unit);
            m_EnemyUnitsByType[unit->unit_type].push_back(unit);
            break;
        case sc2::Unit::Alliance::Neutral:
            m_NeutralUnits.push_back(unit);
            m_NeutralUnitsByType[unit->unit_type].push_back(unit);
            break;
        }
    }
}


