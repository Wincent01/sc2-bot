#include "Utilities.h"

#include "Data.h"
#include "Config.h"

bool scbot::Utilities::HasQueuedOrder(const sc2::Unit* unit, sc2::ABILITY_ID ability_id) {
    NON_NULL(unit);

    for (const auto& order : unit->orders) {
        if (order.ability_id == ability_id) {
            return true;
        }
    }

    return false;
}

bool scbot::Utilities::HasQueuedOrder(const sc2::Units& units, sc2::ABILITY_ID ability_id) {
    for (const auto& unit : units) {
        if (HasQueuedOrder(unit, ability_id)) {
            return true;
        }
    }

    return false;
}

bool scbot::Utilities::HasQueuedOrder(const sc2::Unit* unit, sc2::ABILITY_ID ability_id, sc2::Tag target_unit_tag) {
    NON_NULL(unit);

    for (const auto& order : unit->orders) {
        if (order.ability_id == ability_id && order.target_unit_tag == target_unit_tag) {
            return true;
        }
    }

    return false;
}

bool scbot::Utilities::HasQueuedOrder(const sc2::Units& units, sc2::ABILITY_ID ability_id, sc2::Tag target_unit_tag) {
    for (const auto& unit : units) {
        if (HasQueuedOrder(unit, ability_id, target_unit_tag)) {
            return true;
        }
    }

    return false;
}

bool scbot::Utilities::HasQueuedOrder(const sc2::Unit* unit, sc2::Tag target_unit_tag) {
    NON_NULL(unit);

    for (const auto& order : unit->orders) {
        if (order.target_unit_tag == target_unit_tag) {
            return true;
        }
    }

    return false;
}

bool scbot::Utilities::HasQueuedOrder(const sc2::Units& units, sc2::Tag target_unit_tag) {
    for (const auto& unit : units) {
        if (HasQueuedOrder(unit, target_unit_tag)) {
            return true;
        }
    }

    return false;
}

const sc2::Unit* scbot::Utilities::LeastBusy(const sc2::Units& units)
{
    NON_EMPTY(units);

    const sc2::Unit* least_busy = nullptr;
    uint32_t least_orders = std::numeric_limits<uint32_t>::max();

    for (const auto& unit : units) {
        if (unit->orders.size() < least_orders) {
            least_busy = unit;
            least_orders = unit->orders.size();
        }
    }

    return least_busy;
}

bool scbot::Utilities::IsInProgress(const sc2::Unit *unit)
{
    NON_NULL(unit);
    
    return unit->build_progress < 1.0f;
}

bool scbot::Utilities::AllInProgress(const sc2::Units& units) {
    if (units.empty()) {
        return false;
    }

    for (const auto& unit : units) {
        if (!IsInProgress(unit)) {
            return false;
        }
    }

    return true;
}

sc2::Units scbot::Utilities::FilterUnits(const sc2::Units& units, std::function<bool(const sc2::Unit*)> predicate) {
    sc2::Units filtered_units;

    for (const auto& unit : units) {
        if (predicate(unit)) {
            filtered_units.push_back(unit);
        }
    }

    return filtered_units;
}

sc2::Units scbot::Utilities::FilterOutInProgress(const sc2::Units& units) {
    return FilterUnits(units, [](const sc2::Unit* unit) {
        return !IsInProgress(unit);
    });
}

bool scbot::Utilities::IsGathering(const sc2::Unit* unit) {
    NON_NULL(unit);

    return std::any_of(unit->orders.begin(), unit->orders.end(), [](const sc2::UnitOrder& order) {
        return scdata::MiningAbilities.find(order.ability_id) != scdata::MiningAbilities.end();
    });
}

bool scbot::Utilities::IsGatheringFrom(const sc2::Unit* unit, const sc2::Units& points) {
    NON_NULL(unit);

    for (const auto& order : unit->orders) {
        if (scdata::MiningAbilities.find(order.ability_id) == scdata::MiningAbilities.end()) {
            continue;
        }
        
        for (const auto& mineral : points) {
            if (mineral->tag == order.target_unit_tag) {
                return true;
            }
        }
    }
    
    return false;
}

