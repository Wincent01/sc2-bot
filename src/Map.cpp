#include "Map.h"

#include <algorithm>
#include <sc2api/sc2_common.h>
#include <sc2api/sc2_agent.h>
#include <sc2api/sc2_unit.h>
#include <sc2api/sc2_interfaces.h>
#include <sc2api/sc2_map_info.h>

#include "Utilities.h"

// Helper function to generate placement queries within a specified radius.
std::vector<sc2::QueryInterface::PlacementQuery> scbot::Map::GeneratePlacementQueries(
    const sc2::Point2D& center, 
    sc2::ABILITY_ID ability_id, 
    float min_radius, 
    float max_radius, 
    float step_size,
    const sc2::Units* pylons, 
    float pylon_radius,
    const sc2::Units* avoid_units, 
    float avoid_radius)
{
    std::vector<sc2::QueryInterface::PlacementQuery> queries;
    sc2::Point2D previous_grid;

    for (float r = min_radius; r < max_radius; r += 1.0f)
    {
        for (float loc = 0.0f; loc < 360.0f; loc += step_size) {
            sc2::Point2D point(
                r * std::cos((loc * 3.1415927f) / 180.0f) + center.x,
                r * std::sin((loc * 3.1415927f) / 180.0f) + center.y
            );

            if (pylons && std::all_of(pylons->begin(), pylons->end(), [&point, pylon_radius](const sc2::Unit* pylon) {
                    return sc2::DistanceSquared2D(pylon->pos, point) > pylon_radius * pylon_radius;
                })) {
                continue;
            }

            if (avoid_units && scbot::Utilities::AnyWithinRange(*avoid_units, point, avoid_radius)) {
                continue;
            }

            sc2::Point2D current_grid(std::floor(point.x), std::floor(point.y));

            if (previous_grid != current_grid) {
                queries.emplace_back(ability_id, point);
                previous_grid = current_grid;
            }
        }
    }

    return queries;
}

// Helper function to find the closest valid point to a pivot.
sc2::Point2D scbot::Map::FindClosestValidPoint(
    const std::vector<sc2::QueryInterface::PlacementQuery>& queries, 
    const std::vector<bool>& results, 
    const sc2::Point2D& pivot,
    bool prefer_distance)
{
    sc2::Point2D closest_point(0.0f, 0.0f);
    float closest_distance = prefer_distance ? std::numeric_limits<float>::max() : 0.0f;
    bool first = true;

    for (size_t i = 0; i < results.size(); ++i) {
        if (!results[i]) continue;

        const sc2::Point2D& point = queries[i].target_pos;
        float distance = sc2::DistanceSquared2D(pivot, point);

        if (first || (prefer_distance ? distance < closest_distance : distance > closest_distance)) {
            closest_point = point;
            closest_distance = distance;
            first = false;
        }
    }

    return closest_point;
}

// Refactored GetClosestPlace functions.
sc2::Point2D scbot::Map::GetClosestPlace(
    sc2::QueryInterface* query,
    const sc2::Point2D& center,
    sc2::ABILITY_ID ability_id,
    float min_radius,
    float max_radius,
    float step_size
)
{
    return scbot::Map::GetClosestPlace(query, center, center, ability_id, min_radius, max_radius, step_size);
}

sc2::Point2D scbot::Map::GetClosestPlace(
    sc2::QueryInterface* query,
    const sc2::Point2D& center,
    const sc2::Point2D& pivot,
    sc2::ABILITY_ID ability_id,
    float min_radius,
    float max_radius,
    float step_size
)
{
    auto queries = scbot::Map::GeneratePlacementQueries(center, ability_id, min_radius, max_radius, step_size);
    auto result = query->Placement(queries);
    return scbot::Map::FindClosestValidPoint(queries, result, pivot);
}

sc2::Point2D scbot::Map::GetClosestPlace(
    sc2::QueryInterface* query,
    const sc2::Point2D& center,
    const sc2::Point2D& pivot,
    const sc2::Units& pylons,
    sc2::ABILITY_ID ability_id,
    float min_radius,
    float max_radius,
    float step_size
)
{
    auto queries = scbot::Map::GeneratePlacementQueries(center, ability_id, min_radius, max_radius, step_size, &pylons, 5.0f);
    auto result = query->Placement(queries);
    return scbot::Map::FindClosestValidPoint(queries, result, pivot);
}

sc2::Point2D scbot::Map::GetClosestPlace(
    sc2::QueryInterface* query,
    const sc2::Point2D& pivot,
    const sc2::Units& pylons,
    sc2::ABILITY_ID ability_id,
    float min_radius,
    float max_radius,
    float step_size
)
{
    auto sorted_pylons = pylons;
    std::sort(sorted_pylons.begin(), sorted_pylons.end(), [&pivot](const sc2::Unit* a, const sc2::Unit* b) {
        return sc2::DistanceSquared2D(a->pos, pivot) < sc2::DistanceSquared2D(b->pos, pivot);
    });

    for (const auto& pylon : sorted_pylons) {
        auto queries = scbot::Map::GeneratePlacementQueries(pylon->pos, ability_id, min_radius, max_radius, step_size);
        auto result = query->Placement(queries);
        sc2::Point2D point = scbot::Map::FindClosestValidPoint(queries, result, pivot);
        if (point != sc2::Point2D(0.0f, 0.0f)) return point;
    }

    return sc2::Point2D(0.0f, 0.0f);
}

sc2::Point2D scbot::Map::GetClosestPlaceWhileAvoiding(sc2::QueryInterface* query, const sc2::Point2D& center, const sc2::Point2D& pivot, const sc2::Units& avoid, sc2::ABILITY_ID ability_id, float min_radius, float max_radius, float avoid_radius, bool prefer_distance, float step_size)
{
    auto queries = scbot::Map::GeneratePlacementQueries(center, ability_id, min_radius, max_radius, step_size, nullptr, 0.0f, &avoid, avoid_radius);
    auto result = query->Placement(queries);
    return scbot::Map::FindClosestValidPoint(queries, result, pivot, prefer_distance);
}

