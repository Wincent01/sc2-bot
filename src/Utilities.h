#pragma once

#include <sc2api/sc2_unit.h>

namespace scbot::Utilities {

// Unit utility functions

/**
 * @brief Checks if a unit has any order (current or queued) with the given ability id.
 * 
 * @param unit The unit to check.
 * @param ability_id The ability id to check for.
 * @return true If the unit has an order with the given ability id, false otherwise.
 */
bool HasQueuedOrder(const sc2::Unit* unit, sc2::ABILITY_ID ability_id);

/**
 * @brief Checks if any one unit in a set has any order (current or queued) with the given ability id.
 * 
 * @param units The set of units to check.
 * @param ability_id The ability id to check for.
 * @return true If any unit in the set has an order with the given ability id, false otherwise.
 */
bool HasQueuedOrder(const sc2::Units& units, sc2::ABILITY_ID ability_id);

/**
 * @brief Checks if a unit has any order (current or queued) with the given ability id and target unit tag.
 * 
 * @param unit The unit to check.
 * @param ability_id The ability id to check for.
 * @param target_unit_tag The target unit tag to check for.
 * @return true If the unit has an order with the given ability id and target unit tag, false otherwise.
 */
bool HasQueuedOrder(const sc2::Unit* unit, sc2::ABILITY_ID ability_id, sc2::Tag target_unit_tag);

/**
 * @brief Checks if any one unit in a set has any order (current or queued) with the given ability id and target unit tag.
 * 
 * @param units The set of units to check.
 * @param ability_id The ability id to check for.
 * @param target_unit_tag The target unit tag to check for.
 * @return true If any unit in the set has an order with the given ability id and target unit tag, false otherwise.
 */
bool HasQueuedOrder(const sc2::Units& units, sc2::ABILITY_ID ability_id, sc2::Tag target_unit_tag);

/**
 * @brief Checks if a unit is in progress (currently being built/trained).
 * 
 * @param unit The unit to check.
 * @return true If the unit is in progress, false otherwise.
 */
bool IsInProgress(const sc2::Unit* unit);

/**
 * @brief Checks if any one unit in a set is in progress (currently being built/trained).
 * 
 * @param units The set of units to check.
 * @return true If any unit in the set is in progress, false otherwise.
 */
bool AllInProgress(const sc2::Units& units);

/**
 * @brief Filter out units based on a predicate.
 * 
 * @param units The set of units to filter.
 * @param predicate The predicate to filter by.
 * @return The set of units that pass the predicate.
 */
sc2::Units FilterUnits(const sc2::Units& units, std::function<bool(const sc2::Unit*)> predicate);

/**
 * @brief Filters out units that are in progress (currently being built/trained).
 * 
 * @param units The set of units to filter.
 * @return The set of units that are not in progress.
 */
sc2::Units FilterOutInProgress(const sc2::Units& units);

/**
 * @brief Check if a unit is gathering resources, either minerals or vespene gas.
 * 
 * @param unit The unit to check.
 * @return true If the unit is gathering resources, false otherwise.
 * @note This function is only applicable to workers, all other units will return false.
 */
bool IsGathering(const sc2::Unit* unit);

/**
 * @brief Check if a unit is gathering from a set of points.
 * 
 * @param unit The unit to check.
 * @param points The set of mineral fields to check against.
 * @return true If the unit is gathering from a set of points, false otherwise.
 * @note This function is only applicable to workers, all other units will return false.
 */
bool IsGatheringFrom(const sc2::Unit* unit, const sc2::Units& points);

/**
 * @brief Check if a unit is idle.
 * 
 * @param unit The unit to check.
 * @return true If the unit is idle, false otherwise.
 * @note Does not account for planned orders in the context of the bot.
 */
bool IsIdle(const sc2::Unit* unit);

/**
 * @brief Check if a unit, either a mineral field or vespene geyser, is depleted.
 * 
 * @param unit The unit to check.
 * @return true If the unit is depleted, false otherwise.
 * @note This function is only applicable to mineral fields and vespene geysers, all other units will return false.
 */
bool IsDepleted(const sc2::Unit* unit);

/**
 * @brief Check if a unit is a mineral field.
 * 
 * @param unit The unit to check.
 * @return true If the unit is a mineral field, false otherwise.
 */
bool IsMineralField(const sc2::Unit* unit);

/**
 * @brief Check if a unit is a vespene geyser.
 * 
 * @param unit The unit to check.
 * @return true If the unit is a vespene geyser, false otherwise.
 */
bool IsVespeneGeyser(const sc2::Unit* unit);

/**
 * @brief Check if a unit is a extractor.
 * 
 * @param unit The unit to check.
 * @return true If the unit is a extractor, false otherwise.
 */
bool IsExtractor(const sc2::Unit* unit);

/**
 * @brief Take a subset from a set of units that are gathering points of interest.
 * 
 * @param units The set of units to filter.
 * @param minerals Whether to include mineral fields.
 * @param vespene Whether to include vespene geysers.
 * @param extractors Whether to include extractors.
 * @return The set of units that are gathering points of interest.
 */
sc2::Units GetResourcePoints(const sc2::Units& units, bool minerals = true, bool vespene = true, bool extractors = true);

/**
 * @brief Check if a unit is a worker.
 * 
 * @param unit The unit to check.
 * @return true If the unit is a worker, false otherwise.
 */
bool IsWorker(const sc2::Unit* unit);

/**
 * @brief Convert a game time unit to seconds.
 * 
 * @param time The game time unit to convert.
 * @return float The time in seconds.
 * @note 1 second is 22.4 steps in game time.
 */
float ToSecondsFromGameTime(float time);

/**
 * @brief Convert seconds to a game time unit.
 * 
 * @param time The time in seconds to convert.
 * @return float The game time unit.
 * @note 1 second is 22.4 steps in game time.
 */
float ToGameTimeFromSeconds(float time);

/**
 * @brief Check if any unit is within a certain distance of a point.
 * 
 * @param units The set of units to check.
 * @param point The point to check against.
 * @param range The range to check within.
 * @return true If any unit is within the range of the point, false otherwise.
 */
bool AnyWithinRange(const sc2::Units& units, const sc2::Point2D& point, float range);

/**
 * @brief Return the units within a certain distance of a point.
 * 
 * @param units The set of units to check.
 * @param point The point to check against.
 * @param range The range to check within.
 * @return The units within the range of the point.
 */
sc2::Units WithinRange(const sc2::Units& units, const sc2::Point2D& point, float range);

/**
 * @brief Return the number of units within a certain distance of a point.
 * 
 * @param units The set of units to check.
 * @param point The point to check against.
 * @param range The range to check within.
 * @return The number of units within the range of the point.
 */
uint64_t CountWithinRange(const sc2::Units& units, const sc2::Point2D& point, float range);

/**
 * @brief Return the closest unit to a point.
 * 
 * @param units The set of units to check.
 * @param point The point to check against.
 * @return The closest unit to the point.
 */
const sc2::Unit* ClosestTo(const sc2::Units& units, const sc2::Point2D& point);

/**
 * @brief Return the closest point to a point.
 * 
 * @param points The set of points to check.
 * @param point The point to check against.
 */
const sc2::Point2D ClosestTo(const std::vector<sc2::Point2D>& points, const sc2::Point2D& point);

/**
 * @brief Return the closest point to a point.
 * 
 * @param points The set of points to check.
 * @param point The point to check against.
 */
const sc2::Point3D ClosestTo(const std::vector<sc2::Point3D>& points, const sc2::Point3D& point);

/**
 * @brief Return the best unit based on a predicate.
 * 
 * @param units The set of units to check.
 * @param predicate The predicate to check against.
 * @return The best unit based on the predicate.
 */
const sc2::Unit* SelectUnit(const sc2::Units& units, std::function<float()> predicate);

/**
 * @brief Return the best unit based on a predicate.
 * 
 * @param units The set of units to check.
 * @param predicate The predicate to check against.
 * @return The best unit based on the predicate.
 */
const sc2::Unit* SelectUnitMin(const sc2::Units& units, std::function<float()> predicate);

/**
 * @brief Return the best unit based on a predicate.
 * 
 * @param units The set of units to check.
 * @param predicate The predicate to check against.
 * @return The best unit based on the predicate.
 */
const sc2::Unit* SelectUnitMax(const sc2::Units& units, std::function<bool(const sc2::Unit*, float)> predicate);

/**
 * @brief Return the unit with the closest average distance to a set of other units.
 * 
 * @param units The set of units to check.
 * @param points The set of points to check against.
 * @return The unit with the closest average distance to the points.
 */
const sc2::Unit* ClosestAverageTo(const sc2::Units& units, const sc2::Units& points);

/**
 * @brief Return the points with the closest average distance to a set of other units.
 * 
 * @param units The set of points to check.
 * @param points The set of points to check against.
 * @return The unit with the closest average distance to the points.
 */
const sc2::Point3D& ClosestAverageTo(const std::vector<sc2::Point3D>& units, const sc2::Units& points);

/**
 * @brief Check if a unit requires power by a pylon.
 * 
 * @param unit The unit to check.
 * @return true If the unit requires power, false otherwise.
 */
bool RequiresPower(const sc2::Unit* unit);

/**
 * @brief Check if a unit is powered by a pylon.
 * 
 * @param unit The unit to check.
 * @return true If the unit is powered, false otherwise.
 * @note Only applies to units that require power.
 */
bool IsPowered(const sc2::Unit* unit);

/**
 * @brief Takes the union of two sets of units.
 * 
 * @param a The first set of units.
 * @param b The second set of units.
 * @param check_duplicates Whether to check for duplicates.
 * @return The union of the sets.
 */
sc2::Units Union(const sc2::Units& a, const sc2::Units& b, bool check_duplicates = false);

} // namespace scbot::Utilities
