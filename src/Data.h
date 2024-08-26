#pragma once

#include <sc2api/sc2_typeenums.h>
#include <sc2api/sc2_common.h>
#include <sc2api/sc2_unit.h>

#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace scdata {
    
    struct Ramp
    {
        sc2::Point2D point;
    };

    struct DelayedOrder
    {
        sc2::ABILITY_ID ability_id;
        sc2::Point2D position;
        sc2::Tag target_unit_tag;
        float time;
    };

    struct TrainResult
    {
        bool success;
        sc2::ABILITY_ID ability_id;
        const sc2::Unit* building;
        sc2::Point2D warp_position;

        TrainResult(bool success_, sc2::ABILITY_ID ability_id_, const sc2::Unit* building_, sc2::Point2D warp_position_):
            success(success_),
            ability_id(ability_id_),
            building(building_),
            warp_position(warp_position_) {}

        TrainResult(bool success_, sc2::ABILITY_ID ability_id_, const sc2::Unit* building_):
            success(success_),
            ability_id(ability_id_),
            building(building_),
            warp_position({0.0f, 0.0f}) {}

        TrainResult(bool success_):
            success(success_),
            ability_id(sc2::ABILITY_ID::INVALID),
            building(nullptr),
            warp_position({0.0f, 0.0f}) {}
    };

    struct ResourcePair
    {
        int32_t minerals;
        int32_t vespene;

        // Subtract operator
        ResourcePair operator-(const ResourcePair& other) const
        {
            return {minerals - other.minerals, vespene - other.vespene};
        }

        bool operator<(const ResourcePair& other) const
        {
            return minerals < other.minerals || vespene < other.vespene;
        }

        bool operator>(const ResourcePair& other) const
        {
            return minerals > other.minerals || vespene > other.vespene;
        }

        bool operator==(const ResourcePair& other) const
        {
            return minerals == other.minerals && vespene == other.vespene;
        }

        bool operator!=(const ResourcePair& other) const
        {
            return minerals != other.minerals || vespene != other.vespene;
        }

        bool operator<=(const ResourcePair& other) const
        {
            return minerals <= other.minerals || vespene <= other.vespene;
        }

        bool operator>=(const ResourcePair& other) const
        {
            return minerals >= other.minerals || vespene >= other.vespene;
        }

        ResourcePair operator+(const ResourcePair& other) const
        {
            return {minerals + other.minerals, vespene + other.vespene};
        }

        // +=/-= operator
        ResourcePair& operator+=(const ResourcePair& other)
        {
            minerals += other.minerals;
            vespene += other.vespene;
            return *this;
        }

        ResourcePair& operator-=(const ResourcePair& other)
        {
            minerals -= other.minerals;
            vespene -= other.vespene;
            return *this;
        }
    };

    struct BuildResult
    {
        bool success;
        float time;
        ResourcePair cost;
        sc2::Tag delayed_order_tag;
        DelayedOrder delayed_order;

        BuildResult(bool success_, float time_, ResourcePair cost_, sc2::Tag delayed_order_tag_, DelayedOrder delayed_order_):
            success(success_),
            time(time_),
            cost(cost_),
            delayed_order_tag(delayed_order_tag_),
            delayed_order(delayed_order_) {}

        BuildResult(bool success_, float time_, ResourcePair cost_):
            success(success_),
            time(time_),
            cost(cost_),
            delayed_order_tag(0),
            delayed_order({sc2::ABILITY_ID::INVALID, {0.0f, 0.0f}, 0, 0.0f}) {}

        BuildResult(bool success_):
            success(success_),
            time(0.0f),
            cost({0, 0}),
            delayed_order_tag(0),
            delayed_order({sc2::ABILITY_ID::INVALID, {0.0f, 0.0f}, 0, 0.0f}) {}

        BuildResult():
            success(false),
            time(0.0f),
            cost({0, 0}),
            delayed_order_tag(0),
            delayed_order({sc2::ABILITY_ID::INVALID, {0.0f, 0.0f}, 0, 0.0f}) {}

        bool IsPlanning() const
        {
            return success || time != 0.0f;
        }

        bool IsSuccess() const
        {
            return success;
        }
    };

    struct ActionPlan
    {
        sc2::ABILITY_ID ability_id;
    };

    extern std::unordered_map<sc2::ABILITY_ID, std::unordered_set<sc2::UNIT_TYPEID>> AbilityRequirements;

    extern std::unordered_set<sc2::UNIT_TYPEID> PoweredStructures;

    extern std::unordered_set<sc2::ABILITY_ID> MiningAbilities;

    extern std::unordered_map<sc2::ABILITY_ID, ResourcePair> AbilityCosts;

    extern std::unordered_map<sc2::ABILITY_ID, sc2::UNIT_TYPEID> AssociatedBuilding;

    extern std::unordered_set<sc2::ABILITY_ID> UnitTrainTypes;

    extern std::unordered_set<sc2::ABILITY_ID> UpgradeTypes;

    extern std::unordered_map<sc2::ABILITY_ID, sc2::ABILITY_ID> UnitTrainAbilityWarpTypes;

    extern std::unordered_set<sc2::ABILITY_ID> StructureTypes;
}