bool scbot::Utilities::IsGatheringFrom(const sc2::Unit* unit, const sc2::Unit* point) {
    NON_NULL(unit);
    NON_NULL(point);

    for (const auto& order : unit->orders) {
        if (scdata::MiningAbilities.find(order.ability_id) == scdata::MiningAbilities.end()) {
            continue;
        }

        if (point->tag == order.target_unit_tag) {
            return true;
        }
    }

    return false;
}

bool scbot::Utilities::IsIdle(const sc2::Unit* unit)
{
    NON_NULL(unit);

    return unit->orders.empty();
}

bool scbot::Utilities::IsDepleted(const sc2::Unit* unit) {
    NON_NULL(unit);

    return unit->mineral_contents == 0 && unit->vespene_contents == 0;
}

bool scbot::Utilities::IsMineralField(const sc2::Unit* unit) {
    NON_NULL(unit);

    const auto type = unit->unit_type;
    return 
        type == sc2::UNIT_TYPEID::NEUTRAL_MINERALFIELD ||
        type == sc2::UNIT_TYPEID::NEUTRAL_MINERALFIELD450 ||
        type == sc2::UNIT_TYPEID::NEUTRAL_MINERALFIELD750 ||
        type == sc2::UNIT_TYPEID::NEUTRAL_RICHMINERALFIELD ||
        type == sc2::UNIT_TYPEID::NEUTRAL_RICHMINERALFIELD750 ||
        type == sc2::UNIT_TYPEID::NEUTRAL_PURIFIERMINERALFIELD ||
        type == sc2::UNIT_TYPEID::NEUTRAL_PURIFIERMINERALFIELD750 ||
        type == sc2::UNIT_TYPEID::NEUTRAL_PURIFIERRICHMINERALFIELD ||
        type == sc2::UNIT_TYPEID::NEUTRAL_PURIFIERRICHMINERALFIELD750 ||
        type == sc2::UNIT_TYPEID::NEUTRAL_LABMINERALFIELD ||
        type == sc2::UNIT_TYPEID::NEUTRAL_LABMINERALFIELD750 ||
        type == sc2::UNIT_TYPEID::NEUTRAL_BATTLESTATIONMINERALFIELD ||
        type == sc2::UNIT_TYPEID::NEUTRAL_BATTLESTATIONMINERALFIELD750;
}

bool scbot::Utilities::IsVespeneGeyser(const sc2::Unit* unit) {
    NON_NULL(unit);

    const auto type = unit->unit_type;
    return 
        type == sc2::UNIT_TYPEID::NEUTRAL_VESPENEGEYSER ||
        type == sc2::UNIT_TYPEID::NEUTRAL_PROTOSSVESPENEGEYSER ||
        type == sc2::UNIT_TYPEID::NEUTRAL_SPACEPLATFORMGEYSER ||
        type == sc2::UNIT_TYPEID::NEUTRAL_PURIFIERVESPENEGEYSER ||
        type == sc2::UNIT_TYPEID::NEUTRAL_SHAKURASVESPENEGEYSER ||
        type == sc2::UNIT_TYPEID::NEUTRAL_RICHVESPENEGEYSER;
}

bool scbot::Utilities::IsExtractor(const sc2::Unit* unit) {
    NON_NULL(unit);

    return unit->unit_type == sc2::UNIT_TYPEID::PROTOSS_ASSIMILATOR;
}

sc2::Units scbot::Utilities::GetResourcePoints(const sc2::Units& units, bool minerals, bool vespene, bool extractors) {
    sc2::Units points;

    for (const auto& unit : units) {
        if (minerals && IsMineralField(unit)) {
            if (IsDepleted(unit)) {
                continue;
            }

            points.push_back(unit);
        } else if (vespene && IsVespeneGeyser(unit)) {
            if (IsDepleted(unit)) {
                continue;
            }
            
            points.push_back(unit);
        } else if (extractors && IsExtractor(unit)) {
            points.push_back(unit);
        }
    }

    return points;
}

