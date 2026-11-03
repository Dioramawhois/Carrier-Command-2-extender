#pragma once

#include <string>
#include <string_view>

namespace cc2::api::vehicle {

enum class VehicleHudPatchResult {
  kNoMatch,
  kAlreadyPatched,
  kPatched,
  kAmbiguous,
};

// Extend only the stock flight-HUD discriminator. The exact anchor is unique
// to vehicle_hud.lua in the supported game scripts; other chassis behavior is
// deliberately left untouched.
inline VehicleHudPatchResult PatchVehicleHudSource(std::string_view source,
                                                    std::string* output) {
  constexpr std::string_view stock =
      "if def == e_game_object_type.chassis_air_wing_light";
  constexpr std::string_view patchMarker =
      "if (type(cc2x_is_custom_aircraft_type) == \"function\" and "
      "cc2x_is_custom_aircraft_type(def))";
  constexpr std::string_view predicateClause =
      "if (type(cc2x_is_custom_aircraft_type) == \"function\" and "
      "cc2x_is_custom_aircraft_type(def))";
  constexpr std::string_view wingClause =
      "or def == e_game_object_type.chassis_air_wing_light";

  if (source.find(patchMarker) != std::string_view::npos &&
      source.find(wingClause) != std::string_view::npos) {
    return VehicleHudPatchResult::kAlreadyPatched;
  }

  const auto at = source.find(stock);
  if (at == std::string_view::npos) return VehicleHudPatchResult::kNoMatch;
  if (source.find(stock, at + stock.size()) != std::string_view::npos)
    return VehicleHudPatchResult::kAmbiguous;
  if (!output) return VehicleHudPatchResult::kNoMatch;

  const auto lineStart = source.rfind('\n', at);
  const auto indentStart = lineStart == std::string_view::npos ? 0 : lineStart + 1;
  const auto indent = source.substr(indentStart, at - indentStart);
  for (const char ch : indent) {
    if (ch != ' ' && ch != '\t') return VehicleHudPatchResult::kNoMatch;
  }

  const auto lineEnd = at + stock.size();
  std::string_view newline;
  if (lineEnd < source.size() && source[lineEnd] == '\n') {
    newline = "\n";
  } else if (lineEnd + 1 < source.size() && source[lineEnd] == '\r' &&
             source[lineEnd + 1] == '\n') {
    newline = "\r\n";
  } else {
    return VehicleHudPatchResult::kNoMatch;
  }

  output->assign(source.data(), at);
  output->append(predicateClause);
  output->append(newline);
  output->append(indent);
  output->append(wingClause);
  output->append(source.data() + lineEnd, source.size() - lineEnd);
  return VehicleHudPatchResult::kPatched;
}

}  // namespace cc2::api::vehicle