sc2::Point2D scbot::Map::GetBestCenter(sc2::QueryInterface* query, const sc2::Units& units, sc2::ABILITY_ID ability_id, float min_radius, float max_radius, float benchmark_radius, float step_size)
{
    auto best_center = sc2::Point2D(0.0f, 0.0f);
    int best_count = 0;

    for (const auto& unit : units) {
        auto queries = scbot::Map::GeneratePlacementQueries(unit->pos, ability_id, min_radius, max_radius, step_size);
        auto result = query->Placement(queries);

        for (size_t i = 0; i < result.size(); ++i) {
            if (!result[i]) continue;

            int count = std::count_if(units.begin(), units.end(), [&queries, i, benchmark_radius](const sc2::Unit* unit) {
                return sc2::DistanceSquared2D(unit->pos, queries[i].target_pos) < benchmark_radius * benchmark_radius;
            });

            if (count > best_count) {
                best_center = queries[i].target_pos;
                best_count = count;
            }
        }
    }

    return best_center;
}

std::pair<sc2::Point2D, float> scbot::Map::GetBestPath(sc2::QueryInterface* query, const sc2::Unit* unit, const sc2::Point2D &center, float min_radius, float max_radius, float step_size)
{
    sc2::Point2D current_grid;

    sc2::Point2D previous_grid;

    int valid_queries = 0;

    std::vector<sc2::QueryInterface::PathingQuery> queries;

    for (float r = min_radius; r < max_radius; r += 1.0f)
    {
        float loc = 0.0f;

        while (loc < 360.0f) {
            sc2::Point2D point = sc2::Point2D((r * std::cos((loc * 3.1415927f) / 180.0f)) + center.x,
                                    (r * std::sin((loc * 3.1415927f) / 180.0f)) + center.y);

            current_grid = sc2::Point2D(std::floor(point.x), std::floor(point.y));

            if (previous_grid != current_grid) {
                sc2::QueryInterface::PathingQuery query{unit->tag, unit->pos, point};
                queries.push_back(query);
                ++valid_queries;
            }

            previous_grid = current_grid;
            loc += step_size;
        }
    }

    if (queries.size() == 0) {
        return std::make_pair(center, 0.0f);
    }

    auto result = query->PathingDistance(queries);

    // Find the shortest path.
    auto best_path = center;
    auto shortest_distance = std::numeric_limits<float>::max();

    for (auto i = 0; i < result.size(); ++i) {
        if (result[i] <= 0.0f) {
            continue;
        }

        const auto& point = queries[i].end_;

        if (result[i] < shortest_distance) {
            best_path = point;
            shortest_distance = result[i];
        }
    }

    return std::make_pair(best_path, shortest_distance);
}

std::vector<scdata::Ramp> scbot::Map::FindRamps(sc2::QueryInterface* query, const sc2::ObservationInterface* observation)
{
    const auto& gameInfo = observation->GetGameInfo();

    std::vector<sc2::Point3D> rampTerrain;
    std::unordered_set<float> playableHeights;

    for (auto i = gameInfo.playable_min.x; i < gameInfo.playable_max.x; i++)
    {
        for (auto j = gameInfo.playable_min.y; j < gameInfo.playable_max.y; j++)
        {
            const auto& point = sc2::Point2D(i, j);
            const auto& pathing = observation->IsPathable(point);
            const auto& placement = observation->IsPlacable(point);
            const auto height = observation->TerrainHeight(point);

            if (placement)
            {
                playableHeights.insert(height);
            }

            if (pathing && !placement)
            {
                rampTerrain.push_back(sc2::Point3D(point.x, point.y, height));
            }
        }
    }

    // Filter out those points which are not within 0.3 of a playable height.
    for (auto i = 0; i < rampTerrain.size(); i++)
    {
        const auto& point = rampTerrain[i];
        
        auto found = false;

        for (auto height : playableHeights)
        {
            if (std::abs(point.z - height) < 0.3f)
            {
                found = true;
                break;
            }
        }

        if (!found)
        {
            rampTerrain.erase(rampTerrain.begin() + i);
            i--;
        }
    }

    // Cluster the points.
    auto clusters = std::vector<std::pair<sc2::Point3D, std::vector<sc2::Point3D>>>();

    for (auto i = 0; i < rampTerrain.size(); i++)
    {
        const auto& point = rampTerrain[i];

        auto found = false;

        for (auto& cluster : clusters)
        {
            // Has to be within 15 units of the cluster and not different in height by more than 0.5.
            if (sc2::DistanceSquared2D(cluster.first, point) < 7.0f * 7.0f &&
                std::abs(cluster.first.z - point.z) < 0.5f)
            {
                cluster.second.push_back(point);
                found = true;

                // Recalculate the center of mass.
                sc2::Point3D center = {0.0f, 0.0f, 0.0f};

                for (auto& p : cluster.second)
                {
                    center.x += p.x;
                    center.y += p.y;
                    center.z += p.z;
                }

                center.x /= cluster.second.size();
                center.y /= cluster.second.size();
                center.z /= cluster.second.size();

                cluster.first = center;

                break;
            }
        }

        if (!found)
        {
            clusters.push_back({point, {point}});
        }
    }

    auto ramps = std::vector<scdata::Ramp>();

    ramps.resize(clusters.size());

    for (auto i = 0; i < clusters.size(); i++)
    {
        ramps[i].point = clusters[i].first;
    }
    
    return ramps;
}