bool scbot::Utilities::IsWorker(const sc2::Unit* unit) {
    NON_NULL(unit);

    const auto type = unit->unit_type;

    return 
        type == sc2::UNIT_TYPEID::PROTOSS_PROBE ||
        type == sc2::UNIT_TYPEID::TERRAN_SCV ||
        type == sc2::UNIT_TYPEID::ZERG_DRONE;
}

float scbot::Utilities::ToSecondsFromGameTime(float time) {
    return time / 22.4f;
}

float scbot::Utilities::ToGameTimeFromSeconds(float time) {
    return time * 22.4f;
}

bool scbot::Utilities::AnyWithinRange(const sc2::Units& units, const sc2::Point2D& point, float range) {
    range *= range;

    for (const auto& unit : units) {
        if (sc2::DistanceSquared2D(unit->pos, point) <= range) {
            return true;
        }
    }

    return false;
}

sc2::Units scbot::Utilities::WithinRange(const sc2::Units& units, const sc2::Point2D& point, float range) {
    sc2::Units within_range;

    range *= range;

    for (const auto& unit : units) {
        if (sc2::DistanceSquared2D(unit->pos, point) <= range) {
            within_range.push_back(unit);
        }
    }

    return within_range;
}

uint64_t scbot::Utilities::CountWithinRange(const sc2::Units& units, const sc2::Point2D& point, float range) {
    uint64_t count = 0;

    range *= range;

    for (const auto& unit : units) {
        if (sc2::DistanceSquared2D(unit->pos, point) <= range) {
            count++;
        }
    }

    return count;
}

const sc2::Unit* scbot::Utilities::ClosestTo(const sc2::Units& units, const sc2::Point2D& point)
{
    NON_EMPTY(units);

    const sc2::Unit* closest_unit = nullptr;
    float closest_distance = std::numeric_limits<float>::max();

    for (const auto& unit : units) {
        float distance = sc2::DistanceSquared2D(unit->pos, point);
        if (distance < closest_distance) {
            closest_unit = unit;
            closest_distance = distance;
        }
    }

    return closest_unit;
}

float scbot::Utilities::DistanceToClosest(const sc2::Units& units, const sc2::Point2D& point) {
    NON_EMPTY(units);

    float closest_distance = std::numeric_limits<float>::max();

    for (const auto& unit : units) {
        float distance = sc2::DistanceSquared2D(unit->pos, point);
        if (distance < closest_distance) {
            closest_distance = distance;
        }
    }

    return std::sqrt(closest_distance);
}

const sc2::Point2D scbot::Utilities::ClosestTo(const std::vector<sc2::Point2D>& points, const sc2::Point2D& point)
{
    NON_EMPTY(points);
    
    sc2::Point2D closest_point;
    float closest_distance = std::numeric_limits<float>::max();

    for (const auto& p : points) {
        float distance = sc2::DistanceSquared2D(p, point);
        if (distance < closest_distance) {
            closest_point = p;
            closest_distance = distance;
        }
    }

    return closest_point;
}

const sc2::Point3D scbot::Utilities::ClosestTo(const std::vector<sc2::Point3D>& points, const sc2::Point3D& point) {
    NON_EMPTY(points);
    
    sc2::Point3D closest_point;
    float closest_distance = std::numeric_limits<float>::max();

    for (const auto& p : points) {
        float distance = sc2::DistanceSquared3D(p, point);
        if (distance < closest_distance) {
            closest_point = p;
            closest_distance = distance;
        }
    }

    return closest_point;
}

const sc2::Unit* scbot::Utilities::SelectUnit(const sc2::Units& units, std::function<bool(const sc2::Unit*, const sc2::Unit*)> predicate) {
    NON_EMPTY(units);
    
    const auto& it = std::min_element(units.begin(), units.end(), predicate);

    return it != units.end() ? *it : nullptr;
}

