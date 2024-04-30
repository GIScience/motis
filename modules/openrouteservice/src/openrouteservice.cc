#include "motis/openrouteservice/openrouteservice.h"

#include <cista/hash.h>
#include <filesystem>

#include "motis/core/common/logging.h"
#include "motis/module/event_collector.h"

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include "motis/module/context/motis_http_req.h"

namespace mm = motis::module;
namespace fs = std::filesystem;

namespace motis::openrouteservice {

openrouteservice::openrouteservice()
    : module("Openrouteservice Options", "openrouteservice") {
  param(url_, "url", "ORS API endpoint");
  param(api_key_, "api_key", "ORS API key");
}

openrouteservice::~openrouteservice() = default;

void openrouteservice::init(motis::module::registry& reg) {
  std::cout << "URL: " << this->url_ << std::endl;
  std::cout << "API key: " << this->api_key_ << std::endl;
  // Store the url and api key in the impl struct
  config = std::make_unique<impl>(url_, api_key_);
  //  reg.subscribe("/init", [this] { init_async(); }, {});
  reg.register_op("/osrm/table",
                  [&](mm::msg_ptr const& msg) { return table(msg); }, {});

  reg.register_op("/osrm/one_to_many", [&](mm::msg_ptr const& msg) { return one_to_many(msg); }, {});

  reg.register_op("/osrm/via", [&](mm::msg_ptr const& msg) { return via(msg); }, {});
}

template <typename Req>
mm::msg_ptr sources_to_targets(Req const* req, openrouteservice::impl* config) {
  auto const timer = motis::logging::scoped_timer{"openrouteservice.matrix"};
  //  auto& thor = config->get();
  //
  //  // Encode OSRMManyToManyRequest as valhalla request.
  //  auto doc = j::Document{};
  //  encode_request(req, doc);
  //
  //  // Decode request.
  //  v::Api request;
  //  v::from_json(doc, v::Options::sources_to_targets, request);
  //  auto& options = *request.mutable_options();
  //
  //  // Get the costing method.
  //  auto mode = v::sif::TravelMode::kMaxTravelMode;
  //  auto const mode_costing = thor.factory_.CreateModeCosting(options, mode);
  //
  //  // Find path locations (loki) for sources and targets.
  //  thor.loki_worker_.matrix(request);
  //
  //  // Run matrix algorithm.
  //  auto const res = thor.matrix_.SourceToTarget(
  //      options.sources(), options.targets(), *config->reader_, mode_costing,
  //      mode, 4000000.0F);
  //  thor.matrix_.clear();
  //
  //  // Encode OSRM response.
  mm::message_creator fbb;
  fbb.create_and_finish(
      MsgContent_OSRMOneToManyResponse,
      CreateOSRMOneToManyResponse(
          fbb, fbb.CreateVectorOfStructs(std::vector<osrm::Cost>{}))
          .Union());
  return make_msg(fbb);
}

mm::msg_ptr openrouteservice::table(mm::msg_ptr const& msg) const {
  using osrm::OSRMManyToManyRequest;
  auto const req = motis_content(OSRMManyToManyRequest, msg);
  return sources_to_targets(req, config.get());
}

mm::msg_ptr openrouteservice::one_to_many(mm::msg_ptr const& msg) const {
  using osrm::OSRMOneToManyRequest;
  auto const req = motis_content(OSRMOneToManyRequest, msg);
  return sources_to_targets(req, config.get());
}

std::string_view translate_mode(std::string_view s) {
  switch (cista::hash(s)) {
    case cista::hash("foot"): return "foot-walking";
    case cista::hash("bike"): return "cycling-regular";
    case cista::hash("car"): return "driving-car";
    default: return "foot-walking";
  }
}

mm::msg_ptr openrouteservice::via(mm::msg_ptr const& msg) const {
  using osrm::OSRMViaRouteRequest;
  auto const req = motis_content(OSRMViaRouteRequest, msg);

  auto const size = req->waypoints()->size();
  auto const waypoints = *req->waypoints();
  auto const start_lng = req->waypoints()->Get(0)->lng();
  auto const start_lat = req->waypoints()->Get(0)->lat();
  auto const end_lng = req->waypoints()->Get(size-1)->lng();
  auto const end_lat = req->waypoints()->Get(size-1)->lat();

  auto const mode_str = (std::string) translate_mode(req->profile()->view());

  auto const query =
      url_ + "/directions/" + mode_str +
      "?api_key=" + api_key_ +
      "&start=" + std::to_string(start_lng) + ',' + std::to_string(start_lat) +
      "&end=" + std::to_string(end_lng) + ',' + std::to_string(end_lat);

  auto f = motis_http(query);
  auto v = f->val();
  std::cout << "ORS reply: " << v.body;

  rapidjson::Document doc;
  if (doc.Parse(v.body.data(), v.body.size()).HasParseError()) {
    doc.GetParseError();
    throw utl::fail("ORS response: Bad JSON: {} at offset {}",
                    rapidjson::GetParseError_En(doc.GetParseError()),
                    doc.GetErrorOffset());
  }
  const rapidjson::Value& feature = doc["features"][0];

  // Get totals
  const rapidjson::Value& summary = feature["properties"]["summary"];
  double duration = summary["duration"].GetDouble();
  double distance = summary["distance"].GetDouble();

  // Extract route geometry
  std::vector<double> coordinates;
  for (const rapidjson::Value& coordinate_pair : feature["geometry"]["coordinates"].GetArray()) {
    coordinates.emplace_back(coordinate_pair[1].GetDouble());
    coordinates.emplace_back(coordinate_pair[0].GetDouble());
  }

  // Encode OSRM response
  mm::message_creator fbb;
  fbb.create_and_finish(
      MsgContent_OSRMViaRouteResponse,
      osrm::CreateOSRMViaRouteResponse(
          fbb, static_cast<int>(duration),
          static_cast<double>(distance),
          CreatePolyline(fbb, fbb.CreateVector(coordinates.data(), coordinates.size())))
          .Union());
  return make_msg(fbb);
}

}  // namespace motis::openrouteservice