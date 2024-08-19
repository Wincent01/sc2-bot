#pragma once

#include <sc2api/sc2_agent.h>
#include <sc2api/sc2_unit.h>
#include <sc2api/sc2_interfaces.h>

#include "Data.h"

namespace scbot::Map {

std::vector<sc2::QueryInterface::PlacementQuery> GeneratePlacementQueries(
    const sc2::Point2D& center, 
    sc2::ABILITY_ID ability_id, 
    float min_radius, 
    float max_radius, 
    float step_size,
    const sc2::Units* pylons = nullptr, 
    float pylon_radius = 0.0f,
    const sc2::Units* avoid_units = nullptr, 
    float avoid_radius = 0.0f
);

sc2::Point2D FindClosestValidPoint(
    const std::vector<sc2::QueryInterface::PlacementQuery>& queries, 
    const std::vector<bool>& results, 
    const sc2::Point2D& pivot,
    bool prefer_distance = true
);

sc2::Point2D GetClosestPlace(
    sc2::QueryInterface* query,
    const sc2::Point2D& center, 
    sc2::ABILITY_ID ability_id, 
    float min_radius, 
    float max_radius, 
    float step_size = 45.0f
);

sc2::Point2D GetClosestPlace(
    sc2::QueryInterface* query,
    const sc2::Point2D& center, 
    const sc2::Point2D& pivot, 
    sc2::ABILITY_ID ability_id, 
    float min_radius, 
    float max_radius, 
    float step_size = 45.0f
);

sc2::Point2D GetClosestPlace(
    sc2::QueryInterface* query,
    const sc2::Point2D& center, 
    const sc2::Point2D& pivot, 
    const sc2::Units& pylons, 
    sc2::ABILITY_ID ability_id, 
    float min_radius, 
    float max_radius, 
    float step_size = 45.0f
);

sc2::Point2D GetClosestPlace(
    sc2::QueryInterface* query,
    const sc2::Point2D& pivot, 
    const sc2::Units& pylons, 
    sc2::ABILITY_ID ability_id, 
    float min_radius, 
    float max_radius, 
    float step_size = 45.0f
);

sc2::Point2D GetClosestPlaceWhileAvoiding(
    sc2::QueryInterface* query,
    const sc2::Point2D& center, 
    const sc2::Point2D& pivot, 
    const sc2::Units& avoid, 
    sc2::ABILITY_ID ability_id, 
    float min_radius, 
    float max_radius, 
    float avoid_radius, 
    bool prefer_distance, 
    float step_size = 45.0f
);

sc2::Point2D GetClosestPlace(
    sc2::QueryInterface* query,
    sc2::Point2D& center,
    const sc2::Point2D& pivot,
    const sc2::Units& pylons,
    sc2::ABILITY_ID ability_id,
    float min_radius,
    float max_radius,
    float step_size = 45.0f
);


sc2::Point2D GetBestCenter(
    sc2::QueryInterface* query,
    const sc2::Units& units, 
    sc2::ABILITY_ID ability_id, 
    float min_radius, 
    float max_radius, 
    float benchmark_radius, 
    float step_size = 45.0f
);

std::pair<sc2::Point2D, float> GetBestPath(
    sc2::QueryInterface* query,
    const sc2::Unit* unit, 
    const sc2::Point2D& center, 
    float min_radius, 
    float max_radius, 
    float step_size = 45.0f
);

std::vector<scdata::Ramp> FindRamps(
    sc2::QueryInterface* query,
    const sc2::ObservationInterface* observation
);

}