const sc2::Unit* scbot::Utilities::SelectUnitMin(const sc2::Units& units, std::function<float(const sc2::Unit*)> predicate) {
    NON_EMPTY(units);
    
    const sc2::Unit* best_unit = nullptr;
    float best_value = std::numeric_limits<float>::max();

    for (const auto& unit : units) {
        float value = predicate(unit);

        if (best_unit == nullptr || value < best_value) {
            best_unit = unit;
            best_value = value;
        }
    }

    return best_unit;
}

const sc2::Unit* scbot::Utilities::SelectUnitMax(const sc2::Units& units, std::function<float(const sc2::Unit*)> predicate) {
    NON_EMPTY(units);

    const sc2::Unit* best_unit = nullptr;
    float best_value = std::numeric_limits<float>::min();

    for (const auto& unit : units) {
        float value = predicate(unit);

        if (best_unit == nullptr || value > best_value) {
            best_unit = unit;
            best_value = value;
        }
    }

    return best_unit;
}

const sc2::Unit* scbot::Utilities::ClosestAverageTo(const sc2::Units& units, const sc2::Units& points) {
    NON_EMPTY(units);
    NON_EMPTY(points);

    const sc2::Unit* best_unit = nullptr;
    float best_distance = std::numeric_limits<float>::max();

    for (const auto& unit : units) {
        float total_distance = 0.0f;

        for (const auto& point : points) {
            total_distance += sc2::DistanceSquared2D(unit->pos, point->pos);
        }

        float average_distance = total_distance / points.size();

        if (best_unit == nullptr || average_distance < best_distance) {
            best_unit = unit;
            best_distance = average_distance;
        }
    }

    return best_unit;
}

const sc2::Point3D& scbot::Utilities::ClosestAverageTo(const std::vector<sc2::Point3D>& units, const sc2::Units& points) {
    NON_EMPTY(units);

    const sc2::Point3D* best_point = nullptr;
    float best_distance = std::numeric_limits<float>::max();

    for (const auto& point : units) {
        float total_distance = 0.0f;

        for (const auto& p : points) {
            total_distance += sc2::DistanceSquared3D(point, p->pos);
        }

        float average_distance = total_distance / points.size();

        if (best_point == nullptr || average_distance < best_distance) {
            best_point = &point;
            best_distance = average_distance;
        }
    }

    return *best_point;
}

bool scbot::Utilities::RequiresPower(const sc2::Unit* unit) {
    NON_NULL(unit);

    return scdata::PoweredStructures.find(unit->unit_type) != scdata::PoweredStructures.end();
}

bool scbot::Utilities::IsPowered(const sc2::Unit* unit) {
    NON_NULL(unit);
    
    return unit->build_progress == 1.0f;
}

sc2::Units scbot::Utilities::Union(const sc2::Units& a, const sc2::Units& b, bool check_duplicates) {
    sc2::Units result = a;

    for (const auto& unit : b) {
        if (check_duplicates && std::find(result.begin(), result.end(), unit) != result.end()) {
            continue;
        }

        result.push_back(unit);
    }

    return result;
}

sc2::Units scbot::Utilities::SortByDistance(const sc2::Units& units, const sc2::Point2D& point)
{
    sc2::Units sorted_units = units;

    std::sort(sorted_units.begin(), sorted_units.end(), [point](const sc2::Unit* a, const sc2::Unit* b) {
        return sc2::DistanceSquared2D(a->pos, point) < sc2::DistanceSquared2D(b->pos, point);
    });

    return sorted_units;
}

sc2::Units scbot::Utilities::SortByAverageDistance(const sc2::Units& units, const sc2::Units& points)
{
    sc2::Units sorted_units = units;

    std::sort(sorted_units.begin(), sorted_units.end(), [points](const sc2::Unit* a, const sc2::Unit* b) {
        float total_distance_a = 0.0f;
        float total_distance_b = 0.0f;

        for (const auto& point : points) {
            total_distance_a += sc2::DistanceSquared2D(a->pos, point->pos);
            total_distance_b += sc2::DistanceSquared2D(b->pos, point->pos);
        }

        return total_distance_a / points.size() < total_distance_b / points.size();
    });

    return sorted_units;
}
